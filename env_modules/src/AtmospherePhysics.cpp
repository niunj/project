/// @file AtmospherePhysics.cpp —— 大气物理光照计算 (两后端共用)。
/// P1-1: 支持 EnvWeather 联动 (能见度/云量/风速 → 大气参数)
/// P1-2: 支持能见度/雾联动 (visibility → 消光系数)
#include "AtmospherePhysics.h"
#include "AtmosphereLUT.h"
#include <rocky_env/SensorApi.h>
#include <simcore/GeoFrame.h>
#include <algorithm>
#include <stdexcept>

#include <vsg/maths/vec3.h>

#include <cmath>

namespace rockyenv {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float sstep(float e0, float e1, float x) {
    float t = std::max(0.0f, std::min(1.0f, (x - e0) / (e1 - e0)));
    return t * t * (3.0f - 2.0f * t);
}

/// Kasten-Young 大气质量 (天顶=1, 地平线≈38)。
float airMass(float elDeg) {
    if (elDeg <= 0.5f) return 40.0f;
    const float h = elDeg + 6.0799f;
    return 1.0f / (std::sin(elDeg * kPi / 180.0f) + 0.50572f * std::pow(h, -1.6364f));
}

/// P1-2: 能见度 → 消光系数 (Beer-Lambert)
/// 能见度 (米) → 消光系数 (每公里)
/// 公式: visibility = -ln(0.02) / extinction → extinction = 3.912 / visibility (km)
float visibilityToExtinction(float visibilityM) {
    const float visKm = std::max(visibilityM / 1000.0f, 0.01f);
    return 3.912f / visKm;  // 每公里消光系数
}

/// P1-1: 云量 → 浊度调整
/// 云量越高, 大气越浑浊 (Mie 散射增加)
float cloudCoverToTurbidity(float baseTurbidity, float cloudCover) {
    // 云量 0→1 对应浊度 1.0→2.5
    return baseTurbidity * (1.0f + cloudCover * 1.5f);
}

/// P1-1: 风速 → 气溶胶调整
/// 高风速卷起更多气溶胶, 增加 Mie 散射
float windSpeedToAerosol(float windSpeed) {
    // 0 m/s → 1.0, 20 m/s → 1.5
    return 1.0f + std::min(windSpeed / 40.0f, 0.5f);
}

}  // namespace

