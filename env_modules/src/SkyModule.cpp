/// Shared atmospheric physics, ECEF sky dome and backend-specific light nodes.
#include <rocky_env/EnvApi.h>
#include <rocky_env/SensorApi.h>   // 批次2 bandSky: clearSkyEnv 基线/普朗克积分/气溶胶 (单一真值)

#include "SkyTypes.h"
#include "AtmospherePhysics.h"
#include "EnvCommon.h"
#include "MoonTexture.h"
#include "StarTexture.h"
#include "AtmosphereLUT.h"
#include <simcore/GeoFrame.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/ImageView.h>

#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/core/Array.h>
#include <vsg/core/Version.h>

#include <cstdio>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

#if VSGRAYSIM_WITH_ROCKY
#include <rocky/vsg/SkyNode.h>
#include <rocky/vsg/VSGContext.h>
#include <rocky/DateTime.h>
#endif
#if VSG_API_VERSION_GREATER_EQUAL(1, 1, 3)
#include <vsg/lighting/DirectionalLight.h>
#include <vsg/lighting/SoftShadows.h>
#include <vsg/lighting/AmbientLight.h>
#else
#include <vsg/nodes/Light.h>
#endif
#include "EphemerisBridge.h"

namespace rockyenv {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ===========================================================================
// Rocky 兼容后端 (ECEF)
// ===========================================================================
class CelestialBackend : public ISkyBackend {
public:
    bool bodies(const vsg::dmat4& enuToEcef, SkyBodies& out) const override {
        EnvSunInfo s, m;
        float phase = 0.0f;
        if (!bridge_.sunInfo(enuToEcef, s,&out.sun.angularRadius)) return false;
        bridge_.moonInfo(enuToEcef, m, phase,&out.moon.angularRadius);
        out.sun.dirECEF = s.dirECEF; out.sun.dirENU = s.dirENU;
        out.sun.elDeg = s.elevationDeg; out.sun.azDeg = s.azimuthDeg;
        out.moon.dirECEF = m.dirECEF; out.moon.dirENU = m.dirENU;
        out.moon.elDeg = m.elevationDeg; out.moon.azDeg = m.azimuthDeg;
        out.moonPhase = phase;
        out.greenwichSiderealRadians = bridge_.greenwichSiderealRadians();
        out.valid = true;
        return true;
    }

