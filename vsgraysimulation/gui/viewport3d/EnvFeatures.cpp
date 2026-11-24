/// @file EnvFeatures.cpp —— rocky 环境特性 (sky/cloud/ocean) 薄封装实现。
#include "EnvFeatures.h"

#include "ViewportContext.h"
#include "FeatureRegistry.h"
#include "EarthViewFeature.h"
#include "VoxelSceneFeature.h"
#include "CommonFeatures.h"
#include "ViewportKit.h"
#include "GeoFrame.h"

#include <vsg/maths/transform.h>
#include <vsg/maths/vec4.h>
#include <vsg/app/Viewer.h>
#include <vsg/commands/Command.h>

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace vsgraysim { namespace viewport3d {



void EnvFeatureBase::readCommonParams(const nlohmann::json& j)
{
    sun_el_     = j.value("sun_el", sun_el_);
    sun_az_     = j.value("sun_az", sun_az_);
    wind_speed_ = j.value("wind_speed", wind_speed_);
    wind_az_    = j.value("wind_az", wind_az_);
    cover_      = j.value("cover", cover_);
    cover_      = j.value("cloud_cover",cover_);
    visibility_ = j.value("visibility", visibility_);
    precipitation_=std::clamp(j.value("rain",precipitation_),0.0f,1.0f);

    // 时间基准参数 (运行时推进由 SkyFeature 仿真时钟统一计算)
    year_       = j.value("year", year_);
    month_      = j.value("month", month_);
    day_        = j.value("day", day_);
    hours_utc_  = j.value("hours_utc", hours_utc_);
    time_scale_ = j.value("time_scale", time_scale_);
}

// W2: 细粒度大气参数实时下发。基类只更新共用成员 (readCommonParams);
// 子类 override 追加本模块 Control 接口调用 (云/海/天空)。
void EnvFeatureBase::setAtmosphereParams(const nlohmann::json& j)
{
    readCommonParams(j);
}

rockyenv::EnvFrame EnvFeatureBase::makeFrame(ViewportContext& ctx,
                                             const FrameTick& tick) const
{
    rockyenv::EnvFrame f;
    gui::ViewportKit::cameraMatrices(ctx.camera(), f.view_proj, f.cam_pos_world);
    auto* earth=ctx.featureAs<EarthViewFeature>(EarthView::kId);
    const auto* planet=earth ? earth->weather() : nullptr;
    f.model = planet ? earth->weatherModel() : ctx.contentModel();                       // ENU→ECEF (平面模式=单位阵)
    const vsg::dmat4 inv = vsg::inverse(f.model);
    const vsg::dvec4 ce = inv * vsg::dvec4(f.cam_pos_world, 1.0);
    f.cam_pos_enu = vsg::dvec3(ce.x, ce.y, ce.z);
    f.time_sec = tick.elapsed_sec;

    // 当前 UTC 时刻: 优先取 SkyFeature 的仿真时钟 (基准 + dt*time_scale 推进,
    // 云/海/天空共享同一时刻); 无天空时用 configure 基准值 (平面模式)。
    if (auto* sky = ctx.featureAs<SkyFeature>(SkyFeature::kId)) {
        sky->currentDateTime(tick, f.year, f.month, f.day, f.hours_utc);
        f.time_sec=sky->clockSeconds();
    } else {
        f.year = year_;
        f.month = month_;
        f.day = day_;
        f.hours_utc = hours_utc_;
    }
    f.time_scale = time_scale_;

    // 太阳方向: 优先取 rocky_sky 的星历单一真值源 (与 SkyNode 同一 DateTime/Ephemeris);
    // 查询前先同步本帧时刻, 避免特性 onFrame 顺序导致云/海拿到上一帧太阳。
    // 天空未挂载/无 rocky 时回退到手工 sun_el/sun_az 参数 (平面模式)。
    bool sunFromEphemeris = false;
    if (auto* sky = ctx.featureAs<SkyFeature>(SkyFeature::kId)) {
        if (rockyenv::ISunProvider* sp = sky->sunProvider()) {
            sp->syncDateTime(f.year, f.month, f.day, f.hours_utc);
            rockyenv::EnvSunInfo si;
            if (sp->sunInfo(f.model, si)) {
                f.sun_dir_enu = si.dirENU;
                f.sun_el_deg  = si.elevationDeg;
                sunFromEphemeris = true;
            }
        }
    }
    if (!sunFromEphemeris) {
        // 太阳方向 (方位角北=0 顺时针, ENU: x=东 y=上 z=南=-北)。
        const float elr = static_cast<float>(vsg::radians(sun_el_));
        const float azr = static_cast<float>(vsg::radians(sun_az_));
        const float east = std::cos(elr) * std::sin(azr);
        const float north = std::cos(elr) * std::cos(azr);
        const float up = std::sin(elr);
        f.sun_dir_enu = vsg::normalize(vsg::vec3(east, up, -north));
        f.sun_el_deg = sun_el_;
    }

    // 大气光照真值: 太阳直射色/天光环境色/天顶-地平线天色。云/海 shader 据此
    // 照明与上雾, 保证暮光变红、夜间转暗与天空渲染一致; 无天空时用 EnvFrame 默认值。
    if (auto* sky = ctx.featureAs<SkyFeature>(SkyFeature::kId)) {
        if (rockyenv::ILightingProvider* lp = sky->lightingProvider()) {
            rockyenv::AtmosphereState st;
            if (lp->getAtmosphereState(st)) {
                f.sun_light     = st.sun_light;
                f.is_day        = st.is_day;
                f.ambient_light = st.ambient_light;
                f.horizon_color = st.horizon_color;
                f.zenith_color  = st.zenith_color;
                f.moon_dir_enu = st.moon_dir_enu;
                f.moon_phase = st.moon_phase;
                f.extinction_coeff = st.extinction_coeff;
            }
        }
    }

    // 水平风方向 (x=东, z=南)。
    const float waz = static_cast<float>(vsg::radians(wind_az_));
    f.wind_dir_enu = vsg::normalize(vsg::vec3(std::sin(waz), 0.0f, -std::cos(waz)));
    f.wind_speed = wind_speed_;
    f.cloud_cover = cover_;
    f.visibility = visibility_;
    f.precipitation = precipitation_;
    if (auto* cloud=ctx.featureAs<CloudFeature>(CloudFeature::kId))
        if (auto* query=cloud->cloudQuery()) query->cloudBase(f.cloud_base_m);
    if(planet && vsg::length(f.cam_pos_world)>1.) {
        const auto& w=*planet;
        const auto llh=ecefToGeodetic(w.eye.x,w.eye.y,w.eye.z);
        const auto up=geoUp(llh.x,llh.y);
        const auto tangent=vsg::cross(w.windAxis,up);
        auto wind=inv*vsg::dvec4(tangent,0.);
        const double speed=std::hypot(wind.x,wind.z);
        f.wind_speed=w.windSpeed*speed;
        if(speed>1.e-6) f.wind_dir_enu=vsg::vec3(wind.x/speed,0.,wind.z/speed);
        f.precipitation=w.rain;
        f.cloud_cover=vsgraysim::earth::cloudFraction(w,up);
        f.cloudEnvelope=w.cover>0.f ? f.cloud_cover/w.cover : 0.f;
        f.detailWeight=w.detail;
        f.detailRadius=std::string_view(id())==OceanFeature::kId ? w.seaRadius : w.cloudRadius;
        f.time_sec=w.seconds;
        if(auto cloud=ctx.featureAs<CloudFeature>(CloudFeature::kId)) f.visibility=cloud->visibility();
    }
    if(auto* scene=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);scene&&scene->snapshot()&&scene->snapshot()->weather.enabled) {
        const auto state=scene->snapshot()->weather.sample(f.time_sec);
        f.cloud_cover=state.cover;f.visibility=state.visibility;f.precipitation=state.precipitation();
        f.overcast=state.overcast;f.cloud_base_m=state.cloudBase;
        const double angle=vsg::radians(state.windAzimuth);
        f.wind_speed=state.windSpeed;f.wind_dir_enu={float(std::sin(angle)),0,float(-std::cos(angle))};
    }
    return f;
}

void EnvFeatureBase::attachNode(ViewportContext& ctx, bool to_world)
{
    to_world_root_ = to_world;
    context_ = &ctx;
    if (!root_node_) return;
    vsg::ref_ptr<vsg::Node> node(root_node_);
    if(auto earth=ctx.featureAs<EarthViewFeature>(EarthView::kId)) {
        using Layer=EarthViewFeature::EnvironmentLayer;
        auto layer=to_world ? Layer::Sky : (std::string_view(id())==OceanFeature::kId ? Layer::LocalSea : Layer::LocalCloud);
        mount_=earth->environmentRoot(layer);
    }
    if(!mount_) mount_=to_world?ctx.worldRoot():ctx.contentRoot();
    mount_->addChild(node);
    ctx.requestCompile();
}

void EnvFeatureBase::onDetach(ViewportContext& ctx)
{
    if (ctx.viewer()) ctx.viewer()->deviceWaitIdle();
    if (root_node_) {
        vsg::ref_ptr<vsg::Node> node(root_node_);
        if(mount_) { auto& children=mount_->children; children.erase(std::remove(children.begin(),children.end(),node),children.end()); }
        mount_=nullptr;
    }
    root_node_ = nullptr;
    module_.reset();
    context_=nullptr;
}

void EnvFeatureBase::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    if (module_ && enabled_) module_->onFrame(makeFrame(ctx, tick));
}

