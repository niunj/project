# 场景配置说明

本目录包含 vsgraysimulation 的场景配置文件示例。

## 文件列表

### 1. tank_platoon_recon.json - 坦克编队侦察场景

**场景描述**: 双坦克编队在山地地形执行侦察任务

**配置内容**:
- **对象**: 2辆 M1A2 坦克 (tank_001, tank_002)，山地地形，地面网格
- **传感器**:
  - 传感器 1: X波段雷达 (tank_001 平台)
  - 传感器 2: X波段雷达 (tank_002 平台)
  - 传感器 3: LWIR 红外相机 (tank_001 平台)
  - 传感器 4: C波段 SAR (空中平台)
- **环境**: 标准大气，晴朗天气
- **仿真**: 300帧，约5秒

**运行命令**:
```bash
# 验证配置
./build/vsgraysim_app --mode validate \
  --object config/scenarios/tank_platoon_recon.json

# 列出传感器信息
./build/vsgraysim_app --mode list-sensors \
  --object config/scenarios/tank_platoon_recon.json

# 单传感器追踪测试
./build/vsgraysim_app --mode trace-sensor \
  --object config/scenarios/tank_platoon_recon.json \
  --sensor-id 1 --frames 30

# 无头模式运行
./build/vsgraysim_app --mode headless \
  --object config/scenarios/tank_platoon_recon.json \
  --frames 100

# GUI模式运行
./build/vsgraysim_app --mode run \
  --object config/scenarios/tank_platoon_recon.json \
  --frames 300
```

**预期结果**:
- 两辆坦克以 11.3 m/s (≈40 km/h) 的速度向东北方向运动
- 雷达实时跟踪运动目标，验证多普勒频移
- SAR 生成坦克编队的条带图像
- 红外相机捕捉坦克热特征

---

### 2. desert_radar_test.json - 沙漠环境雷达测试

**场景描述**: 测试雷达在沙尘环境下的杂波抑制与穿透性能

**配置内容**:
- **对象**: 1辆 M1A2 坦克 (静态目标)，沙漠地形
- **传感器**:
  - 传感器 1: X波段雷达 (高功率，窄波束)
  - 传感器 2: C波段 SAR (空中平台)
  - 传感器 3: LWIR 红外相机
- **环境**: 沙尘大气，地面温度 50°C，能见度 8 km
- **仿真**: 150帧，约2.5秒

**运行命令**:
```bash
# 验证配置
./build/vsgraysim_app --mode validate \
  --object config/scenarios/desert_radar_test.json

# 运行测试
./build/vsgraysim_app --mode headless \
  --object config/scenarios/desert_radar_test.json \
  --frames 150
```

**预期结果**:
- 雷达在沙尘环境下检测静态坦克目标
- SAR 测试穿透沙尘的能力
- 红外相机捕捉高温环境下的目标对比度
- 分析杂波抑制效果

---

## 场景设计流程

### 1. 定义场景目标
明确仿真目的：
- 传感器性能测试
- 多目标跟踪验证
- 环境影响研究
- 物理运动仿真

### 2. 选择配置模板
根据目标选择合适的场景类型：
- 单目标测试 → 简单配置
- 多目标战术 → 复杂对象配置
- 环境测试 → 强化大气配置
- 互扰研究 → 强化传感器配置

### 3. 配置对象
根据现有模型资源配置：
- `models/M1A2/M1A2.obj` - M1A2 坦克
- `models/AGM-114/AGM-114.obj` - AGM-114 导弹
- `models/Mountain/mountain.obj` - 山地地形
- `models/Desert/Desert.obj` - 沙漠地形
- `models/Taipei/Taipei.obj` - 城市地形
- `models/ground_grid.gltf` - 基础地面网格

### 4. 配置传感器
选择传感器类型和参数：
- **雷达**: X/C/L 波段，功率、波束宽度、PRF
- **SAR**: C 波段，空中平台，条带宽度
- **红外**: LWIR/MWIR，视场角
- **激光**: 波长、功率、脉冲率
- **可见光**: 视场角、分辨率

### 5. 配置环境
设置大气和天气参数：
- 标准大气 / 沙尘 / 云层
- 温度、湿度、气溶胶浓度
- 湍流强度、云密度

### 6. 验证运行
按照检查清单验证：
```bash
# 1. 参数验证
./build/vsgraysim_app --mode validate --object your_scenario.json

# 2. 传感器列表
./build/vsgraysim_app --mode list-sensors --object your_scenario.json

# 3. 单传感器测试
./build/vsgraysim_app --mode trace-sensor --object your_scenario.json --sensor-id 1 --frames 10

# 4. 小帧数测试
./build/vsgraysim_app --mode headless --object your_scenario.json --frames 10
```

---

## 配置文件说明

### 对象配置 (objects)
```json
{
  "object_id": "unique_id",           // 对象唯一标识
  "mesh_path": "models/...",          // 模型文件路径
  "face_count": 10000,                // 面数
  "temp_update_rate": 10,             // 动态温度更新频率(Hz)
  "physics": { /* 物理参数 */ },
  "face_groups": [ /* 面组配置 */ ]
}
```

### 传感器配置 (sensors)
```json
{
  "sensor_id": 1,                     // 传感器ID
  "type": "radar|sar|ir|laser|vis",   // 类型
  "band": "X|C|LWIR|...",             // 频段
  "platform_object": "object_id",     // 挂载平台(可选)
  "tx_params": { /* 发射参数 */ },
  "aperture": { /* 孔径配置 */ },
  "rx_cross_sensors": [ /* 互扰配置 */ ]
}
```

### 大气配置 (atmosphere)
```json
{
  "layers": [
    {
      "height_min": 0,
      "height_max": 11000,
      "temp": 288.15,
      "aerosol": 1.0e-5,
      "turbulence": 1.0e-14,
      "cloud_density": 0.0
    }
  ]
}
```

---

## 注意事项

### 1. 模型路径
确保模型文件存在于以下位置之一：
- `models/` 目录
- `build/models/` 目录
- 配置文件中指定的路径

### 2. 物理参数
- 动态对象必须设置 `mass > 0`
- 静态对象设置 `mass = 0` 且无初速度
- 物理驱动对象需要 `physics_enabled: true`

### 3. 传感器平台挂载
- 地面平台使用 `platform_object` 指定对象ID
- 空中平台使用 `platform_type: "airborne"`
- 孔径位置为偏移量(有平台)或绝对坐标(无平台)

### 4. 面组范围
确保 `face_range[1] <= face_count`，避免越界错误。

### 5. 性能考虑
- 测试场景建议帧数 ≤ 100
- 生产场景建议帧数 ≤ 500
- 传感器数量建议 ≤ 10
- 避免过多面组划分

---

## 相关文档

- [场景设计指南](../../docs/scene_design_guide.md) - 完整的场景设计方法论
- [快速参考](../../docs/scene_quick_reference.md) - 配置模板和参数范围
- [整体架构](../../docs/design_overall_architecture.md) - 系统架构说明
- [物理引擎设计](../../docs/design_physics_and_processing.md) - 物理引擎详细设计

---

**维护者**: vsgraysimulation 开发团队
**更新日期**: 2026-07-31