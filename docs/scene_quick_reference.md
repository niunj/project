# 场景设计快速参考

## 1. 场景配置文件结构

### 1.1 完整场景配置
```json
{
  "scenario_name": "场景名称",
  "description": "场景描述",
  "version": "1.0",
  "geo_anchor": {          // ENU 原点 (WGS84); 锚点唯一来源,
    "lat": 25.078,         // 加载场景时 rocky_map 重锚到此点
    "lon": 121.772,
    "alt": 0.0,
    "heading_deg": 0.0
  },
  "terrain_ref": "",       // 地形工程 (.ter/.vsgterrain) | rocky 地形包
                           // (.map.json) | 空 = 仅 rocky 地球粗底图
                           // (OBJ 地表模型放 objects[], type="terrain")
  "ground": {              // 传感器地面材质缺省 (terrain 对象面组命中时覆盖)
    "temp_k": 290.0,
    "emissivity": 0.92
  },
  "ocean": {               // 可选: 海面场景声明
    "enabled": false,
    "sea_temp_k": 291.0,
    "sea_level": 0.0,
    "extent": 0.0          // 0 = 随场景对象分布自适应
  },
  "objects": [ /* 对象列表 */ ],
  "sensors": { /* 传感器配置 */ },
  "atmosphere": { /* 大气环境 */ },
  "simulation": { /* 仿真参数 */ }
}
```

### 1.2 对象配置模板
```json
{
  "object_id": "unique_id",
  "mesh_path": "models/path/to/model.obj",
  "face_count": 10000,
  "temp_update_rate": 10,  // 动态温度更新频率(Hz), 0=静态
  "physics": {
    "mass": 1000.0,  // 质量(kg), 0=静态
    "initial_position": [x, y, z],
    "initial_rotation_deg": [rx, ry, rz],
    "initial_velocity": [vx, vy, vz],
    "restitution": 0.1,  // 弹性系数
    "friction": 0.8,     // 摩擦系数
    "convex_hull": true  // 使用凸包碰撞
  },
  "face_groups": [ /* 面组配置 */ ]
}
```

### 1.3 面组配置模板
```json
{
  "group_id": "group_name",
  "face_range": [start, end],
  "vis_params": {
    "brdf_type": "diffuse|roughmetal|rubber|sand|rock",
    "albedo": 0.2,      // 反照率 [0,1]
    "roughness": 0.5    // 粗糙度 [0,1]
  },
  "lwir_params": {
    "emissivity": 0.9,      // 发射率 [0,1]
    "base_temp": 320.15,    // 基础温度(K)
    "temp_tex_id": 0        // 温度纹理ID(可选)
  },
  "radar_x_params": {
    "rcs_lut_id": 1,            // RCS查找表ID
    "dielectric_real": 15.0,    // 介电常数实部
    "dielectric_imag": 3.0,     // 介电常数虚部
    "surface_rough": 0.3        // 表面粗糙度
  },
  "sar_c_params": {
    "sigma0_db": -12.0,     // 后向散射系数(dB)
    "polar_couple": 0.03    // 极化耦合系数
  }
}
```

### 1.4 传感器配置模板
```json
{
  "sensor_id": 1,
  "type": "radar|sar|ir|laser|vis",
  "band": "X|C|LWIR|MWIR|VIS|1064nm",
  "platform_object": "object_id",  // 可选,挂载到物理平台
  "tx_params": {
    "power": 5000.0,        // 发射功率(W)
    "beam_width": 0.05,     // 波束宽度(rad)
    "wavelength": 0.03,     // 波长(m)
    "pulse_rate": 3000.0,   // 脉冲重复率(Hz)
    "duty_cycle": 0.05      // 占空比
  },
  "aperture": {
    "position": [x, y, z],  // 绝对位置(无平台)或偏移(有平台)
    "direction": [dx, dy, dz],
    "fov_horizontal": 60.0, // 水平视场角(度)
    "fov_vertical": 30.0,   // 垂直视场角(度)
    "swath_width": 5000.0   // SAR条带宽度(仅SAR)
  },
  "rx_cross_sensors": [ /* 互扰配置 */ ]
}
```

### 1.5 互扰配置模板
```json
{
  "sensor_id": 2,
  "type": "ir",
  "receive_range": 0.1,      // 接收范围系数
  "coupling_factor": 0.02,   // 耦合因子
  "band_match": 0.8,         // 频段匹配度 [0,1]
  "is_process": true         // 是否处理互扰信号
}
```

### 1.6 大气层配置模板
```json
{
  "height_min": 0,
  "height_max": 11000,
  "temp": 288.15,            // 温度(K)
  "pressure": 101325.0,      // 压强(Pa)
  "humidity": 0.5,           // 相对湿度 [0,1]
  "density": 1.225,          // 密度(kg/m³)
  "aerosol": 1.0e-5,         // 气溶胶浓度
  "ext_coeff": [2.0e-4, 5.0e-4, 1.0e-5, 1.0e-5],  // 消光系数[VIS,IR,Lidar,Radar]
  "scatter_coeff": [5.0e-5, 1.0e-4, 1.0e-7, 1.0e-7],
  "absorb_coeff": [1.0e-5, 2.0e-5, 1.0e-7, 1.0e-7],
  "refract_index": 1.0003,   // 折射率
  "turbulence": 1.0e-14,     // 湍流强度
  "cloud_density": 0.0       // 云密度 [0,1]
}
```