void EnvFeatureBase::configure(const nlohmann::json& params)
{
    readCommonParams(params);
}

void EnvFeatureBase::setEnabled(bool on)
{
    enabled_ = on;
    if (module_) module_->setEnabled(on);
}

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------
void SkyFeature::configure(const nlohmann::json& params)
{
    readCommonParams(params);
    // Native voxel Earth owns its ECEF atmosphere. The legacy false default
    // only selected the Rocky compatibility dome and left the native sky black.
    sky_.show_atmosphere = params.value("show_atmosphere", true);
    sky_.use_bruneton_lut=params.value("use_bruneton_lut",true);
    sky_.overcast=params.value("overcast",false);
    sky_.year  = params.value("year", sky_.year);
    sky_.month = params.value("month", sky_.month);
    sky_.day   = params.value("day", sky_.day);
    sky_.hours_utc = params.value("hours_utc", sky_.hours_utc);
    sky_.time_scale = params.value("time_scale", sky_.time_scale);
    // 物理/渲染参数 (Rocky 单后端, 无 backend 选择)。
    sky_.exposure  = params.value("exposure", sky_.exposure);
    sky_.turbidity = params.value("turbidity", sky_.turbidity);
    // [临时-天空标定钩子] 环境变量覆盖: VSGRAYSIM_SKY_TIME="月,日,时(UTC)"。
    if (const char* t = std::getenv("VSGRAYSIM_SKY_TIME")) {
        int cm = 0, cd = 0; double ch = 0.0;
        if (sscanf(t, "%d,%d,%lf", &cm, &cd, &ch) == 3) {
            sky_.month = cm; sky_.day = cd; sky_.hours_utc = ch;
        }
    }
    if (const char* e = std::getenv("VSGRAYSIM_SKY_EXPOSURE")) sky_.exposure = std::atof(e);
    // 新基准时刻: 仿真时钟清零重开。
    sim_seconds_ = 0.0;
    clock_frame_ = 0;
}

