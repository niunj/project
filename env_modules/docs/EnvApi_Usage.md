# rocky_env 接口使用指南（rocky_sky 重点）

> 适用版本：`EnvApi.h` v2（2026-09-17）。全产品线 **rocky 地球基座（ECEF）**
> 单一路径——通过 `#include <rocky_env/EnvApi.h>` 集成。
> 模块只依赖 VSG + rocky，不依赖 Qt / 业务代码。

---

## 1. 三十秒上手：EnvironmentStack（推荐入口）

最简路径都是 `createEnvironmentStack`——一次创建
sky+cloud+ocean，模块内部保证"sky 先算出星历/光照真值，再喂给云海"的单一
真值源时序，宿主不需要关心模块调用顺序：

```cpp
#include <rocky_env/EnvApi.h>

using namespace rockyenv;

// 1) 创建（sky 硬依赖 rocky 上下文 —— 与 MapNode 同一 VSGContextImpl*）
EnvironmentStackParams sp;
sp.sky.rocky_context = rockyContextPtr;   // rocky::VSGContextImpl* (缺失则 sky=nullptr)
sp.sky.year = 2026; sp.sky.month = 9; sp.sky.day = 17; sp.sky.hours_utc = 4.0;
auto stack = createEnvironmentStack(sp);

// 2) 挂载（两个根，缺一个就跳过）
if (auto* w = stack->worldRoot())   worldRoot->addChild(w);    // ECEF 世界根（rocky 天空）
if (auto* c = stack->contentRoot()) contentRoot->addChild(c);  // 锚点 ENU 内容根（云/海）

// 3) 每帧驱动（渲染线程 tick 里）
EnvFrame f;
f.view_proj   = cameraMatrices->viewProj;   // ECEF → 裁剪
f.model       = contentModel;               // 锚点 ENU → ECEF（决定太阳的 ENU 投影）
f.cam_pos_world = cameraMatrices->eyeWorld; // 相机 ECEF 位置
f.cam_pos_enu  = eyeENU;                    // 由 model 逆矩阵求出
f.year = 2026; f.month = 9; f.day = 17; f.hours_utc = 4.0;
stack->onFrame(f);   // 注意参数非 const——栈内会把星历/光照真值覆写回 f

// 4) 能力查询（模板转发，sky 优先）
if (auto* sun = stack->query<ISunProvider>()) { /* 太阳真值 */ }
if (auto* atmo = stack->query<IAtmosphereQuery>()) {
    vsg::vec3 tau, L;
    atmo->transmittance(fromECEF, toECEF, tau, L);   // 传感器链路大气衰减 (ECEF 米制)
}
if (auto* ctrl = stack->query<ISkyControl>()) ctrl->setExposure(1.5f);
```

栈内真值传播（`sky_drives_sun` / `sky_drives_lighting`，默认开）：
`onFrame` 里 sky 先跑，其 `ISunProvider::sunInfo` 与
`ILightingProvider::getAtmosphereState` 结果覆写进 `EnvFrame::sun_dir_enu /
sun_el_deg / sun_light / ambient_light / horizon_color / zenith_color /
is_day / extinction_coeff`，再喂给 cloud / ocean——云影方向、海面耀斑、
雾色与天上太阳位置天然一致，不需要宿主手动同步。

---

## 2. Sky 模块详解（Rocky 单后端）

```cpp
SkyParams p;
p.rocky_context = rockyCtx;          // 与 MapNode 同一 rocky::VSGContextImpl* (缺失 → nullptr)
p.hours_utc = 4.0;                   // 初始 UTC 时刻
p.use_bruneton_lut = true;           // Bruneton LUT 穹顶（默认更快更准）

auto mod = createSkyModule(p);
worldRoot->addChild(mod->rootNode());    // Rocky 后端挂 ECEF 世界根
// 每帧: mod->onFrame(f);（f.model = 锚点 ENU→ECEF，决定太阳的 ENU 投影）
```

要点：

- **锚点随 rocky_map 走**（`f.model` 的 ENU→ECEF 矩阵即观测位置），不提供
  运行时锚点迁移接口；换锚点 = 换 `f.model`（迁移 ENU→ECEF 矩阵即可，
  星历/暮光/月光全部随之更新）。
- 太阳方向真值 = 星历(ECEF) 经 `EnvFrame::model` 投影到当前 ENU 锚点；
  云/海 shader 与场景平行光都应消费 `ISunProvider::sunInfo(f.model, ...)`
  的输出，禁止另算一套。
- 月盘为相机跟随公告板，月相/月光由 `AtmosphereState` 下发。
- `transmittance` 坐标恒为 **ECEF 米制**（锚点 ENU 查询点先乘 `f.model`），
  高度按地心距 − 6371 km。
- 场景平行光无需自建：后端自带 sun/moon/ambient 灯 rig，颜色强度随大气
  质量/月相物理演化（`applyLights`）。

## 3. 能力接口速查

| 接口 | 获取方式 | 用途 |
|---|---|---|
| `IEnvModule` | 工厂返回值 | 生命周期 / rootNode / onFrame / 显隐 |
| `ISunProvider` | `dynamic_cast` 或 `stack->query<>()` | 星历太阳方向/高度角/方位角（单一真值源） |
| `ILightingProvider` | 同上 | `AtmosphereState`：直射/环境/月光色、天顶地平线色、昼夜因子、曝光 |
| `IAtmosphereQuery` | 同上 | 任意两点视线透过率 + 路径辐射（RT/传感器链路） |
| `ISkyControl`（v2 新增） | 同上 | 运行时：曝光、浊度、日/月/大气开关（get/set 成对） |
| `IEnvStack`（v2 新增） | `createEnvironmentStack` | 聚合栈：双根挂载、真值传播、能力转发 |