    const AtmosphereLUT* atmosphereLUT() const override {
        return lut_ && lut_->isValid() ? lut_.get() : nullptr;
    }
protected:
    EphemerisBridge bridge_;
    std::unique_ptr<AtmosphereLUT> lut_;
    void buildLUT() {
        lut_ = std::make_unique<AtmosphereLUT>();
        if (!lut_->generateReference()) {
            std::fprintf(stderr, "[rocky_env] RockySky: failed to generate AtmosphereLUT\n");
            lut_.reset();
        }
    }
};

class NativeSkyBackend final : public CelestialBackend {
public:
    explicit NativeSkyBackend(const SkyParams& params) {
        show_sun_=params.show_sun;show_moon_=params.show_moon;
        sun_->shadowSettings=vsg::SoftShadows::create(2,.00465f);
        moon_->shadowSettings=vsg::SoftShadows::create(1,.00452f);
        lights_->addChild(sun_);lights_->addChild(ambient_);lights_->addChild(moon_);
        syncDateTime(params.year,params.month,params.day,params.hours_utc);
        if(params.use_bruneton_lut) buildLUT();
    }
    vsg::Node* node() override {return lights_;}
    bool worldMounted() const override {return true;}
    void syncDateTime(int y,int m,int d,double h) override {bridge_.setDateTime(y,m,d,h);}
    void onFrame(const EnvFrame&) override {}
    void applyRuntime(const SkyRuntime& rt) override { show_sun_=rt.show_sun;show_moon_=rt.show_moon; }
    void applyLights(const AtmosphereState& s,const SkyBodies& b) override {
        sun_->direction=-b.sun.dirECEF;sun_->color=s.sun_light;sun_->intensity=show_sun_?1.f:0.f;
        ambient_->color=s.ambient_light;
        moon_->direction=-b.moon.dirECEF;moon_->color=s.moon_light;moon_->intensity=show_moon_?1.f:0.f;
    }
private:
    bool show_sun_,show_moon_;
    vsg::ref_ptr<vsg::Group> lights_=vsg::Group::create();
    vsg::ref_ptr<vsg::DirectionalLight> sun_=vsg::DirectionalLight::create(),moon_=vsg::DirectionalLight::create();
    vsg::ref_ptr<vsg::AmbientLight> ambient_=vsg::AmbientLight::create();
};

#if VSGRAYSIM_WITH_ROCKY
class RockySkyBackend : public CelestialBackend {
public:
    explicit RockySkyBackend(const SkyParams& p) : p_(p) {
        auto ctx = static_cast<rocky::VSGContextImpl*>(p_.rocky_context);
        if (!ctx) { std::fprintf(stderr, "[rocky_env] RockySky: null context\n"); return; }
        sky_ = rocky::SkyNode::create(ctx);
        if (!sky_) { std::fprintf(stderr, "[rocky_env] SkyNode::create failed\n"); return; }
        sky_->setWorldSRS(rocky::SRS::ECEF);
        sky_->setShowAtmosphere(p_.show_atmosphere);

        ok_=true;
        syncDateTime(p_.year, p_.month, p_.day, p_.hours_utc);

        // 月亮平行光 (rocky SkyNode 无月亮); 加为 SkyNode 子节点, ECEF 方向。
        moon_light_ = vsg::DirectionalLight::create();
        moon_light_->name = "RockyMoon";
        moon_light_->intensity = 0.0f;
        sky_->addChild(moon_light_);

        // P0-2: 月盘渲染 (相机跟随公告板, ENU 相机偏移几何, 见 onFrame)
        buildMoonDisk();
        if (moon_disk_sg_) sky_->addChild(moon_disk_sg_);

        // P0-1: Bruneton LUT (可选, 用于增强大气散射)
        if (p_.use_bruneton_lut) {
            buildLUT();
        }

        ok_ = true;
        std::fprintf(stderr, "[rocky_env] RockySky backend: atmosphere=%d moon_disk=%d lut=%d %04d-%02d-%02d %.2fUTC\n",
                     p_.show_atmosphere ? 1 : 0, moon_disk_sg_ ? 1 : 0,
                     lut_ ? 1 : 0, p_.year, p_.month, p_.day, p_.hours_utc);
    }

    vsg::Node* node() override { return sky_.get(); }
    bool worldMounted() const override { return true; }  // ECEF 世界根

    void syncDateTime(int y, int m, int d, double h) override {
        if (!ok_) return;
        bridge_.setDateTime(y, m, d, h);  // 内部归一化日/月/年
        sky_->setDateTime(rocky::DateTime(bridge_.year(), bridge_.month(),
                                          bridge_.day(), bridge_.hoursUtc()));
    }

    void onFrame(const EnvFrame& f) override {
        if (!ok_ || !moon_ub_ || external_dome_) return;
        // 月盘: 填充 UBO (月亮方向 ENU + 月相 + 高度角) 并更新相机跟随公告板。
        EnvSunInfo m;
        float phase = 0.0f;
        if (!bridge_.moonInfo(f.model, m, phase)) return;
        float misc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        fillEnvUBO(*moon_ub_, f, misc);
        (*moon_ub_)[84] = m.dirENU.x;
        (*moon_ub_)[85] = m.dirENU.y;
        (*moon_ub_)[86] = m.dirENU.z;
        (*moon_ub_)[87] = phase;
        (*moon_ub_)[90] = m.elevationDeg;
        moon_ub_->dirty();
        updateMoonBillboard(m.dirENU, m.elevationDeg);
    }