// W2: 细粒度大气参数实时下发 —— 天空 (曝光/浊度/天体开关)。
void SkyFeature::setAtmosphereParams(const nlohmann::json& params)
{
    EnvFeatureBase::setAtmosphereParams(params);   // 更新共用成员 (sun_el/sun_az/...)
    sky_.year=year_; sky_.month=month_; sky_.day=day_; sky_.hours_utc=hours_utc_;
    if (params.contains("exposure"))  sky_.exposure  = params["exposure"].get<float>();
    if (params.contains("turbidity")) sky_.turbidity = params["turbidity"].get<float>();
    if (params.contains("show_atmosphere")) sky_.show_atmosphere = params["show_atmosphere"].get<bool>();
    // 实时下发到 Rocky SkyNode (模块就绪时)。
    if (auto* sc = skyControl()) {
        sc->setExposure(sky_.exposure);
        sc->setTurbidity(sky_.turbidity);
        sc->setShowAtmosphere(sky_.show_atmosphere);
    }
}

void SkyFeature::currentDateTime(const FrameTick& tick,
                                 int& year, int& month, int& day, double& hoursUtc)
{
    // Authored weather/UTC must agree on the very first frame after an edit
    // or external publication, independently of feature traversal order.
    if(auto* scene=context_?context_->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId):nullptr;
       scene&&scene->snapshot()&&scene->snapshot()->weather.enabled) {
        const auto& utc=scene->snapshot()->utcOrigin;
        sky_.year=utc.at("year");sky_.month=utc.at("month");sky_.day=utc.at("day");sky_.hours_utc=utc.at("hours_utc");
        time_scale_=utc.value("time_scale",1.);sky_.time_scale=time_scale_;
    }
    // 同帧幂等: 云/海/天空每帧都会来问一次, 时钟只在首问时按 dt*time_scale 推进。
    if (tick.simulation_clock) {
        sim_seconds_ = tick.elapsed_sec;
        clock_frame_ = tick.frame_index;
    } else if (tick.frame_index != clock_frame_) {
        // A paused authoring world must reopen with the same sun/moon UTC.
        // Never advance its lighting clock by asset-loading wall time.
        auto* voxel=context_?context_->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId):nullptr;
        if(voxel&&voxel->snapshot())sim_seconds_=voxel->snapshot()->timeSeconds();
        else sim_seconds_ += tick.dt_sec * time_scale_;
        clock_frame_ = tick.frame_index;
    }
    // 基准 UTC + 累计仿真秒 → 归一化公历日期 (跨月/跨年/负向均正确)。
    const auto* voxel=context_?context_->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId):nullptr;
    const bool authoritative=tick.simulation_clock||(voxel&&voxel->snapshot());
    const auto utc=earth::sceneUtc({{"year",sky_.year},{"month",sky_.month},{"day",sky_.day},
        {"hours_utc",sky_.hours_utc},{"time_scale",authoritative?time_scale_:1.}},sim_seconds_);
    year=utc.year;month=utc.month;day=utc.day;hoursUtc=utc.hours;
}

