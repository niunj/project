/// smoke_cloud.cpp —— rocky_env Cloud v4 链路冒烟 (无窗口):
///   1) createCloudModule 单层兼容模式 (一期行为不变);
///   2) ICloudControl/ICloudQuery 可查询;
///   3) 多层配置 (卷云+积云+积雨云) 与层管理;
///   4) 云量调制/总云量一致性;
///   5) ICloudQuery: 云底/云顶、云影因子 (太阳高度/云量敏感);
///   6) CPU 云量函数与 shadowFactor 行为合理性。
#include <rocky_env/EnvApi.h>
#include <rocky_env/SparseCloudVolume.h>
#include <rocky_env/VoxelCloudCluster.h>

#include <vsg/nodes/Node.h>

#include <cstdio>
#include <cmath>

using namespace rockyenv;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); ++g_fail; } } while (0)

int main() {
    // ---- 1. 单层兼容模式 ----
    CloudParams p;
    p.cover = 0.5f;
    auto mod = createCloudModule(p);
    CHECK(mod != nullptr, "createCloudModule (单层兼容) 成功");
    CHECK(mod->rootNode() != nullptr, "rootNode 非空");

    auto* ctrl = dynamic_cast<ICloudControl*>(mod.get());
    auto* query = dynamic_cast<ICloudQuery*>(mod.get());
    CHECK(ctrl && query, "ICloudControl/ICloudQuery 可查询");

    EnvFrame f;
    f.model = vsg::dmat4(1.0);
    f.view_proj = vsg::dmat4(1.0);
    f.cam_pos_enu = vsg::dvec3(0.0, 50.0, 0.0);
    f.sun_dir_enu = vsg::vec3(0.4f, 0.8f, 0.3f);
    f.sun_el_deg = 55.0f;
    f.wind_dir_enu = vsg::vec3(1.0f, 0.0f, 0.0f);
    f.wind_speed = 6.0f;
    f.cloud_cover = 0.5f;
    f.time_sec = 100.0;
    mod->onFrame(f);

    // ---- 2. 单层模式查询 ----
    float base = 0, top = 0;
    CHECK(query->cloudBase(base), "单层模式 cloudBase 有效");
    CHECK(base < 1000.0f, "兼容模式云底取层云高度 (<1000m)");
    CHECK(query->cloudTop(top) && top > base, "cloudTop > cloudBase");
    const float cov0 = query->totalCoverage();
    std::printf("    兼容模式总云量 %.2f (云底 %.0fm 云顶 %.0fm)\n", cov0, base, top);
    CHECK(cov0 > 0.1f && cov0 < 1.0f, "总云量在 (0.1, 1)");

    // 云影: 晴天高太阳。
    const float sf0 = query->shadowFactor(500.0, 500.0, 0.9);
    std::printf("    云影因子 (太阳 52°): %.3f\n", sf0);
    CHECK(sf0 > 0.0f && sf0 <= 1.0f, "云影因子在 0..1");

    // ---- 3. 多层配置 ----
    std::vector<CloudLayerParams> layers(3);
    layers[0].type = CloudLayerType::Cirrus;
    layers[0].altitude = 8000.0f; layers[0].coverage = 0.4f;
    layers[1].type = CloudLayerType::Cumulus;
    layers[1].altitude = 1200.0f; layers[1].coverage = 0.6f;
    layers[2].type = CloudLayerType::Cumulonimbus;
    layers[2].altitude = 900.0f; layers[2].thickness = 6000.0f; layers[2].coverage = 0.3f;
    ctrl->setLayers(layers);
    mod->onFrame(f);
    CHECK(ctrl->getLayers().size() == 3, "setLayers 三层生效");

    CHECK(query->cloudBase(base) && std::abs(base - 900.0f) < 1.0f,
          "多层云底 = 最低层 (900m)");
    CHECK(query->cloudTop(top) && top > 8000.0f,
          "多层云顶 = 卷云层顶 (>8000m)");
    std::printf("    多层: 云底 %.0fm 云顶 %.0fm 总云量 %.2f\n",
                base, top, query->totalCoverage());

    // 层开关。
    ctrl->setLayerEnabled(0, false);
    mod->onFrame(f);
    float base2 = 0;
    query->cloudBase(base2);
    CHECK(std::abs(base2 - 900.0f) < 1.0f, "关卷云层后云底不变 (最低仍 900m)");
    // 关积雨云层并恢复卷云: 云顶应由卷云决定 (8000m + 厚 600 → 8600)。
    ctrl->setLayerEnabled(0, true);
    ctrl->setLayerEnabled(2, false);
    mod->onFrame(f);
    float top2 = 0;
    query->cloudTop(top2);
    std::printf("    关积雨云后云顶 %.0fm\n", top2);
    CHECK(std::abs(top2 - 8600.0f) < 1.0f, "关积雨云后云顶 = 卷云层顶");
    ctrl->setLayerEnabled(2, true);

    // 逐层覆盖率。
    ctrl->setLayerCoverage(1, 0.9f);
    mod->onFrame(f);
    CHECK(std::abs(ctrl->getLayers()[1].coverage - 0.9f) < 1e-3f,
          "setLayerCoverage 生效");

    // ---- 4. 全局云量调制 ----
    const float totalHigh = query->totalCoverage();
    ctrl->setCoverage(0.1f);
    mod->onFrame(f);
    const float totalLow = query->totalCoverage();
    CHECK(totalLow < totalHigh, "全局云量调低 → 总云量下降");
    ctrl->setCoverage(1.0f);
    mod->onFrame(f);
    CHECK(query->totalCoverage() > totalLow, "全局云量恢复 → 总云量回升");

    // ---- 5. 云影行为 ----
    // 高覆盖 → 阴影更强 (shadowFactor 更小)。
    ctrl->setLayers(layers);
    ctrl->setCoverage(1.0f);
    ctrl->setLayerCoverage(0, 1.0f);   // 层云满覆盖
    layers = ctrl->getLayers();
    layers[0].type = CloudLayerType::Stratus;   // 改为层云 (sheet 类型才投影)
    layers[0].altitude = 2000.0f;
    ctrl->setLayers(layers);
    mod->onFrame(f);
    const float sfHigh = query->shadowFactor(500.0, 500.0, 0.9);
    ctrl->setCoverage(0.05f);
    mod->onFrame(f);
    const float sfLow = query->shadowFactor(500.0, 500.0, 0.9);
    std::printf("    云影: 满覆盖 %.3f vs 近无云 %.3f\n", sfHigh, sfLow);
    CHECK(sfHigh < sfLow, "高覆盖云影更强");
    CHECK(sfLow > 0.9f, "近无云时云影≈1");
    // 太阳低角度 → 投影退化 (因子→1)。
    ctrl->setCoverage(1.0f);
    mod->onFrame(f);
    const float sfLowSun = query->shadowFactor(500.0, 500.0, 0.01);
    CHECK(sfLowSun >= 0.99f, "太阳近地平线不投影 (因子=1)");

    // ---- 6. P3-4b 体积云 (ray-march 路径) ----
    // 体积云开启后, 积云/积雨云走 3D ray-march; 此处验证模块可创建、
    // 查询接口可用、coverageAt 返回合理密度 (0..1) 且不破坏现有契约。
    {
        CloudParams vp;
        vp.cover = 0.5f;
        vp.volume = true;
        auto vmod = createCloudModule(vp);
        CHECK(vmod != nullptr, "createCloudModule (volume=true) 成功");
        CHECK(vmod->rootNode() != nullptr, "体积云 rootNode 非空");
        auto* vq = dynamic_cast<ICloudQuery*>(vmod.get());
        CHECK(vq != nullptr, "体积云 ICloudQuery 可查询");
        vmod->onFrame(f);
        float vbase = 0, vtop = 0;
        CHECK(vq->cloudBase(vbase), "体积云 cloudBase 有效");
        CHECK(vq->cloudTop(vtop) && vtop > vbase, "体积云 cloudTop > cloudBase");
        // 体积云 coverageAt: 多点采样应返回 0..1, 且满覆盖层中心附近应有云。
        float accum = 0.0f;
        for (int i = 0; i < 8; ++i) {
            accum += vq->coverageAt(double(i) * 150.0, double(i) * 120.0);
        }
        accum /= 8.0f;
        std::printf("    体积云 coverageAt 8 点均值 %.3f\n", accum);
        CHECK(accum >= 0.0f && accum <= 1.0f, "体积云 coverageAt 在 0..1");
        CHECK(vq->totalCoverage() > 0.05f, "体积云 totalCoverage 非零");
        vmod->setEnabled(false);
        CHECK(!vmod->enabled(), "体积云 setEnabled(false) 生效");
        vmod->setEnabled(true);
    }

    // ---- 7. 高分辨率稀疏体素状态与动力学 ----
    {
        SparseCloudConfig cfg;cfg.voxelSizeMetres=40.f;cfg.brickSide=8;cfg.maxBricks=256;
        SparseCloudVolume volume(cfg);
        volume.injectThermal({0,800,0},180.f,.018f,4.f,{3.f,1.f,0.f});
        volume.injectThermal({120,760,40},120.f,.016f,2.f,{-2.f,.5f,1.f});
        const auto seededBricks=volume.activeBrickCount();
        CHECK(seededBricks>0 && seededBricks<cfg.maxBricks,"稀疏云只分配有状态的体素砖");
        const auto beforeRevision=volume.revision();
        CloudDynamics dynamics;dynamics.ambientVapourMixingRatio=.008f;dynamics.ambientWind={5.f,0.f,1.f};
        for(int i=0;i<8;++i) volume.step(.1f,dynamics);
        CHECK(volume.revision()>beforeRevision,"稀疏云动力学推进 revision");
        CHECK(volume.activeVoxelCount()>0,"湿热体素凝结为云水");
        float peakWater=0.f,peakUp=-1e9f,peakCurl=0.f;
        for(int z=-160;z<=160;z+=40)for(int y=640;y<=1000;y+=40)for(int x=-200;x<=240;x+=40) {
            const auto state=volume.sample({float(x),float(y),float(z)});
            peakWater=std::max(peakWater,state.liquidWaterKgM3);
            peakUp=std::max(peakUp,state.velocity.y);
            peakCurl=std::max(peakCurl,std::abs(state.velocity.x)+std::abs(state.velocity.z));
        }
        CHECK(peakWater>0 && peakUp>0 && peakCurl>.05f,"云水、浮力和三维翻滚速度共同演化");
        const auto uploads=volume.consumeDirtyBricks();
        CHECK(!uploads.empty() && uploads.front().density.size()==size_t(cfg.brickSide)*cfg.brickSide*cfg.brickSide,
              "活动砖可打包为 GPU R16 密度图集");
        CHECK(volume.consumeDirtyBricks().empty(),"未变化体素砖不重复上传");
    }

    // ---- 8. 显隐 ----
    {
        VoxelCloudClusterState state;state.id=7;state.baseMetres=420;state.depthMetres=2200;
        state.horizontalRadius=1100;state.vapour=.018f;state.temperatureExcess=4;state.updraft=5;
        VoxelCloudCluster cluster(state,SparseCloudConfig{40,8,300,1e-4f});
        float peak=0,centralPeak=0;size_t occupied=0,samples=0,emptyWithinEnvelope=0;
        for(int z=-1200;z<=1200;z+=80)for(int y=400;y<2800;y+=40)
            for(int x=-1200;x<=1200;x+=80) {
                const float density=cluster.density({float(x),float(y),float(z)});
                peak=std::max(peak,density);occupied+=density>.03f;
                emptyWithinEnvelope+=density<.002f;++samples;
            }
        for(int y=400;y<2800;y+=32)
            centralPeak=std::max(centralPeak,cluster.density({0,float(y),0}));
        CHECK(cluster.volume().activeBrickCount()>0,"每团云拥有独立稀疏体素砖集合");
        CHECK(peak>.05f,"湿热云团形成可解析的三维凝结体");
        CHECK(centralPeak>.05f,"主上升气流保持连续的下降穿云路径");
        CHECK(occupied>80 && emptyWithinEnvelope>samples/3,
              "独立热泡与干空气卷吸形成非层状三维云团和内部间隙");
    }

    // ---- 9. 显隐 ----
    mod->setEnabled(false);
    CHECK(!mod->enabled(), "setEnabled(false) 生效");
    mod->setEnabled(true);
    CHECK(mod->enabled(), "setEnabled(true) 生效");

    std::printf("\n%s: %d 项失败\n", g_fail ? "SMOKE FAILED" : "SMOKE PASSED", g_fail);
    return g_fail ? 1 : 0;
}