    void applyLights(const AtmosphereState& s, const SkyBodies& b) override {
        if (!ok_) return;
        // 驱动 rocky 内置太阳/环境光 (rocky 原实现颜色强度恒定, 与物理脱钩)。
        // 注意: 不再重复 addChild (旧 facade 把 ambient/sun 二次挂载导致灯光翻倍)。
        if (sky_->sun) {
            sky_->sun->color = s.sun_light;
            sky_->sun->intensity = p_.show_sun ? 1.0f : 0.0f;
        }
        if (sky_->ambient) {
            sky_->ambient->color = s.ambient_light;
            sky_->ambient->intensity = 1.0f;
        }
        if (moon_light_) {
            moon_light_->direction = vsg::vec3(-b.moon.dirECEF.x, -b.moon.dirECEF.y, -b.moon.dirECEF.z);
            moon_light_->color = s.moon_light;
            moon_light_->intensity = (p_.show_moon) ? 1.0f : 0.0f;
        }
    }

    // v2: 运行时控制。锚点对 Rocky 后端无意义 (太阳方向由 EnvFrame::model 的
    // ENU→ECEF 决定); 仅透传渲染/物理参数。
    void applyRuntime(const SkyRuntime& rt) override {
        if (!rt.params_dirty) return;
        p_.exposure = rt.exposure;
        p_.turbidity = rt.turbidity;
        p_.show_sun = rt.show_sun;
        p_.show_moon = rt.show_moon;
        if (rt.show_atmosphere != p_.show_atmosphere) {
            p_.show_atmosphere = rt.show_atmosphere;
            sky_->setShowAtmosphere(rt.show_atmosphere && !external_dome_);
        }
    }

    const AtmosphereLUT* atmosphereLUT() const override {
        return (lut_ && lut_->isValid()) ? lut_.get() : nullptr;
    }
    void setExternalDome(bool enabled) override {
        external_dome_=enabled;
        sky_->setShowAtmosphere(p_.show_atmosphere && !enabled);
        auto& children=sky_->children;
        children.erase(std::remove(children.begin(),children.end(),moon_disk_sg_),children.end());
        if(!enabled && moon_disk_sg_)sky_->addChild(moon_disk_sg_);
    }

private:
    // P0-2: 构建月盘渲染 (相机跟随公告板四边形)。
    // 几何顶点为「相机 ENU 偏移」, 由 sky_dome.vert 平移到相机 + model(ENU→ECEF)
    // 定位到地球; 每帧在 updateMoonBillboard 中按月亮方向重建四顶点。
    void buildMoonDisk() {
        vsg::ref_ptr<vsg::floatArray> ub;
        // 月盘: 半透明、深度测试、背面剔除关闭
        vsg::Descriptors moonDescriptors{moonTexture(0)};
        moon_disk_sg_ = buildEnvPipeline("sky_dome.vert", "moon_disk.frag",
                                         /*blending=*/true, /*depth_test=*/true, ub,&moonDescriptors,VK_SHADER_STAGE_FRAGMENT_BIT);
        if (!moon_disk_sg_) {
            std::fprintf(stderr, "[rocky_env] RockySky: failed to build moon disk pipeline\n");
            return;
        }
        moon_ub_ = ub;

        const float kMoonDiskDist  = 100000.0f;   // 100 km (远小于远裁面, 远大于盘面尺寸)
        const float kMoonDiskHalf  = kMoonDiskDist * 0.0060f;  // 月角半径 0.0045 rad × 余量
        const vsg::vec3 c(0.0f, kMoonDiskDist, 0.0f);
        std::vector<vsg::vec3> v = {
            c + vsg::vec3(-kMoonDiskHalf, 0.0f, -kMoonDiskHalf),
            c + vsg::vec3( kMoonDiskHalf, 0.0f, -kMoonDiskHalf),
            c + vsg::vec3( kMoonDiskHalf, 0.0f,  kMoonDiskHalf),
            c + vsg::vec3(-kMoonDiskHalf, 0.0f,  kMoonDiskHalf)};
        std::vector<vsg::vec3> n(4, vsg::vec3(0.0f, -1.0f, 0.0f));
        std::vector<vsg::vec2> uv = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        const std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};