## 2. 常用参数范围

### 2.1 物理参数
| 参数 | 典型值 | 单位 |
|------|--------|------|
| 坦克质量 | 40000-70000 | kg |
| 小车质量 | 1000-3000 | kg |
| 导弹质量 | 10-100 | kg |
| 摩擦系数 | 0.6-0.9 | - |
| 弹性系数 | 0.1-0.3 | - |

### 2.2 传感器参数
| 传感器类型 | 功率(W) | 波束宽度(rad) | PRF(Hz) |
|-----------|---------|--------------|---------|
| X波段雷达 | 1000-10000 | 0.02-0.08 | 1000-5000 |
| C波段SAR | 5000-20000 | - | 1000-2000 |
| 激光雷达 | 10-200 | 0.001-0.02 | 1000-10000 |

### 2.3 材料参数
| 材料类型 | 反照率 | 发射率 | 基础温度(K) |
|---------|--------|--------|------------|
| 金属表面 | 0.3-0.6 | 0.1-0.3 | 290-310 |
| 沙漠沙土 | 0.3-0.4 | 0.9-0.95 | 310-340 |
| 植被 | 0.2-0.3 | 0.95-0.98 | 295-310 |
| 岩石 | 0.2-0.35 | 0.85-0.92 | 300-320 |
| 橡胶 | 0.1-0.2 | 0.85-0.9 | 310-340 |
| 发动机 | - | 0.9-0.95 | 350-400 |

## 3. 坐标系约定

### 3.1 世界坐标系
- **Y轴向上** (Y-up), 原点 = 场景 `geo_anchor` 的锚点 ENU 切平面
  (X=East, Y=Up, Z=-North), 由 rocky 地球基座钉到地表
- 重力方向: `(0, -9.81, 0)`
- 地面: rocky 地表高程 (传感器链: .vsgterrain 地形 > rocky 高程网格 > 平面兜底)
- X-Z 平面为锚点切平面

### 3.2 位置与方向
```json
// 位置: [x, y, z]
"initial_position": [0.0, 1.0, 0.0]  // 地面上方1米

// 方向: [dx, dy, dz] (无需归一化,系统会自动归一化)
"direction": [1.0, 0.0, 0.0]  // X轴正向
"direction": [0.0, -1.0, 0.0]  // 垂直向下
"direction": [1.0, -0.5, 0.0]  // 斜向下(X正向,俯角约26.6°)

// 旋转: [rx, ry, rz] (欧拉角,度数)
"initial_rotation_deg": [0.0, 45.0, 0.0]  // Y轴旋转45°
```

## 4. 常见场景配置

### 4.1 静态单目标
```json
{
  "physics": { "mass": 0 },
  "initial_velocity": [0.0, 0.0, 0.0]
}
```

### 4.2 运动平台
```json
{
  "physics": {
    "mass": 61500,
    "initial_velocity": [10.0, 0.0, 0.0],  // 36 km/h
    "friction": 0.8
  }
}
```

### 4.3 空中平台(SAR)
```json
{
  "platform_type": "airborne",
  "aperture": {
    "position": [0.0, 3000.0, 0.0],  // 高度3km
    "direction": [1.0, -0.2, 0.0],   // 侧视
    "swath_width": 5000.0
  }
}
```

### 4.4 挂载传感器到平台
```json
{
  "platform_object": "tank_001",
  "aperture": {
    "position_offset": [0.0, 2.5, 1.0],  // 平台坐标系偏移
    "direction": [0.0, 0.0, 1.0]
  }
}
```

### 4.5 动态温度更新
```json
{
  "temp_update_rate": 10,  // 10 Hz更新
  "face_groups": [{
    "lwir_params": {
      "base_temp": 380.15,  // 发动机温度
      "temp_tex_id": 1      // 动态纹理ID
    }
  }]
}
```

## 5. 运行命令参考

### 5.1 验证配置
```bash
./vsgraysim_app --mode validate \
  --object config/scenarios/tank_platoon_recon.json
```

### 5.2 列出传感器
```bash
./vsgraysim_app --mode list-sensors \
  --object config/scenarios/desert_radar_test.json
```

### 5.3 单传感器追踪
```bash
./vsgraysim_app --mode trace-sensor \
  --object config/scenarios/tank_platoon_recon.json \
  --sensor-id 1 --frames 30
```

### 5.4 无头模式运行
```bash
./vsgraysim_app --mode headless \
  --object config/scenarios/desert_radar_test.json \
  --frames 100
```

### 5.5 完整GUI运行
```bash
./vsgraysim_app --mode run \
  --object config/scenarios/tank_platoon_recon.json \
  --frames 300
```

## 6. 故障排查

### 6.1 模型加载失败
```
错误: "mesh file does not exist"
解决: 检查 mesh_path 是否正确,确认模型在 build/models/ 或 models/ 目录
```

### 6.2 物理参数错误
```
错误: 动态对象质量为0
解决: 设置 mass > 0,或改为静态对象(mass=0,无速度)
```

### 6.3 传感器配置冲突
```
错误: 传感器ID重复
解决: 确保每个 sensor_id 在场景中唯一
```

### 6.4 面组范围越界
```
错误: face_range 超过 face_count
解决: 检查 face_range[1] <= face_count
```

---

**文档版本**: v1.0
**更新日期**: 2026-07-31