void SkyFeature::onAttach(ViewportContext& ctx)
{
    // The Earth backend may change during playback. Keep one map-independent
    // ECEF atmosphere and shared illumination clock across those transitions.
    sky_.voxel_earth=true;
    module_ = rockyenv::createSkyModule(sky_);
    if (module_) {
        root_node_ = module_->rootNode();
        const bool to_world = module_->mountInWorldRoot();
        attachNode(ctx, to_world);
        spdlog::info("SkyFeature: VSG 天空已挂载 (ECEF 世界根, atmosphere={})",
                     sky_.show_atmosphere);
    }
    // 独占场景灯光: 默认灯光组若已先挂载 (配置顺序相反时), 摘除之, 避免
    // 双太阳/双环境光叠加 (暮光不红/夜不黑/白天过曝)。
    if (auto* lf = ctx.featureAs<LightsFeature>(LightsFeature::kId)) {
        lf->surrenderToSky(bool(module_));
    }
}

void SkyFeature::onDetach(ViewportContext& ctx)
{
    if (auto* lf = ctx.featureAs<LightsFeature>(LightsFeature::kId)) {
        lf->surrenderToSky(false);  // 归还默认灯光
    }
    EnvFeatureBase::onDetach(ctx);
}

rockyenv::ISunProvider* SkyFeature::sunProvider()
{
    return module_ ? dynamic_cast<rockyenv::ISunProvider*>(module_.get()) : nullptr;
}

rockyenv::ILightingProvider* SkyFeature::lightingProvider()
{
    return module_ ? dynamic_cast<rockyenv::ILightingProvider*>(module_.get()) : nullptr;
}

rockyenv::IAtmosphereQuery* SkyFeature::atmosphereQuery()
{
    return module_ ? dynamic_cast<rockyenv::IAtmosphereQuery*>(module_.get()) : nullptr;
}