AtmosphereState computeAtmosphere(const SkyBodies& b, float turbidity, float exposure,
                                  const WeatherParams& weather)
{
    AtmosphereState s;
    s.exposure = exposure;
    s.turbidity = turbidity;

    // ---- P1-1: 天气参数调整大气 ----
    // 能见度 → 消光系数 (用于雾/距离衰减)
    const float extinction = visibilityToExtinction(weather.visibility);
    s.extinction_coeff = extinction;  // 新增: 消光系数供着色器使用
    // 能见度透传: AtmosphereState.visibility 是 bandSky 气溶胶 σ 与灰天谱形
    // 权值 κ 的单一真值源 (此前从未赋值, bandSky 恒用默认 10km)。0/负值视为
    // 未提供, 保留默认晴空 10km (与 EnvPhysicsService 的 >0 门控同语义)。
    s.visibility = (weather.visibility > 0.0f) ? weather.visibility : 10000.0f;
    
    // 云量 → 浊度调整
    const float adjustedTurbidity = cloudCoverToTurbidity(turbidity, weather.cloud_cover);
    s.turbidity = adjustedTurbidity;
    
    // 风速 → 气溶胶调整
    const float aerosolFactor = windSpeedToAerosol(weather.wind_speed);

    // ---- 太阳直射辐照 (RGB 光谱消光) ----
    // Rayleigh 天顶光学厚度 (Bruneton 系数 × 标高 8500m), 蓝>红;
    // Mie 天顶光学厚度 (灰色, 与浊度成正比)。
    const vsg::vec3 odRayleigh(0.0493f, 0.1152f, 0.2814f);
    const float     odMie = 0.020f * std::max(0.2f, adjustedTurbidity) * aerosolFactor;
    const float am = airMass(b.sun.elDeg);

    // 昼因子: 太阳 -2°..6° 平滑; 暮光仍有光, 夜≈0。
    const float day = sstep(-2.0f, 6.0f, b.sun.elDeg);
    const float night = sstep(6.0f, -4.0f, b.sun.elDeg);

    // P1-2: 能见度消光 (大气透过率)
    const float visExtinction = std::exp(-extinction * 0.1f);  // 100m 基准距离
    
    vsg::vec3 T(std::exp(-am * (odRayleigh.x + odMie)) * visExtinction,
                std::exp(-am * (odRayleigh.y + odMie)) * visExtinction,
                std::exp(-am * (odRayleigh.z + odMie)) * visExtinction);
    // 晴午归一化到 ≈1, 暮光随消光自然变红变暗。
    const float kSun = 1.15f;
    s.sun_light = vsg::vec3(T.x * kSun * day, T.y * kSun * day, T.z * kSun * day);
    s.sun_dir_enu = b.sun.dirENU;
    s.sun_el_deg = b.sun.elDeg;
    s.sun_az_deg = b.sun.azDeg;
    s.is_day = day;

    // ---- 天空梯度色 (供云/海雾/反射; 也是环境光基准) ----
    // 暖色权重: 太阳越低地平线越红 (-2° 满暖, 12° 以上纯蓝)。
    const float warmT = 1.0f - sstep(-2.0f, 12.0f, b.sun.elDeg);
    const vsg::vec3 dayZenith(0.20f, 0.42f, 0.72f);
    const vsg::vec3 dayHorizon(0.72f, 0.78f, 0.84f);
    const vsg::vec3 duskZenith(0.10f, 0.12f, 0.28f);
    const vsg::vec3 duskHorizon(0.95f, 0.45f, 0.25f);
    const vsg::vec3 nightSky(0.010f, 0.015f, 0.030f);

    const float skyBright = sstep(-8.0f, 8.0f, b.sun.elDeg);  // 天光昼亮夜灭
    vsg::vec3 zenith = vsg::vec3(dayZenith.x + (duskZenith.x - dayZenith.x) * warmT,
                                 dayZenith.y + (duskZenith.y - dayZenith.y) * warmT,
                                 dayZenith.z + (duskZenith.z - dayZenith.z) * warmT);
    vsg::vec3 horizon = vsg::vec3(dayHorizon.x + (duskHorizon.x - dayHorizon.x) * warmT,
                                  dayHorizon.y + (duskHorizon.y - dayHorizon.y) * warmT,
                                  dayHorizon.z + (duskHorizon.z - dayHorizon.z) * warmT);
    
    // P1-2: 能见度影响天顶/地平线颜色 (低能见度时天空变灰)
    const float visFactor = std::clamp(weather.visibility / 10000.0f, 0.0f, 1.0f);
    const vsg::vec3 fogColor(0.7f, 0.75f, 0.8f);  // 雾色 (灰色)
    zenith = vsg::vec3(
        zenith.x * visFactor + fogColor.x * (1.0f - visFactor),
        zenith.y * visFactor + fogColor.y * (1.0f - visFactor),
        zenith.z * visFactor + fogColor.z * (1.0f - visFactor)
    );
    horizon = vsg::vec3(
        horizon.x * visFactor + fogColor.x * (1.0f - visFactor),
        horizon.y * visFactor + fogColor.y * (1.0f - visFactor),
        horizon.z * visFactor + fogColor.z * (1.0f - visFactor)
    );
    
    zenith = vsg::vec3(zenith.x * (0.3f + 0.7f * skyBright),
                       zenith.y * (0.3f + 0.7f * skyBright),
                       zenith.z * (0.3f + 0.7f * skyBright));
    horizon = vsg::vec3(horizon.x * skyBright, horizon.y * skyBright, horizon.z * skyBright);
    // 夜间压到极暗蓝 (星光)。
    s.zenith_color = vsg::vec3(zenith.x + nightSky.x * night,
                               zenith.y + nightSky.y * night,
                               zenith.z + nightSky.z * night);
    s.horizon_color = vsg::vec3(horizon.x + nightSky.x * night,
                                horizon.y + nightSky.y * night,
                                horizon.z + nightSky.z * night);

    // ---- 环境光: 天光积分近似 (白天); 夜间微量星光 + 月光环境 ----
    const float moonUp = sstep(-2.0f, 6.0f, b.moon.elDeg);
    const float moonBright = 2.5e-6f * b.moonPhase * b.moonPhase * moonUp; // full moon/daylight ratio
    s.moon_dir_enu = b.moon.dirENU;
    s.moon_el_deg = b.moon.elDeg;
    s.moon_phase = b.moonPhase;
    const vsg::vec3 moonCool(0.78f, 0.83f, 1.0f);
    s.moon_light = vsg::vec3(moonCool.x * moonBright * night,
                             moonCool.y * moonBright * night,
                             moonCool.z * moonBright * night);

    const vsg::vec3 ambDay(horizon.x * 0.35f + zenith.x * 0.15f,
                           horizon.y * 0.35f + zenith.y * 0.15f,
                           horizon.z * 0.35f + zenith.z * 0.15f);
    const float ambNight = 1e-5f + moonBright * 0.15f;
    s.ambient_light = vsg::vec3(ambDay.x + moonCool.x * ambNight * night,
                                ambDay.y + moonCool.y * ambNight * night,
                                ambDay.z + moonCool.z * ambNight * night);
    return s;
}