        moon_verts_ = vsg::vec3Array::create(v.size());
        moon_verts_->properties.dataVariance = vsg::DYNAMIC_DATA;
        auto na = vsg::vec3Array::create(n.size());
        auto ua = vsg::vec2Array::create(uv.size());
        for (size_t i = 0; i < v.size(); ++i) {
            (*moon_verts_)[i] = v[i]; (*na)[i] = n[i]; (*ua)[i] = uv[i];
        }
        auto ia = vsg::uintArray::create(idx.size());
        for (size_t i = 0; i < idx.size(); ++i) (*ia)[i] = idx[i];
        auto vid = vsg::VertexIndexDraw::create();
        vid->assignArrays({moon_verts_, na, ua});
        vid->assignIndices(ia);
        vid->indexCount = static_cast<uint32_t>(idx.size());
        vid->instanceCount = 1;
        moon_disk_sg_->addChild(vid);
    }

    // 每帧重建月盘公告板四顶点 (中心 = 月亮方向 × kMoonDiskDist 的相机偏移)。
    void updateMoonBillboard(const vsg::vec3& moonDirENU, float moonElDeg) {
        if (!moon_verts_) return;
        // 月亮在地平线下或被关闭: 把四边形藏到相机正下方深处。
        if (moonElDeg < -2.0f || !p_.show_moon) {
            for (uint32_t i = 0; i < moon_verts_->size(); ++i)
                (*moon_verts_)[i] = vsg::vec3(0.0f, -1.0e6f, 0.0f);
            moon_verts_->dirty();
            return;
        }
        const float kDist = 100000.0f;
        const float kHalf = kDist * 0.0060f;
        const vsg::vec3 dir = vsg::normalize(moonDirENU);
        // 与月亮方向正交的稳定基
        const vsg::vec3 ref = std::abs(dir.y) < 0.99f ? vsg::vec3(0.0f, 1.0f, 0.0f)
                                                      : vsg::vec3(1.0f, 0.0f, 0.0f);
        const vsg::vec3 right = vsg::normalize(vsg::cross(ref, dir));
        const vsg::vec3 up    = vsg::cross(dir, right);
        const vsg::vec3 c = dir * kDist;
        (*moon_verts_)[0] = c - right * kHalf - up * kHalf;
        (*moon_verts_)[1] = c + right * kHalf - up * kHalf;
        (*moon_verts_)[2] = c + right * kHalf + up * kHalf;
        (*moon_verts_)[3] = c - right * kHalf + up * kHalf;
        moon_verts_->dirty();
    }

    // P0-1: 构建 Bruneton LUT


    SkyParams p_;
    bool external_dome_=false;
    vsg::ref_ptr<rocky::SkyNode> sky_;

    vsg::ref_ptr<vsg::DirectionalLight> moon_light_;

    // P0-2: 月盘渲染 (相机跟随公告板)
    vsg::ref_ptr<vsg::StateGroup> moon_disk_sg_;
    vsg::ref_ptr<vsg::vec3Array>  moon_verts_;
    vsg::ref_ptr<vsg::floatArray> moon_ub_;

    // P0-1: Bruneton LUT


    bool ok_ = false;
};
#endif


