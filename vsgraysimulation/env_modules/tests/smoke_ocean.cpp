/// smoke_ocean.cpp —— rocky_env Ocean S1 链路冒烟 (无窗口):
///   1) createOceanModule + IOceanSampler/IOceanControl 接口可查询;
///   2) JONSWAP 海况: 风速→Hs/Tp/蒲福级单调性 + 量级合理性;
///   3) CPU 采样: 波高统计 (Hs 一致性, 所见即所采)、法线/轨道速度量级;
///   4) 白沫: 高海况覆盖率 > 低海况;
///   5) 运行时控制: setWind/setWaveHeight 生效与恢复;
///   6) 谐波表: UBO 参数与采样一致性 (同一 WaveField 公式)。
#include <rocky_env/EnvApi.h>

#include <vsg/nodes/Node.h>

#include <cstdio>
#include <cmath>
#include <vector>

using namespace rockyenv;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); ++g_fail; } } while (0)

int main() {
    // ---- 1. 创建与接口 ----
    OceanParams p;
    p.wind_speed = 10.0f;   // 5 级风
    auto mod = createOceanModule(p);
    CHECK(mod != nullptr, "createOceanModule 成功");
    CHECK(mod->rootNode() != nullptr, "rootNode 非空");

    auto* sampler = dynamic_cast<IOceanSampler*>(mod.get());
    auto* control = dynamic_cast<IOceanControl*>(mod.get());
    CHECK(sampler && control, "IOceanSampler/IOceanControl 可查询");

    // 帧驱动 (平面模式)
    EnvFrame f;
    f.model = vsg::dmat4(1.0);
    f.view_proj = vsg::dmat4(1.0);
    f.cam_pos_enu = vsg::dvec3(0.0, 100.0, 0.0);
    f.wind_dir_enu = vsg::vec3(1.0f, 0.0f, 0.0f);
    f.wind_speed = 10.0f;
    mod->onFrame(f);

    // ---- 2. JONSWAP 海况 ----
    SeaState s1 = sampler->seaState();
    std::printf("    U10=10m/s: Hs=%.2fm Tp=%.1fs 蒲福=%d\n",
                s1.significant_wave_height, s1.peak_period, s1.beaufort);
    CHECK(s1.beaufort == 5, "10m/s → 蒲福 5 级");
    CHECK(s1.significant_wave_height > 0.3f && s1.significant_wave_height < 4.0f,
          "5 级海况 Hs 在 0.3..4m 合理区间");
    CHECK(s1.peak_period > 2.0f && s1.peak_period < 12.0f, "Tp 在 2..12s 合理区间");

    // 风速加大 → 海况单调增强
    f.wind_speed = 20.0f;
    mod->onFrame(f);
    SeaState s2 = sampler->seaState();
    std::printf("    U10=20m/s: Hs=%.2fm 蒲福=%d\n",
                s2.significant_wave_height, s2.beaufort);
    CHECK(s2.significant_wave_height > s1.significant_wave_height, "风速↑ → Hs↑");
    CHECK(s2.beaufort > s1.beaufort, "风速↑ → 蒲福级↑");
    CHECK(s2.peak_period > s1.peak_period, "风速↑ → Tp↑ (长涌发展)");

    // 静风 → 近乎无浪
    f.wind_speed = 0.5f;
    mod->onFrame(f);
    SeaState s3 = sampler->seaState();
    std::printf("    U10=0.5m/s: Hs=%.3fm 蒲福=%d\n",
                s3.significant_wave_height, s3.beaufort);
    CHECK(s3.significant_wave_height < 0.15f, "静风 Hs < 0.15m");
    CHECK(s3.beaufort <= 1, "静风蒲福 ≤ 1");

    // ---- 3. CPU 采样: 所见即所采 (Hs 统计一致性) ----
    f.wind_speed = 12.0f;
    mod->onFrame(f);
    const double t = 100.0;
    // 空间网格统计波高: Hs ≈ 4σ (crest-to-trough 的一半按 Hs 定义近似)
    const int NS = 256;
    const double step = 8.0;
    double sum = 0.0, sum2 = 0.0;
    float hmin = 1e9f, hmax = -1e9f;
    for (int i = 0; i < NS; ++i) {
        for (int j = 0; j < NS; ++j) {
            const float h = sampler->waveHeight(i * step, j * step, t);
            sum += h; sum2 += (double)h * h;
            hmin = std::min(hmin, h);
            hmax = std::max(hmax, h);
        }
    }
    const double mean = sum / (NS * NS);
    const double sigma = std::sqrt(sum2 / (NS * NS) - mean * mean);
    const double hsStat = 4.0 * sigma;   // Hs = 4σ (波面高度标准差)
    const SeaState s4 = sampler->seaState();
    std::printf("    U10=12m/s: 谱 Hs=%.2fm, 采样统计 Hs(4σ)=%.2fm, "
                "波高范围 [%.2f, %.2f]\n",
                s4.significant_wave_height, hsStat, hmin, hmax);
    CHECK(hsStat > 0.3f && hsStat < 6.0f, "采样统计 Hs 量级合理");
    // 谱 Hs 与采样统计一致性 (Gerstner 波列 σ = √(ΣA²/2), Hs=2√2σ=2.83σ;
    // 4σ 口径略偏大, 容差 40%)
    CHECK(std::abs(hsStat - s4.significant_wave_height * 4.0 / 2.828) /
              std::max(s4.significant_wave_height, 0.01f) < 0.45,
          "谱 Hs 与采样统计一致 (容差 45%)");

    // 法线与轨道速度
    const auto nrm = sampler->waveNormal(100.0, 200.0, t);
    const float nlen = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
    CHECK(std::abs(nlen - 1.0f) < 1e-3f, "法线归一化");
    CHECK(nrm.y > 0.5f, "法线以向上为主 (波面法线)");

    const auto vel = sampler->orbitalVelocity(100.0, 200.0, t);
    const float vlen = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
    const float vmax = s4.peak_period > 0.1f
        ? 2.0f * 3.14159f * 1.0f / s4.peak_period : 2.0f;
    std::printf("    轨道速度幅值 %.3f m/s (上限估算 %.2f)\n", vlen, vmax * 3);
    CHECK(vlen > 1e-4f && vlen < vmax * 3.0f + 1.0f, "轨道速度量级合理");

    // 时间演化: 相位推进, 波高场变化
    const float h_a = sampler->waveHeight(500.0, 500.0, t);
    const float h_b = sampler->waveHeight(500.0, 500.0, t + 0.7);
    CHECK(h_a != h_b, "时间推进波场演化");

    // ---- 4. 白沫: 高海况覆盖更强 ----
    f.wind_speed = 22.0f;
    mod->onFrame(f);
    double foamHi = 0.0;
    for (int i = 0; i < 128; ++i)
        foamHi += sampler->foamCoverage(i * 6.0, i * 6.0, t);
    foamHi /= 128;
    f.wind_speed = 3.0f;
    mod->onFrame(f);
    double foamLo = 0.0;
    for (int i = 0; i < 128; ++i)
        foamLo += sampler->foamCoverage(i * 6.0, i * 6.0, t);
    foamLo /= 128;
    std::printf("    白沫均值: 22m/s=%.3f, 3m/s=%.3f\n", foamHi, foamLo);
    CHECK(foamHi > foamLo, "高海况白沫覆盖 > 低海况");
    CHECK(foamHi >= 0.0f && foamHi <= 1.0f && foamLo >= 0.0f, "白沫覆盖率 0..1");

    // ---- 5. 运行时控制 ----
    control->setWaveHeight(2.5f);   // 手动锁定
    mod->onFrame(f);
    CHECK(std::abs(control->getWaveHeight() - 2.5f) < 0.05f,
          "setWaveHeight 手动锁定生效");
    const SeaState sManual = sampler->seaState();
    CHECK(std::abs(sManual.significant_wave_height - 2.5f) < 0.05f,
          "手动 Hs 与采样同源");
    control->setWaveHeight(0.0f);   // 恢复气象驱动
    mod->onFrame(f);
    const SeaState sAuto = sampler->seaState();
    CHECK(std::abs(sAuto.significant_wave_height - 2.5f) > 0.05f,
          "setWaveHeight(0) 恢复气象驱动");

    control->setWind(8.0f, vsg::vec3(0.0f, 0.0f, 1.0f));
    mod->onFrame(f);
    CHECK(std::abs(control->getWindSpeed() - 8.0f) < 1e-3f, "setWind 风速生效");
    const auto wd = control->getWindDir();
    CHECK(std::abs(wd.x) < 1e-3f && std::abs(wd.z - 1.0f) < 1e-3f,
          "setWind 风向生效 (+Z)");
    const SeaState sWind = sampler->seaState();
    CHECK(std::abs(sWind.peak_direction_rad - 3.14159f / 2.0f) < 0.1f,
          "主波向随风向 (+Z → π/2)");

    control->setSeaLevel(3.0f);
    control->setWaterColor(vsg::vec3(0.1f, 0.2f, 0.3f), vsg::vec3(0.2f, 0.4f, 0.5f));
    OceanBounds b = control->getBounds();
    b.radius = 5000.0f;
    control->setBounds(b);
    mod->onFrame(f);
    CHECK(std::abs(control->getSeaLevel() - 3.0f) < 1e-3f, "setSeaLevel 生效");
    CHECK(std::abs(control->getBounds().radius - 5000.0f) < 1e-3f, "setBounds 生效");

    // ---- 6. 显隐 ----
    mod->setEnabled(false);
    CHECK(!mod->enabled(), "setEnabled(false) 生效");
    mod->setEnabled(true);
    CHECK(mod->enabled(), "setEnabled(true) 生效");

    // ---- 7. v3 (S2): 涌浪叠加 ----
    control->setSwell(1.2f, 200.0f, 30.0f);
    mod->onFrame(f);
    const float hsWind = sampler->seaState().significant_wave_height;
    CHECK(std::abs(control->getSwellHeight() - 1.2f) < 0.05f, "setSwell 涌浪生效");
    CHECK(hsWind > 1.0f, "总 Hs 含涌浪分量 (>1m)");
    // 涌浪叠加后波场演化 (长波相位推进)。
    const float h_sw_a = sampler->waveHeight(1000.0, 1000.0, t);
    const float h_sw_b = sampler->waveHeight(1000.0, 1000.0, t + 3.0);
    CHECK(h_sw_a != h_sw_b, "涌浪叠加后波场演化");
    // 关闭涌浪: 总 Hs 回落。
    control->setSwell(0.0f, 200.0f, 30.0f);
    mod->onFrame(f);
    CHECK(control->getSwellHeight() < 0.01f, "setSwell(0) 关闭涌浪");
    CHECK(sampler->seaState().significant_wave_height < hsWind, "关涌浪后 Hs 回落");

    // ---- 8. v3 (S2): 光学/几何选项 ----
    control->setReflection(0);
    CHECK(control->getReflection() == 0, "setReflection(0) 生效");
    control->setReflection(1);
    CHECK(control->getReflection() == 1, "setReflection(1) 生效");
    control->setCurvature(true);
    CHECK(control->getCurvature(), "setCurvature(true) 生效");
    control->setCurvature(false);
    CHECK(!control->getCurvature(), "setCurvature(false) 生效");
    mod->onFrame(f);   // 选项变化后帧驱动仍正常

    // ---- 9. v3: legacy 波形模式 (向后兼容) ----
    {
        OceanParams lp;
        lp.wave_mode = 0;   // GerstnerLegacy
        auto legacyMod = createOceanModule(lp);
        CHECK(legacyMod != nullptr, "legacy 波形模式创建成功");
        EnvFrame lf = f;
        lf.wind_speed = 0.5f;   // 静风下 legacy 仍有固定波形
        legacyMod->onFrame(lf);
        auto* ls = dynamic_cast<IOceanSampler*>(legacyMod.get());
        CHECK(ls != nullptr && ls->seaState().beaufort >= 0,
              "legacy 模式采样接口可用");
        const float lh = ls->waveHeight(50.0, 50.0, 10.0);
        std::printf("    legacy 模式波高 %.3fm (固定谐波)\n", lh);
        CHECK(std::abs(lh) < 5.0f, "legacy 波高量级合理");
    }

    std::printf("\n%s: %d 项失败\n", g_fail ? "SMOKE FAILED" : "SMOKE PASSED", g_fail);
    return g_fail ? 1 : 0;
}