bool sampleAtmospherePath(const AtmosphereState& state, const vsg::dvec3& sunECEF,
                          const vsg::dvec3& fromECEF, const vsg::dvec3& toECEF,
                          const AtmosphereLUT* lut, vsg::vec3& tau, vsg::vec3& pathRadiance) {
    const auto delta=toECEF-fromECEF;
    const double distance=vsg::length(delta), sunLength=vsg::length(sunECEF);
    if(!std::isfinite(distance) || !std::isfinite(sunLength) || sunLength<.5)return false;
    tau={1,1,1}; pathRadiance={0,0,0};
    if(distance<1e-8)return true;
    const auto sun=sunECEF/sunLength;
    const vsg::vec3 betaR(5.802e-6f,13.558e-6f,33.1e-6f);
    constexpr float betaM=4.444e-6f;
    const int count=std::clamp(int(std::ceil(std::min(distance/2000.,128.))),8,128);
    vsg::dvec3 opticalDepth; double meanHeight=0;
    try {
        for(int i=0;i<count;++i) {
            const auto point=fromECEF+delta*((i+.5)/count);
            const auto geo=simcore::geo::ecefToGeodetic(point.x,point.y,point.z);
            // A path through the solid ellipsoid is occluded, not clear air.
            if(geo.z < -1.) {tau={0,0,0};return true;}
            const double height=std::max(geo.z,0.);meanHeight+=height/count;
            const double rhoR=std::exp(-height/8500.),rhoM=std::exp(-height/1200.);
            for(int channel=0;channel<3;++channel)
                opticalDepth[channel]+=(betaR[channel]*rhoR+betaM*rhoM)*distance/count;
        }
        for(int channel=0;channel<3;++channel)tau[channel]=float(std::exp(-opticalDepth[channel]));
        const auto middle=fromECEF+delta*.5;
        const auto geo=simcore::geo::ecefToGeodetic(middle.x,middle.y,middle.z);
        const auto up=simcore::geo::geoUp(geo.x,geo.y);
        const float cosine=float(std::clamp(vsg::dot(sun,up),-1.,1.));
        vsg::vec3 sunTransmittance;
        if(lut) sunTransmittance=lut->sampleTransmittanceCPU(float(meanHeight),cosine);
        else {
            const double airMass=cosine>.027 ? 1./cosine : 38.;
            for(int channel=0;channel<3;++channel)
                sunTransmittance[channel]=float(std::exp(-airMass*(betaR[channel]*8500.*std::exp(-meanHeight/8500.)+betaM*1200.*std::exp(-meanHeight/1200.))));
        }
        const float phaseCos=float(std::clamp(vsg::dot(delta/distance,sun),-1.,1.));
        const float phaseR=3.f/(16.f*kPi)*(1+phaseCos*phaseCos);
        constexpr float g=.76f,g2=g*g;
        const float phaseM=3.f*(1-g2)*(1+phaseCos*phaseCos)/((8*kPi)*(2+g2)*std::pow(1+g2-2*g*phaseCos,1.5f));
        pathRadiance=sunTransmittance*(phaseR+phaseM)*(vsg::vec3(1,1,1)-tau)*state.sun_light;
        return true;
    } catch(const std::invalid_argument&) {return false;}
}