rockyenv::ISkyControl* SkyFeature::skyControl()
{
    return module_ ? dynamic_cast<rockyenv::ISkyControl*>(module_.get()) : nullptr;
}

// ---------------------------------------------------------------------------
// Cloud
// ---------------------------------------------------------------------------
void CloudFeature::configure(const nlohmann::json& params)
{
    readCommonParams(params);
    cloud_.stratus_altitude = params.value("stratus_altitude", cloud_.stratus_altitude);
    cloud_.stratus_size     = params.value("stratus_size", cloud_.stratus_size);
    cloud_.cumulus_altitude = params.value("cumulus_altitude", cloud_.cumulus_altitude);
    cloud_.cumulus_radius   = params.value("cumulus_radius", cloud_.cumulus_radius);
    cloud_.cumulus_count    = params.value("cumulus_count", cloud_.cumulus_count);
    cloud_.cover            = cover_;
    cloud_.volume=params.value("volume",true);
}

// W2: 细粒度大气参数实时下发 —— 云 (云量/层参数)。
void CloudFeature::setAtmosphereParams(const nlohmann::json& params)
{
    EnvFeatureBase::setAtmosphereParams(params);   // 更新共用成员 (cover/wind/...)
    const float sheet=params.value("stratus_altitude",params.value("cloud_base",cloud_.stratus_altitude));
    const float cumulus=params.value("cumulus_altitude",params.value("cloud_base",cloud_.cumulus_altitude));
    const bool changed=sheet!=cloud_.stratus_altitude || cumulus!=cloud_.cumulus_altitude;
    cloud_.stratus_altitude=sheet; cloud_.cumulus_altitude=cumulus;
    cloud_.cover = cover_;
    // 实时下发到云模块 (ICloudControl 就绪时)。
    if (auto* cc = cloudControl()) {
        cc->setCoverage(cloud_.cover);
        if (changed) {
            auto layers=cc->getLayers();
            for (auto& layer:layers) layer.altitude=layer.type==rockyenv::CloudLayerType::Cumulus ||
                layer.type==rockyenv::CloudLayerType::Cumulonimbus ? cumulus : sheet;
            if (context_ && context_->viewer()) context_->viewer()->deviceWaitIdle();
            cc->setLayers(layers);
            if (context_) context_->requestCompile();
        }
    }
}

void CloudFeature::onAttach(ViewportContext& ctx)
{
    module_ = rockyenv::createCloudModule(cloud_);
    if (module_) {
        // The native Earth pass ray-marches the complete ECEF voxel volume at
        // every altitude. Do not draw the old local sheet/sprite approximation
        // over it; retain rocky_env controls and queries for both backends.
        auto* earth=ctx.featureAs<EarthViewFeature>(EarthView::kId);
        local_rendered_=enabled_ && !(earth && earth->weather());
        module_->setEnabled(local_rendered_);
        root_node_ = module_->rootNode();
        attachNode(ctx, /*to_world=*/false);  // 云层锚定在作业区 ENU 上空
        spdlog::info("CloudFeature: 云层已挂载 (cover={:.2f}, 积云 {} 块)",
                     cover_, cloud_.cumulus_count);
    }
}

void CloudFeature::onFrame(ViewportContext& ctx,const FrameTick& tick)
{
    auto* earth=ctx.featureAs<EarthViewFeature>(EarthView::kId);
    const bool wantLocal=enabled_ && !(earth && earth->weather());
    if(module_ && wantLocal!=local_rendered_) {
        module_->setEnabled(wantLocal);
        local_rendered_=wantLocal;
        if(wantLocal)ctx.requestCompile();
    }
    EnvFeatureBase::onFrame(ctx,tick);
}

rockyenv::ICloudControl* CloudFeature::cloudControl()
{
    return module_ ? dynamic_cast<rockyenv::ICloudControl*>(module_.get()) : nullptr;
}

rockyenv::ICloudQuery* CloudFeature::cloudQuery()
{
    return module_ ? dynamic_cast<rockyenv::ICloudQuery*>(module_.get()) : nullptr;
}

