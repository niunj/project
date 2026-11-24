/// smoke_sky.cpp —— rocky_env Sky 链路冒烟 (无窗口, Rocky 单后端):
///   1) rocky VSGContext (裸 vsg::Viewer, GPU 工作推迟到 compile 阶段);
///   2) createSkyModule (rocky_context 注入, ECEF 世界根);
///   3) ISunProvider 星历真值 (锚点经 EnvFrame::model 的 ENU→ECEF 决定);
///   4) IAtmosphereQuery 视线透过率/路径辐射;
///   5) ILightingProvider AtmosphereState (暮光染色验证);
///   6) ISkyControl 运行时控制 + 锚点迁移 (更换 f.model = 另一锚点的 ENU→ECEF)。
#include <rocky_env/EnvApi.h>
#include <rocky_env/RuntimePaths.h>

#if VSGRAYSIM_WITH_ROCKY
#include <rocky/vsg/VSGContext.h>
#endif

#include <simcore/GeoFrame.h>

#include <vsg/app/Viewer.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>

using namespace rockyenv;
using simcore::geo::GeoAnchor;
using simcore::geo::makeEnuToWorldMatrix;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); ++g_fail; } } while (0)

int main() {
    // ---- 0. rocky 数据路径兜底 (与 gui RockyMapFeature::ensureEnvironment 同款) ----
    // PROJ 找不到 proj.db 会使 SRS::ECEF 无效 → SkyNode 不创建太阳光,
    // setDateTime 解引用空指针段错误; ROCKY_FILE_PATH 供 rocky 定位自带 shader。
    rockyenv::runtime::configureDefaults();

    // ---- 1. rocky VSGContext (无窗口: 裸 Viewer, SkyNode 创建不触发 GPU 编译) ----
#if VSGRAYSIM_WITH_ROCKY
    auto viewer = vsg::Viewer::create();
    auto singleton = rocky::VSGContextFactory::create(viewer);   // 按值, 持有于 main 作用域
    void* ctx = singleton.get();
    CHECK(ctx != nullptr, "VSGContextFactory::create(裸 viewer) 成功");

#else
    void* ctx=nullptr;
#endif
    SkyParams p_no_ctx;
#if VSGRAYSIM_WITH_ROCKY
    CHECK(createSkyModule(p_no_ctx) == nullptr, "legacy sky requires the map context");
#else
    CHECK(createSkyModule(p_no_ctx) != nullptr, "standalone VSG sky is the default in voxel builds");
#endif

    SkyParams p;
    p.rocky_context = ctx; p.voxel_earth=!ctx;
    p.year = 2026; p.month = 9; p.day = 16; p.hours_utc = 4.0;  // 台北正午
    auto mod = createSkyModule(p);
    CHECK(mod != nullptr, "createSkyModule(Rocky) 成功");
    CHECK(mod->rootNode() != nullptr, "rootNode 非空");
    CHECK(mod->mountInWorldRoot() == true, "Rocky 后端挂 ECEF 世界根");

    auto* sun = dynamic_cast<ISunProvider*>(mod.get());
    auto* light = dynamic_cast<ILightingProvider*>(mod.get());
    auto* atmo = dynamic_cast<IAtmosphereQuery*>(mod.get());
    CHECK(sun && light && atmo, "ISunProvider/ILighting/IAtmosphereQuery 三接口可查询");

    // ---- 3. 每帧驱动 (锚点 ENU→ECEF, 同 gui contentModel 契约) ----
    GeoAnchor anchor;   // 默认台北 25.078N, 121.772E
    const vsg::dmat4 model = makeEnuToWorldMatrix(anchor);
    // ENU 点 → ECEF (vsg 无 dvec4→dvec3 隐式转换; IAtmosphereQuery 契约恒 ECEF)。
    auto toECEF = [&model](const vsg::dvec3& enu) {
        const vsg::dvec4 r = model * vsg::dvec4(enu, 1.0);
        return vsg::dvec3(r.x, r.y, r.z);
    };
    EnvFrame f;
    f.model = model;
    f.view_proj = vsg::dmat4(1.0);
    f.cam_pos_enu = vsg::dvec3(0.0, 100.0, 0.0);
    f.year = 2026; f.month = 9; f.day = 16; f.hours_utc = 4.0;
    mod->onFrame(f);

    // ---- 4. 星历: 台北 9 月正午太阳应在高空 ----
    EnvSunInfo si;
    bool ok = sun->sunInfo(model, si);
    CHECK(ok, "sunInfo 查询成功");
    std::printf("    太阳: 高度角 %.1f°, 方位角 %.1f°\n", si.elevationDeg, si.azimuthDeg);
    CHECK(si.elevationDeg > 40.0f && si.elevationDeg < 95.0f, "正午太阳高度角 40°..95°");

    // ---- 5. AtmosphereState: 正午直射应接近白且亮 ----
    AtmosphereState st;
    CHECK(light->getAtmosphereState(st), "getAtmosphereState 有效");
    const float sunLum = (st.sun_light.x + st.sun_light.y + st.sun_light.z) / 3.0f;
    std::printf("    正午直射亮度 %.3f, 昼因子 %.2f\n", sunLum, st.is_day);
    CHECK(sunLum > 0.5f, "正午直射亮度 > 0.5");
    CHECK(st.is_day > 0.9f, "昼因子 ≈ 1");

    // ---- 6. 暮光: UTC 21:00 (台北清晨 5 点) 直射应变暗 ----
    EnvFrame f2 = f;
    f2.hours_utc = 21.0;  // 前一天 21 UTC = 本地 ~05:00
    mod->onFrame(f2);
    AtmosphereState st2;
    light->getAtmosphereState(st2);
    const float lum2 = (st2.sun_light.x + st2.sun_light.y + st2.sun_light.z) / 3.0f;
    std::printf("    晨光直射亮度 %.3f (R=%.3f B=%.3f), 昼因子 %.2f\n",
                lum2, st2.sun_light.x, st2.sun_light.z, st2.is_day);
    CHECK(lum2 < sunLum, "夜间直射为 0 (暗于正午)");
    CHECK(st2.is_day < 0.1f, "夜间昼因子 ≈ 0");
    // 暮光窗口扫描: 从 08:00 UTC 起步进 0.05h, 找太阳高度角 1°..5° 的帧
    // (台北 9 月日落 ≈ 09:55 UTC), 验证低角度直射偏红。
    EnvFrame ft = f;
    bool found = false;
    for (double h = 8.0; h < 11.0 && !found; h += 0.05) {
        ft.hours_utc = h;
        mod->onFrame(ft);
        EnvSunInfo tw;
        if (!sun->sunInfo(model, tw)) break;
        if (tw.elevationDeg > 1.0f && tw.elevationDeg < 5.0f) {
            AtmosphereState stw;
            light->getAtmosphereState(stw);
            const float lumT = (stw.sun_light.x + stw.sun_light.y + stw.sun_light.z) / 3.0f;
            std::printf("    暮光窗 (太阳 %.1f°, UTC %.2f): 直射亮度 %.3f (R=%.3f B=%.3f)\n",
                        tw.elevationDeg, h, lumT, stw.sun_light.x, stw.sun_light.z);
            CHECK(lumT > 0.0f && lumT < sunLum, "暮光直射暗于正午且非零");
            CHECK(stw.sun_light.x > stw.sun_light.z, "暮光直射偏红 (R>B)");
            found = true;
        }
    }
    CHECK(found, "扫描到暮光窗口帧");

    // ---- 7. IAtmosphereQuery: 海平面 10km 水平视线 (ENU 查询点 → ECEF) ----
    vsg::vec3 tau, pr;
    ok = atmo->transmittance(toECEF(vsg::dvec3(0, 10, 0)),
                             toECEF(vsg::dvec3(10000, 10, 0)), tau, pr);
    CHECK(ok, "transmittance 查询成功");
    std::printf("    10km 水平: tau=(%.3f,%.3f,%.3f) L=(%.4f,%.4f,%.4f)\n",
                tau.x, tau.y, tau.z, pr.x, pr.y, pr.z);
    CHECK(tau.x > 0.1f && tau.x < 1.0f, "10km 透过率在 (0.1, 1)");
    CHECK(tau.y < tau.x && tau.z < tau.y, "蓝通道消光最强 (R>G>B 透过率)");
    CHECK(pr.x >= 0.0f && pr.x < 1.0f, "路径辐射非负且有限");

    // 高空视线 (相机 8km): 透过率应显著高于海面
    vsg::vec3 tau2, pr2;
    atmo->transmittance(toECEF(vsg::dvec3(0, 8000, 0)),
                        toECEF(vsg::dvec3(10000, 8000, 0)), tau2, pr2);
    std::printf("    8km 高空: tau=(%.3f,%.3f,%.3f)\n", tau2.x, tau2.y, tau2.z);
    CHECK(tau2.x > tau.x, "高空透过率高于海面");

    // ---- 8. 显隐开关 ----
    mod->setEnabled(false);
    CHECK(!mod->enabled(), "setEnabled(false) 生效");
    mod->setEnabled(true);
    CHECK(mod->enabled(), "setEnabled(true) 生效");

    // ---- 9. ISkyControl 运行时控制 ----
    auto* ctrl = dynamic_cast<ISkyControl*>(mod.get());
    CHECK(ctrl != nullptr, "ISkyControl 可查询");
    if (ctrl) {
        const float ex0 = ctrl->getExposure();
        ctrl->setExposure(1.7f);
        ctrl->setTurbidity(2.5f);
        ctrl->setShowMoon(false);
        CHECK(std::abs(ctrl->getExposure() - 1.7f) < 1e-4f, "setExposure 生效");
        CHECK(std::abs(ctrl->getTurbidity() - 2.5f) < 1e-4f, "setTurbidity 生效");
        CHECK(ctrl->getShowMoon() == false, "setShowMoon(false) 生效");
        // 下一帧生效 (不崩溃且光照仍可查询)。
        mod->onFrame(f);
        AtmosphereState st3;
        CHECK(light->getAtmosphereState(st3), "参数修改后 onFrame 仍有效");
        // 恢复。
        ctrl->setExposure(ex0);
        ctrl->setTurbidity(1.0f);
        ctrl->setShowMoon(true);
    }

    // ---- 10. 锚点迁移 (Rocky 方式: 更换 EnvFrame::model 的 ENU→ECEF) ----
    // 从台北 (25.1N) 移到新加坡 (1.35N): 同一 UTC 时刻太阳高度角应显著变化。
    EnvSunInfo si0;
    sun->sunInfo(model, si0);
    GeoAnchor sg_anchor;
    sg_anchor.lat_deg = 1.35; sg_anchor.lon_deg = 103.82;
    const vsg::dmat4 model_sg = makeEnuToWorldMatrix(sg_anchor);
    EnvSunInfo si1;
    const bool ok1 = sun->sunInfo(model_sg, si1);
    CHECK(ok1, "锚点迁移 (新 ENU→ECEF) 后 sunInfo 查询成功");
    std::printf("    太阳高度角: 台北 %.1f° → 新加坡 %.1f°\n",
                si0.elevationDeg, si1.elevationDeg);
    CHECK(std::abs(si1.elevationDeg - si0.elevationDeg) > 5.0f,
          "锚点迁移后太阳高度角显著变化 (>5°)");

    // ---- 11. EnvStack 聚合栈 (sky+cloud+ocean 一站式) ----
    {
        EnvironmentStackParams sp;
        sp.sky.rocky_context = ctx; sp.sky.voxel_earth=!ctx;
        auto stack = createEnvironmentStack(sp);
        CHECK(stack != nullptr, "createEnvironmentStack 成功");
        CHECK(stack->worldRoot() != nullptr && stack->contentRoot() != nullptr,
              "栈双根节点非空");
        CHECK(stack->module("sky") != nullptr, "module(\"sky\") 可访问");
        CHECK(stack->module("cloud") != nullptr, "module(\"cloud\") 可访问");
        CHECK(stack->module("ocean") != nullptr, "module(\"ocean\") 可访问");
        CHECK(stack->module("nope") == nullptr, "module(未知名) 返回 nullptr");

        EnvFrame sf;
        sf.model = model;
        sf.view_proj = vsg::dmat4(1.0);
        sf.cam_pos_enu = vsg::dvec3(0.0, 200.0, 0.0);
        sf.year = 2026; sf.month = 9; sf.day = 16; sf.hours_utc = 4.0;
        sf.sun_dir_enu = vsg::vec3(0.0f, -1.0f, 0.0f);  // 故意给错值, 验证栈覆写
        sf.sun_el_deg = -30.0f;
        stack->onFrame(sf);
        CHECK(sf.sun_el_deg > 40.0f, "栈内星历覆写 EnvFrame 太阳 (真值传播)");
        CHECK(sf.is_day > 0.9f, "栈内光照真值覆写昼因子");

        auto* stkSun = stack->query<ISunProvider>();
        auto* stkLight = stack->query<ILightingProvider>();
        CHECK(stkSun != nullptr && stkLight != nullptr,
              "栈能力转发 query<ISunProvider/ILightingProvider>");
        auto* stkCtrl = stack->query<ISkyControl>();
        CHECK(stkCtrl != nullptr, "栈能力转发 query<ISkyControl>");
    }

    std::printf("\n%s: %d 项失败\n", g_fail ? "SMOKE FAILED" : "SMOKE PASSED", g_fail);
    return g_fail ? 1 : 0;
}
