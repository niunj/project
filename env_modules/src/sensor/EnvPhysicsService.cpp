/// @file EnvPhysicsService.cpp —— 环境物理服务实现 (B3)。
///
/// 波段映射物理:
///   σ_分子: 晴空基线 (clearSkyEnv 标定值, 水汽/CO2 吸收为主)
///   σ_气溶胶: Koschmieder σ=3.912/V_km, Ångström 幂律 (λ0=0.55μm, α=1.3)
///             —— 波长越长气溶胶散射越弱, 红外端以分子吸收基线为主
///   σ_云: coverage × 层云液态水消光 (TV/SWIR 散射主导 ~2/km;
///         MWIR/LWIR 云滴吸收/散射 ~4/6 /km, 阴天红外透过显著恶化)
///   sky_radiance: 晴空基线 × 昼因子插值 (TV/SWIR 天空=太阳散射, 夜≈基线×0.02;
///         红外=大气热辐射, 昼夜变化小) + 云贡献 ε_cloud·M(T_cloud)/π·coverage
///         —— 红外云比晴空亮 (云顶温度高于有效大气辐射温度), TV 云昼亮夜黑
///   T_cloud: 云底高 + 层厚/2 处, 由地面 288.15K 按直减率 6.5K/km 递推
///   sun_irradiance: 晴空基线 × sun_light(暮光变暗) × (1-0.8·coverage) × is_day
///
/// 全部输入与天空/云渲染画面同源 (IEnvStack 单一真值源), "改云量/时刻 →
/// 传感器画面变" 的验收即建立在此同源性上。
#include "rocky_env/EnvPhysicsService.h"
#include "rocky_env/EnvApi.h"
#include "EphemerisBridge.h"
#include "AtmospherePhysics.h"
#include <stdexcept>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rockyenv {
namespace sensor {

namespace {

/// 云天类型
int weatherType(double coverage, bool precip) {
    if (precip) return 4;
    if (coverage < 0.10) return 0;   // 晴
    if (coverage < 0.45) return 1;   // 少云
    if (coverage < 0.85) return 2;   // 多云
    return 3;                        // 阴
}

/// 云消光系数 (1/km): TV/SWIR 散射主导; 红外云滴吸收/散射更强
double cloudSigma(BandType t) {
    switch (t) {
        case BandType::TV:
        case BandType::SWIR: return 2.0;   // 散射主导
        case BandType::MWIR: return 4.0;   // 云滴吸收+散射
        case BandType::LWIR: return 6.0;
    }
    return 0.0;
}

/// 云内路径比例 (0..1): 云消光只作用于云内路径段, 俯视云下目标不穿云。
/// σ 为标量均匀介质近似, 此处按视线几何折算期望穿云比例:
///   俯视 (el≤0) 且相机在云底之下 → 路径最高点即相机高度, 不穿云 → 0;
///   仰视 (el>0) → 到达云底后的路径段视为云内, 以 1km 参考程折算比例;
///   相机已在云中/云上 → 仰视全程云内 → 1。
/// (逐像素穿云几何 — 云层往返双段 τ — 留后续; 本近似消除"云量>0 即全程
///  云内消光"导致的地面目标信号全灭问题。)
double cloudPathFraction(const EnvRequest& req, double cloudBaseM) {
    if (req.los_el_deg <= 0.0)
        return (req.alt >= cloudBaseM) ? 1.0 : 0.0;   // 俯视: 云下不穿云 / 云上保守取云内
    if (req.alt >= cloudBaseM) return 1.0;             // 云中/云上仰视: 全程云内
    const double sinEl = std::max(std::sin(req.los_el_deg * M_PI / 180.0), 1.0e-3);
    const double dToCloud = (cloudBaseM - req.alt) / sinEl;   // 到云底斜距 (m)
    constexpr double kRefPathM = 1000.0;               // 参考程: 云底外 1km 内的仰视段计穿云
    return std::clamp(1.0 - dToCloud / kRefPathM, 0.0, 1.0);
}

/// 雨消光系数 (1/km, Napier): 经验律 α[dB/km] = a · R^b (Marshall-Palmer 型),
/// R 为雨强 (mm/h)。系数随波段 (文献典型值):
///   TV/SWIR (0.4-1.6μm, 雨滴 Mie/几何散射) a≈0.21 b≈0.74
///   MWIR   (3-5μm)                         a≈0.15 b≈0.72
///   LWIR   (8-12μm, 雨滴尺寸≫λ 消光最弱)    a≈0.09 b≈0.70
/// 降水强度 p (0..1, 1=暴雨) 标定到 R = p·50 mm/h (50mm/h ≈ 暴雨)。
/// 例: p=0.8 → R=40mm/h: TV 3.1dB/km (V≈5.5km), LWIR 1.1dB/km。
double rainSigma(const BandSpec& band, double precipP) {
    if (precipP <= 0.0) return 0.0;
    const double R = precipP * 50.0;               // mm/h
    double a = 0.0, b = 1.0;
    switch (band.type) {
        case BandType::TV:
        case BandType::SWIR: a = 0.21; b = 0.74; break;
        case BandType::MWIR: a = 0.15; b = 0.72; break;
        case BandType::LWIR: a = 0.09; b = 0.70; break;
    }
    return a * std::pow(R, b) / 4.343;   // dB/km → Np/km
}

} // namespace

void EnvPhysicsService::bind(IEnvStack* stack) {
    envStack_=stack;bandAtmo_=nullptr;physical_state_valid_=false;
    std::lock_guard<std::mutex> lock(ocean_cache_mutex_);
    oceanSampler_=nullptr;foam_cached_=false;++ocean_generation_;
    seaTempK_=0;beaufort_=-1;
}

void EnvPhysicsService::update(const EnvFrame& frame) {
    update(frame.time_sec,frame.hours_utc,frame.cloud_cover,frame.wind_speed,frame.precipitation);
    if(envStack_) return;
    EphemerisBridge ephemeris;
    ephemeris.setDateTime(frame.year,frame.month,frame.day,frame.hours_utc);
    EnvSunInfo sun,moon; float phase=0;
    if(!ephemeris.sunInfo(frame.model,sun)) throw std::runtime_error("headless Rocky ephemeris failed");
    SkyBodies bodies; bodies.valid=true;
    bodies.sun.dirENU=sun.dirENU; bodies.sun.dirECEF=sun.dirECEF;
    bodies.sun.elDeg=sun.elevationDeg; bodies.sun.azDeg=sun.azimuthDeg;
    if(ephemeris.moonInfo(frame.model,moon,phase)) {
        bodies.moon.dirENU=moon.dirENU; bodies.moon.dirECEF=moon.dirECEF;
        bodies.moon.elDeg=moon.elevationDeg; bodies.moon.azDeg=moon.azimuthDeg; bodies.moonPhase=phase;
    }
    const auto state=computeAtmosphere(bodies,1,1,{frame.visibility,frame.cloud_cover,frame.wind_speed,frame.precipitation});
    sunDirEnu_=state.sun_dir_enu; isDay_=state.is_day;
    sunLight_=std::clamp(double(std::max({state.sun_light.r,state.sun_light.g,state.sun_light.b})),0.,1.);
    visibilityM_=frame.visibility; cloudCover_=std::clamp(double(frame.cloud_cover),0.,1.);
    cloudBaseM_=frame.cloud_base_m;
    sunElevationDeg_=state.sun_el_deg;physical_state_valid_=true;
}

void EnvPhysicsService::update(double simTime, double hoursUtc,
                               double cloudCover, double windSpeed, double precip) {
    (void)windSpeed;   // B6 雷达海杂波消费 (海况), 红外/可见光链暂不使用
    simTime_ = simTime;
    hoursUtc_ = hoursUtc;
    precip_ = (precip >= 0.0) ? std::clamp(precip, 0.0, 1.0) : 0.0;
    bandAtmo_=nullptr;physical_state_valid_=false;

    // 缺省真值 (无栈 / 栈查询失败时的回退)
    cloudCover_ = cloudCover >= 0.0 ? std::clamp(cloudCover,0.0,1.0) : 0.0;
    cloudBaseM_ = 1200.0;
    sunLight_ = 1.0;
    isDay_ = 1.0;
    visibilityM_ = 10000.0;
    sunDirEnu_ = vsg::vec3(0.3f, 0.8f, 0.2f);   // 与旧管线硬编码方向一致
    seaTempK_ = 0.0;
    {
        std::lock_guard<std::mutex> lock(ocean_cache_mutex_);
        oceanSampler_=nullptr;foam_cached_=false;++ocean_generation_;
    }
    beaufort_ = -1;

    if (!envStack_) return;

    // 批次2: 谱大气能力缓存 (sky 模块实现 bandSky; 无实现保持 nullptr 走基线路径)
    bandAtmo_ = envStack_->query<IAtmosphereQuery>();

    // 大气光照真值 (sky 模块每帧产出; 与天空渲染画面同源)
    if (auto* sky = envStack_->module("sky")) {
        if (auto* lp = dynamic_cast<ILightingProvider*>(sky)) {
            AtmosphereState st;
            if (lp->getAtmosphereState(st)) {
                sunLight_ = std::clamp(
                    std::max({st.sun_light.r, st.sun_light.g, st.sun_light.b}), 0.0f, 1.0f);
                isDay_ = std::clamp(double(st.is_day), 0.0, 1.0);
                sunElevationDeg_=st.sun_el_deg;physical_state_valid_=true;
                if (st.visibility > 0.0f) visibilityM_ = double(st.visibility);
                if (vsg::length(st.sun_dir_enu) > 0.5f) sunDirEnu_ = st.sun_dir_enu;
            }
        }
    }

    // 云真值 (与云渲染画面同源)
    if (auto* cloud = envStack_->module("cloud")) {
        if (auto* cq = dynamic_cast<ICloudQuery*>(cloud)) {
            float cov = 0.0f, base = 0.0f;
            if (cq->cloudBase(base)) cloudBaseM_ = double(base);
            cov = cq->totalCoverage();
            cloudCover_ = (cloudCover >= 0.0) ? std::clamp(cloudCover, 0.0, 1.0)
                                              : double(cov);
        } else if (cloudCover >= 0.0) {
            cloudCover_ = std::clamp(cloudCover, 0.0, 1.0);
        }
    } else if (cloudCover >= 0.0) {
        cloudCover_ = std::clamp(cloudCover, 0.0, 1.0);
    }

    // 批次3: 海洋真值 (与海面渲染同源; EnvAnswer 下发供 B6 海杂波/海面背景消费)
    if (auto* oc = envStack_->module("ocean")) {
        if (auto* os = dynamic_cast<IOceanSampler*>(oc)) {
            seaTempK_ = os->seaTempK();
            beaufort_ = os->seaState().beaufort;
            // Metadata is cheap. Spatial samples may require a complete CPU
            // IFFT: defer them until a consumer explicitly requests ocean data.
            std::lock_guard<std::mutex> lock(ocean_cache_mutex_);
            oceanSampler_=os;
        }
    }
}

double EnvPhysicsService::cloudContribution(const BandSpec& band) const {
    const double cov = std::clamp(cloudCover_, 0.0, 1.0);
    // 云中温度: 云底+层厚/2 处, 地面 288.15K 直减率 6.5K/km
    BandRadianceLut lut;
    lut.build(band);
    const double tCloud = std::max(210.0, 288.15 - 6.5e-3 * (cloudBaseM_ + 300.0));
    const double lCloud = 0.96 * lut.exitance(float(tCloud)) / M_PI;  // 近黑体云顶
    // TV 云昼亮 (反射) 夜黑; 红外云恒亮 (热发射)
    return cov * ((band.type == BandType::TV) ? lCloud * (0.06 + 0.5 * isDay_) : lCloud);
}

double EnvPhysicsService::rainExtinction(const BandSpec& band) const {
    return rainSigma(band, precip_);
}

double EnvPhysicsService::representativeFoam() const {
    std::lock_guard<std::mutex> lock(ocean_cache_mutex_);
    if(!oceanSampler_)return 0;
    if(!foam_cached_||foam_generation_!=ocean_generation_||foam_time_!=simTime_) {
        // Retain the original 16 locations and exact update timestamp. Only
        // publish the cache after all samples succeed; exceptions are retryable.
        double sum=0;
        for(int i=0;i<4;++i)for(int k=0;k<4;++k)
            sum+=oceanSampler_->foamCoverage(-300.0+200.0*i,-300.0+200.0*k,simTime_);
        foamFraction_=sum/16.;foam_time_=simTime_;foam_generation_=ocean_generation_;foam_cached_=true;
    }
    return foamFraction_;
}

EnvAnswer EnvPhysicsService::answer(const EnvRequest& req) const {return answerImpl(req,true);}
EnvAnswer EnvPhysicsService::answerAtmosphere(const EnvRequest& req) const {return answerImpl(req,false);}

EnvAnswer EnvPhysicsService::answerImpl(const EnvRequest& req,bool includeOcean) const {
    // Start with daytime clear-sky spectra and apply the shared ephemeris
    // state below. The legacy scalar-only update has no observer coordinates;
    // it deliberately retains default daylight instead of guessing from UTC.
    // UTC is not local solar time. A headless ephemeris state is just as
    // authoritative as a state obtained through the graphical environment.
    EnvAnswer e = clearSkyEnv(req.band, false);
    const double lambdaC = 0.5 * (req.band.lambda_min + req.band.lambda_max) * 1e6;

    double sky = e.sky_radiance;
    double sun = e.sun_irradiance;
    double sigma = e.sigma_per_km;

    {
        const double cov = std::clamp(cloudCover_, 0.0, 1.0);
        const double sigCloud = cloudSigma(req.band.type);
        const double sigRain = rainSigma(req.band, precip_);   // 雨全程均匀消光
        // 云顶发射贡献 (含 cov 因子; cloudContribution 与烘焙同式, 两路径共用)
        const double cloudSky = cloudContribution(req.band);

        // ---- 批次2: 谱大气优先 —— bandSky 晴空真值 (方向化 L_sky/物理 Esun,
        //      σ 已含气溶胶) + 云调制 (消光/发射叠加, 与旧行为同式) ----
        double lBs = 0.0, sBs = 0.0, eBs = 0.0;
        AtmosphereState physical;
        physical.sun_el_deg=float(sunElevationDeg_);physical.is_day=float(isDay_);
        physical.visibility=float(visibilityM_);
        const bool spectral = bandAtmo_
            ? bandAtmo_->bandSky(req.band.lambda_min,req.band.lambda_max,req.los_el_deg,lBs,sBs,eBs)
            : physical_state_valid_ && sampleBandSky(physical,req.band.lambda_min,req.band.lambda_max,req.los_el_deg,lBs,sBs,eBs);
        if (spectral) {
            sigma = sBs + cov * sigCloud * cloudPathFraction(req, cloudBaseM_) + sigRain;
            sky   = lBs + cloudSky;
            // 直射: 谱 TOA × 斜程透过 (bandSky 内) × 云遮蔽 × 降水衰减 × 夜间硬门控
            sun   = eBs * (1.0 - 0.8 * cov) * (1.0 - 0.5 * precip_)
                  * std::clamp(isDay_, 0.0, 1.0);
        } else {
            // ---- 回退: 晴空标量基线 × 昼因子/能见度调制 (旧行为) ----
            // ---- σ: 分子基线 + 气溶胶 (能见度) + 云 (按视线穿云几何加权) + 雨 ----
            sigma = e.sigma_per_km + aerosolSigma(visibilityM_, lambdaC)
                  + cov * sigCloud * cloudPathFraction(req, cloudBaseM_) + sigRain;

            // TV/SWIR 天空昼因子强相关 (太阳散射); 红外热辐射昼夜变化小
            const double dayFactor = (req.band.type == BandType::TV)   ? (0.02 + 0.98 * isDay_)
                                   : (req.band.type == BandType::SWIR) ? (0.05 + 0.95 * isDay_)
                                                                        : (0.85 + 0.15 * isDay_);
            sky = e.sky_radiance * dayFactor + cloudSky;

            // ---- 直射: sun_light 强度 × 云遮蔽 × 降水衰减 × 昼因子 ----
            sun = e.sun_irradiance * sunLight_ * (1.0 - 0.8 * cov)
                * (1.0 - 0.5 * precip_) * std::clamp(isDay_, 0.0, 1.0);
        }
    }

    e.sigma_per_km = sigma;
    e.sky_radiance = sky;
    e.sun_irradiance = sun;
    e.path_radiance = 0.0;   // 路径辐射 = (1-τ)·sky, shader 内联 (τ 随距离)
    e.cloud_coverage = cloudCover_;
    // 雨时能见度 = Koschmieder (气溶胶+雨总消光), 与晴空气溶胶能见度取小
    const double sigRainTotal = rainSigma(req.band, precip_);
    e.visibility_m = (sigRainTotal > 1e-6)
        ? std::min(visibilityM_, 3.912 / sigRainTotal * 1000.0) : visibilityM_;
    e.is_day = isDay_;
    e.weather_type = weatherType(cloudCover_, precip_ > 0.02);
    if(includeOcean) {
        e.sea_surface_temp_k=seaTempK_;
        e.foam_fraction=representativeFoam();
        e.beaufort=beaufort_;
    }

    // 批次1: 太阳方向真值 (AtmosphereState 同源) → 方位/俯仰 (EnvAnswer 契约)。
    // 零向量 (星历未就绪) 时保留 EnvAnswer 缺省值 (与旧管线硬编码方向一致)。
    const float sunLen = vsg::length(sunDirEnu_);
    if (sunLen > 1.0e-5f) {
        e.sun_el_deg = std::asin(std::clamp(double(sunDirEnu_.y) / double(sunLen), -1.0, 1.0))
                       * 180.0 / M_PI;
        e.sun_az_deg = std::fmod(std::atan2(double(sunDirEnu_.x), -double(sunDirEnu_.z))
                       * 180.0 / M_PI + 360.0,360.0); // North=0, clockwise toward East.
    }
    return e;
}

} // namespace sensor
} // namespace rockyenv