// ---------------------------------------------------------------------------
// Ocean
// ---------------------------------------------------------------------------
void OceanFeature::configure(const nlohmann::json& params)
{
    const auto previous = ocean_;
    const float patch = params.value("fft_patch_size", ocean_.fft_patch_size);
    if (!std::isfinite(patch) || patch <= 0)
        throw std::invalid_argument("ocean.fft_patch_size must be finite and positive metres");
    readCommonParams(params);
    ocean_.sea_level   = params.value("sea_level", ocean_.sea_level);
    ocean_.size        = params.value("ocean_size", params.value("size", ocean_.size));
    if (params.contains("extent") && params.at("extent").get<float>() > 0)
        ocean_.size = params.at("extent").get<float>();
    ocean_.grid        = params.value("grid", ocean_.grid);
    ocean_.wave_height = params.value("wave_height", ocean_.wave_height);
    ocean_.wave_length = params.value("wave_length", ocean_.wave_length);
    ocean_.wind_speed  = wind_speed_;
    ocean_.wave_mode=params.value("wave_mode",2);
    ocean_.reflection=params.value("reflection",2);
    ocean_.curvature=params.value("curvature",true);
    ocean_.fft_patch_size = patch;

    // 可配海洋边界 (锚点 ENU, 米)。不配置时保持默认 (近似无界跟随)。
    auto& b = ocean_.bounds;
    if (params.contains("ocean_bounds_shape")) {
        const auto& s = params["ocean_bounds_shape"];
        if (s.is_string())
            b.shape = (s.get<std::string>() == "rect" ||
                       s.get<std::string>() == "rectangle")
                      ? rockyenv::OceanBounds::Rectangle
                      : rockyenv::OceanBounds::Disc;
        else
            b.shape = s.get<int>();
    }
    b.center_x   = params.value("ocean_center_x", b.center_x);
    b.center_z   = params.value("ocean_center_z", b.center_z);
    b.radius     = params.value("ocean_radius", b.radius);
    b.half_x     = params.value("ocean_half_x", b.half_x);
    b.half_z     = params.value("ocean_half_z", b.half_z);
    b.fade_width = params.value("ocean_fade", b.fade_width);
    if (params.contains("enabled")) setEnabled(params.at("enabled").get<bool>());
    if (module_ && ctx_ && (previous.fft_patch_size != ocean_.fft_patch_size ||
        previous.size != ocean_.size || previous.grid != ocean_.grid ||
        previous.wave_mode != ocean_.wave_mode)) {
        auto* context = ctx_;
        onDetach(*context);
        onAttach(*context);
        module_->setEnabled(enabled_);
    }
    if (auto* control = oceanControl()) {
        control->setBounds(ocean_.bounds);
        control->setReflection(ocean_.reflection);
        control->setCurvature(ocean_.curvature);
        if (params.contains("wave_length")) control->setWaveLength(ocean_.wave_length);
    }
}

// W2: 细粒度大气参数实时下发 —— 海洋 (风速/波高/涌浪)。
void OceanFeature::setAtmosphereParams(const nlohmann::json& params)
{
    bool configuration = false;
    for (const char* key : {"enabled", "fft_patch_size", "ocean_size", "size", "extent",
                            "grid", "wave_mode", "reflection", "curvature"})
        configuration = configuration || params.contains(key);
    if (configuration) {
        auto settings = params;
        if (!settings.contains("wave_mode")) settings["wave_mode"] = ocean_.wave_mode;
        if (!settings.contains("reflection")) settings["reflection"] = ocean_.reflection;
        if (!settings.contains("curvature")) settings["curvature"] = ocean_.curvature;
        configure(settings);
    }
    EnvFeatureBase::setAtmosphereParams(params);   // 更新共用成员 (wind_speed/wind_az/...)
    if (params.contains("wave_height")) ocean_.wave_height = params["wave_height"].get<float>();
    if (params.contains("sea_level"))   ocean_.sea_level   = params["sea_level"].get<float>();
    ocean_.wind_speed = wind_speed_;
    // 实时下发到海洋模块 (IOceanControl 就绪时)。
    if (auto* oc = oceanControl()) {
        // 风向 ENU 单位向量 (az: 北=0 顺时针 → ENU x=东 y=上 z=南=-北)。
        const float azr = static_cast<float>(vsg::radians(wind_az_));
        const vsg::vec3 wdir(std::sin(azr), 0.0f, -std::cos(azr));
        oc->setWind(wind_speed_, wdir);
        if (params.contains("wave_height"))
            oc->setWaveHeight(ocean_.wave_height);
        if (params.contains("sea_level"))
            oc->setSeaLevel(ocean_.sea_level);
    }
}

void OceanFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    ocean_.terrain = this;  // Phase 2: 注入地形桥
    module_ = rockyenv::createOceanModule(ocean_);
    if (module_) {
        module_->setWindow(ctx.window());
        module_->setEnabled(enabled_);
        if (auto* command=module_->fftCommands()) {
            compute_switch_=vsg::Switch::create();
            compute_switch_->addChild(enabled_,vsg::ref_ptr<vsg::Node>(command));
            if (ctx.preRenderRoot()) ctx.preRenderRoot()->addChild(compute_switch_);
        }
        root_node_ = module_->rootNode();
        attachNode(ctx, /*to_world=*/false);  // 海面为锚点 ENU 海平面
        spdlog::info("OceanFeature: 海面已挂载 (size={}m, grid={}, 波高 {}m, 海岸线掩膜=ON)",
                     ocean_.size, ocean_.grid, ocean_.wave_height);
    }
}

void OceanFeature::onFrame(ViewportContext& ctx,const FrameTick& tick) {
    if (!module_ || !enabled_) return;
    auto frame=makeFrame(ctx,tick);
    const bool localVisible=frame.detailWeight>0.f;
    // The FFT images are initialized by the pre-render compute pass only
    // while the near ocean is active. Do not leave its render node enabled
    // at orbital heights after disabling that compute pass.
    if(compute_switch_) compute_switch_->setAllChildren(localVisible);
    module_->setEnabled(localVisible);
    module_->onFrame(frame);
}

void OceanFeature::setEnabled(bool on) {
    EnvFeatureBase::setEnabled(on);
    if (compute_switch_) compute_switch_->setAllChildren(on);
}
void OceanFeature::onDetach(ViewportContext& ctx) {
    if (ctx.viewer()) ctx.viewer()->deviceWaitIdle();
    if (auto* root=ctx.preRenderRoot()) {
        auto& nodes=root->children;
        nodes.erase(std::remove(nodes.begin(),nodes.end(),compute_switch_),nodes.end());
    }
    compute_switch_=nullptr;
    EnvFeatureBase::onDetach(ctx); ctx_=nullptr;
}

rockyenv::IOceanControl* OceanFeature::oceanControl()
{
    return module_ ? dynamic_cast<rockyenv::IOceanControl*>(module_.get()) : nullptr;
}

rockyenv::IOceanSampler* OceanFeature::oceanSampler()
{
    return module_ ? dynamic_cast<rockyenv::IOceanSampler*>(module_.get()) : nullptr;
}

