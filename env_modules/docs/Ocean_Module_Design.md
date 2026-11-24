# rocky_ocean 产品级设计（S1 落地版）

> 基线：`env_modules/src/OceanModule.cpp`（2026-09-17 审计）。对标 Sundog Triton。
> 本文档记录 S1（波场真值源 + 仿真采样闭环）的完整设计，及 S2+ 的演进接口。

---

## 0. 一句话定位

海面不再是一个"效果贴片"，而是一个**物理波场**：CPU 侧一份波谱真值，
GPU 渲染与 CPU 仿真采样同参数、同公式；海况由气象（风速/风区/风时）驱动，
想定编辑器可运行时调节。

## 1. 架构（S1）

```
EnvFrame (wind_speed / wind_dir / time)
        │
        ▼
WaveField (src/WaveField.h, 纯 C++, 零 VSG 依赖)
  ├─ JONSWAP 派生: 风速/风区/风时 → Hs(有效波高) / Tp(谱峰周期) / 蒲福级
  ├─ 谐波表生成: 8 个 Gerstner 谐波 (波长/振幅/角度) —— 频谱离散化
  ├─ CPU 求值: waveHeight / waveNormal / orbitalVelocity / jacobianFoam
  │    (与 ocean.vert **逐公式一致**, 保证所见即所采)
  └─ 手动 override: wave_height 直接指定时锁定振幅、仅用谱分布形状
        │
        ├─► UBO offset 92..123 (oceanWave 段, 8×vec4) ──► ocean.vert/frag
        │    (shader 不再有写死谐波; 改波场 = 改 UBO, 零重建)
        └─► IOceanSampler (EnvApi.h) ──► 雷达海杂波 / 舰船浮力 / 红外海面
```

## 2. 公共 API（EnvApi.h 追加，向后兼容）

```cpp
struct SeaState {          // 海况概况 (与渲染同源)
    float significant_wave_height;  // Hs (m)
    float peak_period;              // Tp (s)
    int   beaufort;                 // 蒲福风级
    float peak_direction_rad;       // 主波向 (ENU, 数学角)
};

class IOceanSampler {      // CPU/GPU 同源采样 (仿真侧)
    virtual SeaState seaState() const = 0;
    virtual float    waveHeight(double ex, double ez, double t) const = 0;
    virtual vsg::vec3 waveNormal(double ex, double ez, double t) const = 0;
    virtual vsg::vec3 orbitalVelocity(double ex, double ez, double t) const = 0;
    virtual float    foamCoverage(double ex, double ez, double t) const = 0;
};

class IOceanControl {      // 运行时控制 (想定编辑器/海况面板)
    virtual void setSeaLevel(float) = 0;
    virtual void setWaveHeight(float) = 0;     // 手动 override (0 = 回到气象驱动)
    virtual void setWaveLength(float) = 0;
    virtual void setWind(float speed, vsg::vec3 dir) = 0;
    virtual void setWaterColor(vsg::vec3 deep, vsg::vec3 shallow) = 0;
    virtual void setBounds(const OceanBounds&) = 0;
    // get 成对
};

struct OceanParams {       // 追加字段 (既有字段不动)
    float fetch_m  = 250000.0f;   // 风区 (JONSWAP)
    float duration = 10800.0f;    // 风时
    int   harmonics = 8;          // 谐波数 (4..16)
    float choppy    = 0.65f;      // Gerstner Q
    bool  auto_sea_state = true;  // 风速驱动波高; false=用 wave_height
};
```

## 3. UBO 布局扩展（只追加）

| offset | 段 | 内容 |
|---|---|---|
| 92..123 | `oceanWave[8]` (vec4×8) | x=波长 L, y=振幅 A, z=cos(θ), w=sin(θ)；θ 为绕风向的展开角 |
| 84..91 | （保留给 sky） | moon/astro |

`kEnvUboFloats: 92 → 124`。非海洋 shader 声明块止于 91，多余字节无害。

## 4. 波物理

- **JONSWAP**：`S(f) = α g² (2π)^-4 f^-5 exp[-1.25 (fp/f)^4] γ^r`，
  α 由风速 U10/风区 X/风时 t 按标准经验式求；`Hs = 4√m0`、`Tp = 1/fp`。
- **蒲福级**：标准风速区间表 (0..12 级)。
- **谐波离散**：波数空间对数均布 8 档（长波→短波），振幅按谱能量分配后
  归一化到目标 Hs（Gerstner 波列 Hs ≈ 2√2·σ，σ=√(ΣA²/2)）。
- **白沫**：Gerstner 雅可比 `J = 1 - Σ Q·k·A·sin(φ)`，J < 阈值(≈0.85) 处
  波面收敛→破碎；叠加风成覆盖率 `0.0035·(U10-6.5)` 与岸线环带浪花
  （水深 < 3m 加强）。
- **轨道速度**：深水线性波 `u = Σ A·ω·d·cos(φ)`（水平）、
  `v = Σ A·ω·sin(φ)`（垂向）。

## 5. S2 落地内容（2026-09-17，冒烟 47/47 PASS）

| 功能 | 实现 |
|---|---|
| **涌浪叠加** | `WaveField::addSwell`：3 个长波谐波（±8° 窄方向），独立于风场不衰减；总 Hs=√(风浪²+涌浪²)；`IOceanControl::setSwell(h, L, dir)` 运行时开关 |
| **方向相关天空反射** | `skyProbe`：反射向量仰角梯度 + 太阳方位侧地平线亮带（暮光亮侧偏暖）；粗糙度（蒲福级+距离）调制掠射菲涅尔权重 |
| **GGX 太阳耀斑** | `ggxD(ndh, rough)`，粗糙度 = 基础 0.06 + 蒲福项 + 距离项（远处波不可分辨） |
| **次表面散射** | 背光波峰透光（青绿增亮）：对射角³ × 峰值因子 × 浅水透明度，仅昼间 |
| **球面曲率** | vert 中 `pos.y -= d²/2R`：远海沉入地平线，`OceanParams::curvature` 开关 |
| **16 谐波 + legacy 模式** | UBO `oceanWave[16]`(92..155)+`oceanOpt`(156..159)；`wave_mode=0` 复刻 Phase 1 的 4 写死谐波（向后兼容） |
| **降水消光** | frag 能见度 × (1−0.6·降水强度) |

## 6. S3+ 演进接口（预留，未实现）

| 期 | 内容 | 入口 |
|---|---|---|
| S3 | planar reflection（离屏 RT 反射真实天空/地形/云；需宿主 RenderGraph 基建） | `OceanParams::reflection=2` |
| S4 | GPU compute FFT 方向谱（WaveField 为谱参数源；需 compute 管线基建） | `OceanParams::wave_mode=2` |
| S5 | 投影网格 / 破碎浪 / 船行尾迹 | — |