## 4. EnvFrame 填写清单（宿主每帧）

必填：`view_proj / model / cam_pos_world / cam_pos_enu / time_sec`、
`year / month / day / hours_utc`（+`time_scale` 推进）。
气象：`wind_dir_enu / wind_speed / cloud_cover / visibility / precipitation`。
`sun_dir_enu / sun_el_deg / sun_light / ...` 等"光照段"手填值仅在**无 sky
模块**时生效；栈内或宿主侧有 sky 时应让真值源覆写。

## 5. 已验证

`env_modules/tests/smoke_sky.cpp`（41 项断言，全 PASS）覆盖：Rocky 单后端
创建（缺 rocky_context 拒绝）、星历正午/夜间/暮光（偏红验证）、
`IAtmosphereQuery` 高低空透过率（ECEF 契约）、显隐、`ISkyControl` 全参数、
锚点迁移（换 `f.model` ENU→ECEF：台北→新加坡）、EnvStack 聚合与真值覆写
链路。

## 6. rocky_cloud（v4 多层语义化）

```cpp
// 多层配置（卷云 + 积云 + 积雨云）
CloudParams p;
p.layers = {
    {CloudLayerType::Cirrus,        8000.f, 800.f, 0.4f, 0.25f},
    {CloudLayerType::Cumulus,       1200.f, 600.f, 0.6f, 0.45f},
    {CloudLayerType::Cumulonimbus,   900.f, 6000.f, 0.3f, 0.6f},
};
auto mod = createCloudModule(p);
contentRoot->addChild(mod->rootNode());
// 每帧 mod->onFrame(f) —— 降水(precipitation)自动驱动云体变暗/雨幡
```

- `CloudParams::layers` 为空 = 一期单层兼容模式（层云+积云环，行为不变）。
- `ICloudControl`（dynamic_cast）：全局云量 `setCoverage`、`setLayers` 整体
  替换、逐层覆盖/开关（运行时生效）。
- `ICloudQuery`（dynamic_cast）：云底/云顶高度、总云量、**`shadowFactor(ex, ez,
  sun_el)` 云影因子**——CPU 云量图与 `cloud_sheet.frag` 的 fbm 逐公式同源，
  云影下的光照/探测仿真可直接消费；海面 shader 已内置同一云影。
- 类型化成形：卷云（风向拉伸薄纱）/层积云（高频团块）/积雨云（塔身+砧状顶+
  雨幡）/银边（背光云缘发亮）。`env_smoke_cloud` 25/25 PASS。
- 下期（预留）：体积云 ray-march（P3-4b，需半分辨率 pass 基建）。

## 7. 能力接口速查（全模块）

| 模块 | 控制接口 | 采样/查询接口 |
|---|---|---|
| sky | `ISkyControl` | `ISunProvider` / `ILightingProvider` / `IAtmosphereQuery` |
| ocean | `IOceanControl` | `IOceanSampler` |
| cloud | `ICloudControl` | `ICloudQuery` |
| 全部 | — | `IEnvStack::query<T>()` 聚合转发 |

## 8. rocky_ocean（S1/S2 波场版）

创建与常规用法：

```cpp
OceanParams p;
p.wind_speed = 10.0f;          // 风速 → JONSWAP 自动派生 Hs/Tp/蒲福级
p.fetch_m = 250000.0f;         // 风区; p.harmonics = 8; p.choppy = 0.65f
auto mod = createOceanModule(p);
contentRoot->addChild(mod->rootNode());
// 每帧 mod->onFrame(f) —— 波场跟随 EnvFrame 的风速/风向自动演化
```

- `auto_sea_state=true`（默认）：波高全自动（风速/风区/风时 → JONSWAP）；
  手动想定 `setWaveHeight(2.5)` 锁定、`setWaveHeight(0)` 恢复自动。
- `IOceanControl`（dynamic_cast）：海平面/波高/波长/风/水色/边界运行时可调。
- `IOceanSampler`（dynamic_cast，onFrame 后有效）：**CPU 与 GPU 画面同参数
  同公式**（所见即所采）——舰船浮力用 `waveHeight/orbitalVelocity`，雷达
  海杂波用 `seaState/foamCoverage`，红外海面发射率用 `waveNormal`。样例：
  U10=10 m/s → Hs 1.38 m / Tp 8.2 s / 蒲福 5 级。
- 波形真值源为 `WaveField`（JONSWAP 谱 → Gerstner 谐波表），经 UBO
  `oceanWave[16]` 下发 shader；改海况零重建。`env_smoke_ocean` 47/47 PASS。
- **S2（2026-09-17）已落地**：涌浪叠加（`setSwell(h, L, dir)`，总 Hs=√(风浪²+涌浪²)）、
  方向相关天空反射 + GGX 太阳耀斑 + 次表面散射（`reflection` 档位 0/1）、
  球面曲率（`curvature`，远海沉入地平线）、legacy 波形模式（`wave_mode=0` 向后兼容）。
- S3+（预留）：真·planar reflection 离屏 RT（`reflection=2`）、GPU FFT 波谱
  （`wave_mode=2`），需宿主 RenderGraph / compute 基建。
  设计文档：`env_modules/docs/Ocean_Module_Design.md`。