// ===========================================================================
// facade
// ===========================================================================
class SkyModule : public IEnvModule,
                 public ISunProvider,
                 public ILightingProvider,
                 public IAtmosphereQuery,
                 public ISkyControl {
public:
    explicit SkyModule(const SkyParams& p) : p_(p) {
        rt_.exposure = p_.exposure;
        rt_.turbidity = p_.turbidity;
        rt_.show_sun = p_.show_sun;
        rt_.show_moon = p_.show_moon;
        rt_.show_atmosphere = p_.show_atmosphere;
#if !VSGRAYSIM_WITH_ROCKY
        p_.voxel_earth = true;
#endif
        if(p_.voxel_earth) backend_=std::make_unique<NativeSkyBackend>(p_);
#if VSGRAYSIM_WITH_ROCKY
        else if(p_.rocky_context) backend_=std::make_unique<RockySkyBackend>(p_);
#endif
        else return;

        holder_ = vsg::Switch::create();
        holder_->addChild(true, vsg::ref_ptr<vsg::Node>(backend_->node()));
        buildDome();
        if (dome_sg_) {holder_->addChild(true, dome_sg_);backend_->setExternalDome(true);}
        if(p_.voxel_earth && !dome_sg_) {backend_.reset();return;}
        enabled_ = true;
    }

    bool valid() const { return backend_ != nullptr; }

    const char* name() const override { return "rocky_sky"; }
    vsg::Node* rootNode() override { return holder_.get(); }
    bool mountInWorldRoot() const override {
        return backend_ ? backend_->worldMounted() : false;
    }

    void onFrame(const EnvFrame& f) override {
        // v2: 先应用宿主/ISkyControl 下发的运行时状态, 再同步时刻。
        if (rt_dirty_) {
            backend_->applyRuntime(rt_);
            p_.exposure=rt_.exposure;p_.turbidity=rt_.turbidity;
            p_.show_sun=rt_.show_sun;p_.show_moon=rt_.show_moon;p_.show_atmosphere=rt_.show_atmosphere;
            rt_dirty_ = false;
        }
        backend_->syncDateTime(f.year, f.month, f.day, f.hours_utc);
        backend_->onFrame(f);

        SkyBodies bodies;
        if (backend_->bodies(f.model, bodies)) {
            // P1-1: 传递天气参数给大气计算
            WeatherParams weather;
            weather.visibility = f.visibility;
            weather.cloud_cover = f.cloud_cover;
            weather.wind_speed = f.wind_speed;
            weather.precipitation = f.precipitation;
            state_ = computeAtmosphere(bodies, p_.turbidity, p_.exposure, weather);
            backend_->applyLights(state_, bodies);
            sun_ecef_ = vsg::dvec3(bodies.sun.dirECEF);
            has_state_ = true;
        }
        // 原生穹顶 UBO: misc={曝光,浊度,相机海拔} + 月亮(84..87) + 星空历(88..91)。
        // 星空/银河/日盘/月晕由 sky_dome.frag 逐像素解析计算 (rocky SkyNode 无星)。
        if (dome_ub_) {
            // Sky rays use the current observer's local vertical, not an
            // unbounded tangent plane around the scene's original anchor.
            EnvFrame skyFrame=f;
            const auto camera=f.model*vsg::dvec4(f.cam_pos_enu,1.0);
            const auto geo=simcore::geo::ecefToGeodetic(camera.x,camera.y,camera.z);
            simcore::geo::GeoAnchor observer;observer.lat_deg=geo.x;observer.lon_deg=geo.y;observer.alt_m=geo.z;
            skyFrame.model=simcore::geo::makeEnuToWorldMatrix(observer);
            skyFrame.cam_pos_enu={0,0,0};
            SkyBodies observerBodies;backend_->bodies(skyFrame.model,observerBodies);
            const float misc[4] = {rt_.exposure, p_.turbidity,
                                   static_cast<float>(geo.z), float((p_.show_atmosphere?1:0)+(p_.show_sun?2:0))};
            fillEnvUBO(*dome_ub_, skyFrame, misc);
            // 太阳 (36..39): sky 先于 EnvStack 的派生 (sky_drives_sun 在
            // EnvStack.onFrame 里发生在本函数返回后), f.sun_dir_enu 此刻仍
            // 为零 —— 直接用 backend 星历真值, 夜/昼散射与时刻正确联动。
            if (observerBodies.valid) {
                (*dome_ub_)[36] = observerBodies.sun.dirENU.x;
                (*dome_ub_)[37] = observerBodies.sun.dirENU.y;
                (*dome_ub_)[38] = observerBodies.sun.dirENU.z;
                (*dome_ub_)[39] = observerBodies.sun.elDeg;
            }
            // 84..87 moon: xyz=月亮方向 ENU, w=月相 (show_moon=false → w=-1 关月晕)
            (*dome_ub_)[84] = observerBodies.moon.dirENU.x;
            (*dome_ub_)[85] = observerBodies.moon.dirENU.y;
            (*dome_ub_)[86] = observerBodies.moon.dirENU.z;
            (*dome_ub_)[87] = p_.show_moon ? observerBodies.moonPhase : -1.0f;
            // 88..91 astro: local mean sidereal angle, latitude, lunar elevation.
            (*dome_ub_)[88] = float(std::remainder(observerBodies.greenwichSiderealRadians+
                                                  simcore::geo::deg2rad(geo.y),2.*kPi));
            (*dome_ub_)[89] = float(simcore::geo::deg2rad(geo.x));
            (*dome_ub_)[90] = observerBodies.moon.elDeg;
            // Fade the unresolved weather veil through the authored deck,
            // allowing a clear sky above low stratus and rain.
            const float cloudBase=std::max(f.cloud_base_m,0.f);
            const float altitude=float(geo.z);
            const float belowCloud=1.f-std::clamp((altitude-cloudBase)/1450.f,0.f,1.f);
            const float overcast=f.overcast>=0.f?f.overcast:(p_.overcast?1.f:0.f);
            (*dome_ub_)[91] = std::clamp(overcast,0.f,1.f)*belowCloud*belowCloud*(3.f-2.f*belowCloud);
            (*dome_ub_)[92] = observerBodies.sun.angularRadius;
            (*dome_ub_)[93] = observerBodies.moon.angularRadius;
            const float visibility=std::max(100.f,f.visibility);
            // A near-surface excess column relative to the clear LUT.
            (*dome_ub_)[94] = std::clamp(3.912f*650.f*(1.f/visibility-1.f/24000.f),0.f,.75f)*
                std::exp(-std::max(altitude,0.f)/1150.f);
            (*dome_ub_)[95] = std::clamp(f.precipitation,0.f,1.f)*belowCloud;
            dome_ub_->dirty();
        }
    }

    void setEnabled(bool on) override {
        enabled_ = on;
        if (holder_) holder_->setAllChildren(on);
    }
    bool enabled() const override { return enabled_; }

    // ISunProvider
    void syncDateTime(int y, int m, int d, double h) override {
        backend_->syncDateTime(y, m, d, h);
    }
    bool sunInfo(const vsg::dmat4& enuToEcef, EnvSunInfo& out) const override {
        SkyBodies b;
        if (!backend_->bodies(enuToEcef, b)) return false;
        out.dirECEF = b.sun.dirECEF;
        out.dirENU = b.sun.dirENU;
        out.elevationDeg = b.sun.elDeg;
        out.azimuthDeg = b.sun.azDeg;
        return true;
    }

    bool solarEnabled() const override { return rt_.show_sun; }
    bool lunarEnabled() const override { return rt_.show_moon; }
    bool moonInfo(const vsg::dmat4& frame,EnvSunInfo& out,float& phase) const override {
        SkyBodies bodies;
        if(!backend_->bodies(frame,bodies)) return false;
        out.dirECEF=bodies.moon.dirECEF;out.dirENU=bodies.moon.dirENU;
        out.elevationDeg=bodies.moon.elDeg;out.azimuthDeg=bodies.moon.azDeg;
        phase=bodies.moonPhase;return true;
    }

    // ILightingProvider
    bool getAtmosphereState(AtmosphereState& out) const override {
        if (!has_state_) return false;
        out = state_;
        return true;
    }

    // ISkyControl —— 运行时控制 (面板/想定编辑器消费)。
    // 注: 太阳方向由 EnvFrame::model (锚点 ENU→ECEF) 决定, 观测锚点不可运行时
    // 迁移 (锚点随 rocky_map 走); 仅渲染/物理参数可调。
    void setExposure(float e) override {
        rt_.exposure = e; rt_.params_dirty = true; rt_dirty_ = true;
    }
    void setTurbidity(float t) override {
        rt_.turbidity = t; rt_.params_dirty = true; rt_dirty_ = true;
    }
    void setShowSun(bool on) override {
        rt_.show_sun = on; rt_.params_dirty = true; rt_dirty_ = true;
    }
    void setShowMoon(bool on) override {
        rt_.show_moon = on; rt_.params_dirty = true; rt_dirty_ = true;
    }
    void setShowAtmosphere(bool on) override {
        rt_.show_atmosphere = on; rt_.params_dirty = true; rt_dirty_ = true;
    }
    float getExposure() const override { return rt_.exposure; }
    float getTurbidity() const override { return rt_.turbidity; }
    bool  getShowSun() const override { return rt_.show_sun; }
    bool  getShowMoon() const override { return rt_.show_moon; }
    bool  getShowAtmosphere() const override { return rt_.show_atmosphere; }

    // IAtmosphereQuery —— 视线路径透过率/路径辐射 (仿真侧大气衰减)。
    // 消光系数与 AtmosphereLUT 生成所用一致 (Rayleigh/Mie 指数大气, 同源)。
    // 世界系恒为 ECEF (rocky 地球)。
    bool transmittance(const vsg::dvec3& fromECEF, const vsg::dvec3& toECEF,
                       vsg::vec3& tau, vsg::vec3& pathRadiance) const override {
        return has_state_ && sampleAtmospherePath(state_,sun_ecef_,fromECEF,toECEF,
                                                  backend_->atmosphereLUT(),tau,pathRadiance);
    }

    // 批次2 (谱大气): 波段化天空真值 —— 与渲染侧同一星历/能见度真值源。
    // 物理模型 (晴空; 云调制由消费侧 EnvPhysicsService 叠加, 基线自洽标定):
    //   σ      = 分子基线 (clearSkyEnv) + 气溶胶 (Koschmieder+Ångström, 能见度同源)
    //   散射波段(TV/SWIR): L_sky = 晴空基线 × 昼门控 × (0.5+0.5·airmass)
    //            —— 视线气团越大 (仰角越低) 散射路径越长天空越亮; 天顶=基线
    //   热波段(MWIR/LWIR): L_sky = ε_atm(airmass)·M_band(T_atm)/π,
    //            ε_atm = 1-exp(-τ0·airmass), τ0 反解自晴空基线 (天顶=基线,
    //            地平 ε→1 趋近大气温度黑体) —— 天顶暗/地平亮与基线行为吻合
    //   esun   = M_band(5778K)·(R_sun/AU)²·exp(-σ·airmass_sun)
    //            —— TOA 波段积分 × 斜程透过; MWIR/LWIR 自动趋零, 无需硬编码
    bool bandSky(double lambda_min_m, double lambda_max_m, double view_el_deg,
                 double& L_sky, double& sigma, double& esun) const override {
        return has_state_ && sampleBandSky(state_,lambda_min_m,lambda_max_m,view_el_deg,L_sky,sigma,esun);
    }

private:
    // ---- 原生天空穹顶 (sky_dome.vert/.frag): 解析散射 + 日盘 + 月晕 +
    //      程序化星空/银河 (rocky SkyNode 无星空, show_atmosphere=false 时
    //      天空由本穹顶全权渲染)。几何 = 以原点为心的球 (顶点静态), vert
    //      每帧按 camPosTime 平移到相机; 半径须大于月盘轨道 (100km)。
    void buildDome() {
        if (const auto* lut=backend_->atmosphereLUT()) {
            vsg::Descriptors descriptors;
            const auto& textures=lut->textures();
            for(const auto& image:{textures.transmittance,textures.scattering,textures.irradiance}) {
                auto view=vsg::ImageView::create(image);
                view->viewType=image->imageType==VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
                descriptors.push_back(vsg::DescriptorImage::create(
                    vsg::ImageInfo::create(lut->sampler(),view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),uint32_t(descriptors.size())));
            }
            descriptors.push_back(moonTexture(3));
            descriptors.push_back(starTexture(4));
            dome_sg_=buildSkyLUTPipeline("sky_dome.vert","sky_dome_lut.frag",false,true,dome_ub_,descriptors);
        }
        if(!dome_sg_) {
            if(p_.use_bruneton_lut) std::fprintf(stderr,"[rocky_env] sky LUT unavailable: using analytic scattering fallback\n");
            vsg::Descriptors celestialTextures{moonTexture(0),starTexture(1)};
            dome_sg_ = buildEnvPipeline("sky_dome.vert", "sky_dome.frag",
                                        /*blending=*/false, /*depth_test=*/true, dome_ub_,&celestialTextures,VK_SHADER_STAGE_FRAGMENT_BIT);
        }
        if (!dome_sg_) {
            std::fprintf(stderr, "[rocky_env] RockySky: 原生穹顶管线创建失败\n");
            return;
        }
        constexpr float R = 200000.0f;   // 200 km (> 月盘 100km, < far 1e7)
        constexpr int AZ = 48, EL = 24;
        // Regional volumes can expose the lower hemisphere beyond their coverage.
        // The sky shader evaluates Earth-intersecting rays using the horizon radiance.
        constexpr float PHI0 = -float(kPi) * .5f;
        std::vector<vsg::vec3> v;
        std::vector<vsg::vec3> n;
        std::vector<vsg::vec2> uv;
        v.reserve(size_t(AZ + 1) * (EL + 1));
        n.reserve(v.capacity());
        uv.reserve(v.capacity());
        for (int j = 0; j <= EL; ++j) {
            const float phi = PHI0 + (float(kPi) * 0.5f - PHI0) * j / EL;
            for (int i = 0; i <= AZ; ++i) {
                const float th = 2.0f * float(kPi) * i / AZ;
                const float cp = std::cos(phi);
                const vsg::vec3 d(cp * std::cos(th), std::sin(phi), cp * std::sin(th));
                v.push_back(d * R);
                n.push_back(-d);            // 内表面
                uv.emplace_back(th / (2.0f * float(kPi)), phi / (0.5f * float(kPi)));
            }
        }
        std::vector<uint32_t> idx;
        idx.reserve(size_t(AZ) * EL * 6);
        for (int j = 0; j < EL; ++j) {
            for (int i = 0; i < AZ; ++i) {
                const uint32_t a = uint32_t(j * (AZ + 1) + i);
                const uint32_t b = a + 1;
                const uint32_t c = a + AZ + 1;
                const uint32_t d = c + 1;
                idx.push_back(a); idx.push_back(c); idx.push_back(b);
                idx.push_back(b); idx.push_back(c); idx.push_back(d);
            }
        }
        auto va = vsg::vec3Array::create(v.size());
        auto na = vsg::vec3Array::create(n.size());
        auto ua = vsg::vec2Array::create(uv.size());
        for (size_t i = 0; i < v.size(); ++i) {
            (*va)[i] = v[i]; (*na)[i] = n[i]; (*ua)[i] = uv[i];
        }
        auto ia = vsg::uintArray::create(idx.size());
        for (size_t i = 0; i < idx.size(); ++i) (*ia)[i] = idx[i];
        auto vid = vsg::VertexIndexDraw::create();
        vid->assignArrays({va, na, ua});
        vid->assignIndices(ia);
        vid->indexCount = static_cast<uint32_t>(idx.size());
        vid->instanceCount = 1;
        dome_sg_->addChild(vid);
    }

    SkyParams p_;
    SkyRuntime rt_;                 // v2: ISkyControl 缓存 (构造期初值取自 p_)
    bool rt_dirty_ = false;
    std::unique_ptr<ISkyBackend> backend_;
    vsg::ref_ptr<vsg::Switch> holder_;
    vsg::ref_ptr<vsg::StateGroup> dome_sg_;   // 原生穹顶管线
    vsg::ref_ptr<vsg::floatArray> dome_ub_;
    AtmosphereState state_;
    vsg::dvec3 sun_ecef_{0,0,1};
    bool has_state_ = false;
    bool enabled_ = true;
};

}  // namespace

std::unique_ptr<IEnvModule> createSkyModule(const SkyParams& p)
{
    std::unique_ptr<SkyModule> m(new SkyModule(p));
    if (!m->valid()) return nullptr;
    return std::unique_ptr<IEnvModule>(m.release());
}

}  // namespace rockyenv