// Shared spectral query used by both the rendering facade and headless execution.
bool sampleBandSky(const AtmosphereState& state, double lambda_min_m, double lambda_max_m,
                   double view_el_deg, double& L_sky, double& sigma, double& esun) {
        if (!std::isfinite(lambda_min_m) || !std::isfinite(lambda_max_m) ||
            lambda_min_m <= 0 || lambda_max_m <= lambda_min_m || !std::isfinite(view_el_deg)) return false;
        namespace ns = rockyenv::sensor;
        const ns::BandSpec band = ns::bandSpecFromLambda(lambda_min_m, lambda_max_m);
        const ns::EnvAnswer base = ns::clearSkyEnv(band, false);   // 昼间晴空基线
        // 气团: 1/sin(el); 仰角 <0 按地平封顶 (sin→0.03 → 33, clamp 至 10)
        auto airmass = [](double el_deg) {
            const double s = std::sin(el_deg * kPi / 180.0);
            return std::clamp(1.0 / std::max(s, 0.03), 1.0, 10.0);
        };
        const double am = airmass(view_el_deg);
        const double isDay = std::clamp(double(state.is_day), 0.0, 1.0);
        // σ: 分子基线 + 气溶胶 (λc, 能见度与 AtmosphereState 渲染侧同源)
        const double lcUm = 0.5 * (lambda_min_m + lambda_max_m) * 1e6;
        sigma = base.sigma_per_km + ns::aerosolSigma(double(state.visibility), lcUm);
        L_sky = 0.0;
        bool esunDone = false;   // 第九轮逐谱路径自带 esun (跳过尾部平坦式)
        switch (band.type) {
            case ns::BandType::TV:
            case ns::BandType::SWIR: {
                // 太阳散射昼门控 (公式与 EnvPhysicsService 昼因子一致, 天顶=基线)
                // SWIR 夜门控 1e-4 (9.6-② 修复配套): 白天基线改太阳散射标定
                // 0.25 (旧热发射式≈0) 后, 夜门控按旧有效值回标 ——
                // 0.25×1e-4 = 旧 5e-4×0.05, 夜间天空行为逐位不变。
                const double gate = (band.type == ns::BandType::TV)
                                  ? (0.02 + 0.98 * isDay)
                                  : (1.0e-4 + (1.0 - 1.0e-4) * isDay);
                L_sky = base.sky_radiance * gate * (0.5 + 0.5 * am);
                break;
            }
            case ns::BandType::MWIR:
            case ns::BandType::LWIR: {
                // 大气柱热发射: 有效柱温取基线构造值, 天顶发射率由晴空基线反解
                const double tAtm = (band.type == ns::BandType::MWIR) ? 250.0 : 263.0;
                // ---- 第九轮 IR 逐谱 LUT 路径 (默认; VSIM_IR_SPECTRAL=0 →
                //      旧平坦标量逐位回归) ----
                //   L_sky(am) = (1/π)·Σ emitW_i·(1-exp(-(τw+τa·G_i)·am))
                //   基线自洽: τw 由天顶带平均发射率 eps0 反解 (spectralTauW 二分,
                //   天顶 L_sky = eps0·M_S/π = 晴空基线) —— 地平 (am↑) 吸收带
                //   (MWIR CO2 4.3μm / LWIR O3 9.6μm+水汽翼) 先饱和趋柱温黑体,
                //   窗口缓升 → 天顶-地平梯度结构真实 (旧式带平均灰体无此结构)。
                //   esun 逐谱: Σ sunW_i·exp(-τa·G_i·am_sun) —— 太阳斜程在吸收
                //   带内被选择性吸收 (τw 窗口部分仍由 σ 标量承担, 与旧式一致)。
                if (ns::irSpectralOn()) {
                    const ns::SpectralSkyLut& sc = ns::spectralSkyLut(band.type, tAtm);
                    if (sc.ready && sc.emitS > 0.0) {
                        const double eps0 = std::clamp(base.sky_radiance * kPi / sc.emitS,
                                                       0.0, 0.999);
                        const double tauW = ns::spectralTauW(band.type, eps0);
                        for (int i = 0; i <= 128; ++i)
                            L_sky += sc.emitW[i] *
                                     (1.0 - std::exp(-(tauW + sc.tauA * sc.shape[i]) * am));
                        L_sky /= kPi;
                        const double amSun = airmass(double(state.sun_el_deg));
                        double sInt = 0.0;
                        for (int i = 0; i <= 128; ++i)
                            sInt += sc.sunW[i] * std::exp(-sc.tauA * sc.shape[i] * amSun);
                        esun = sInt * 2.164e-5 * std::exp(-sigma * amSun);
                        esunDone = true;
                    }
                }
                if (!esunDone) {
                    const double mBand = ns::bandExitance(lambda_min_m, lambda_max_m, tAtm);
                    if (mBand > 0.0) {
                        const double eps0 = std::clamp(base.sky_radiance * kPi / mBand, 0.0, 0.999);
                        const double tau0 = -std::log(std::max(1.0 - eps0, 1.0e-6));
                        L_sky = (1.0 - std::exp(-tau0 * am)) * mBand / kPi;
                    }
                }
                break;
            }
        }
        // 太阳直射: TOA 波段积分 × 斜程透过 (海平面 σ × 气团; 夜间仰角<0 → 封顶 10)
        // (R_sun/AU)² = (6.96e8/1.496e11)² ≈ 2.164e-5, 全谱 TOA ≈ 1361 W/m² 量级
        // (第九轮: MWIR/LWIR 逐谱路径已在 case 内计算, 跳过此处平坦式)
        if (!esunDone) {
            constexpr double rSunAu2 = 2.164e-5;
            esun = ns::bandExitance(lambda_min_m, lambda_max_m, 5778.0) * rSunAu2 *
                   std::exp(-sigma * airmass(double(state.sun_el_deg)));
        }
        return true;
    
}

}  // namespace rockyenv