// ITerrainBridge 实现
float OceanFeature::heightENU(double ex, double ez) const
{
    if (!ctx_) return std::numeric_limits<float>::quiet_NaN();

    if (auto* geo=ctx_->geoQuery(); geo && geo->environmentTerrain())
        return geo->environmentTerrain()->heightENU(ex,ez);
    const vsg::dmat4 model = ctx_->contentModel();
    // ENU 查询点 (y=0 平面) → ECEF。
    const vsg::dvec4 p0 = model * vsg::dvec4(ex, 0.0, ez, 1.0);

    // ---- rocky 地球模式: 真实地形高程 ----
    if (auto* geo = ctx_->geoQuery(); geo && geo->geoActive()) {
        double localHeight=0;
        if(geo->heightLocal(ex,ez,localHeight))return float(localHeight);
        if(geo->hasAuthoritativeSurface())return std::numeric_limits<float>::quiet_NaN();
        // 旧实现用 (0,-1,0) 射线 intersectRayGeo: ECEF 系下该方向不是径向铅垂,
        // 且 intersectRayGeo 只与光滑椭球求交 (不含地形高程层), 山体岛屿全部
        // 漏判。改为: 取查询点经纬度 → groundElevation 查真实地形椭球高 →
        // 地形点 ECEF 反变换回 ENU, 返回 ENU y (与 sea_level 同基准)。
        const vsg::dvec3 llh = ecefToGeodetic(p0.x, p0.y, p0.z);
        double terrH = 0.0;
        if (geo->groundElevation(llh.x, llh.y, terrH)) {
            const vsg::dvec3 tEcef = geodeticToECEF(llh.x, llh.y, terrH);
            const vsg::dvec4 en = vsg::inverse(model) *
                                  vsg::dvec4(tEcef.x, tEcef.y, tEcef.z, 1.0);
            return static_cast<float>(en.y);
        }
        // Missing terrain remains unknown. The module masks it and retries at
        // a bounded cadence; arrival of authoritative data changes revision().
        return std::numeric_limits<float>::quiet_NaN();
    }

    // ---- 平面模式: 局部高度场 (model=单位阵, 世界 xz 即 ENU xz) ----
    if (auto* tq = ctx_->terrainQuery()) {
        return static_cast<float>(tq->heightAtWorld(p0.x, p0.z));
    }

    return std::numeric_limits<float>::quiet_NaN();
}

vsg::vec3 OceanFeature::normalENU(double ex, double ez) const
{
    if (auto* geo=ctx_ ? ctx_->geoQuery() : nullptr; geo && geo->environmentTerrain())
        return geo->environmentTerrain()->normalENU(ex,ez);
    // 通过差分计算法线
    const float delta = 1.0f;
    const float hL = heightENU(ex - delta, ez);
    const float hR = heightENU(ex + delta, ez);
    const float hD = heightENU(ex, ez - delta);
    const float hU = heightENU(ex, ez + delta);
    
    if(!std::isfinite(hL)||!std::isfinite(hR)||!std::isfinite(hD)||!std::isfinite(hU))
        return {0,1,0};
    return vsg::normalize(vsg::vec3(hL - hR, 2.0f * delta, hD - hU));
}

float OceanFeature::waterBedENU(double ex,double ez,float seaLevel) const {
    if(ctx_) if(auto* earth=ctx_->featureAs<EarthViewFeature>(EarthView::kId); earth && earth->weather())
        return earth->weatherBed(ex,ez,seaLevel);
    if (auto* geo=ctx_ ? ctx_->geoQuery() : nullptr; geo && geo->environmentTerrain())
        return geo->environmentTerrain()->waterBedENU(ex,ez,seaLevel);
    if(ctx_ && ctx_->geoQuery() && ctx_->geoQuery()->hasAuthoritativeSurface()) {
        double bed;
        if(ctx_->geoQuery()->waterBedLocal(ex,ez,seaLevel,bed)) return float(bed);
        return std::numeric_limits<float>::quiet_NaN();
    }
    return heightENU(ex,ez);
}

bool OceanFeature::isLand(double ex, double ez, float seaLevel) const
{
    return waterBedENU(ex,ez,seaLevel) > seaLevel;
}

float OceanFeature::waterDepth(double ex, double ez, float seaLevel) const
{
    const float terrainH = waterBedENU(ex,ez,seaLevel);
    return std::isfinite(terrainH) ? std::max(0.0f, seaLevel - terrainH)
        : std::numeric_limits<float>::quiet_NaN();
}

uint64_t OceanFeature::revision() const {
    if(ctx_) if(auto* earth=ctx_->featureAs<EarthViewFeature>(EarthView::kId); earth && earth->weather())
        return earth->weatherTerrainRevision();
    return ctx_ && ctx_->geoQuery() ? ctx_->geoQuery()->revision() : 0;
}

VSGVP_REGISTER_FEATURE(SkyFeature);
VSGVP_REGISTER_FEATURE(CloudFeature);
VSGVP_REGISTER_FEATURE(OceanFeature);

}} // namespace vsgraysim::viewport3d
