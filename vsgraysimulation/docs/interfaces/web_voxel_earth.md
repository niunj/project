# B/S 体素地球

浏览器使用 WebGPU 绘制从现有体素瓦片派生的暴露面。地球相机、可见瓦片选择、拆分和拾取在客户端运行。仿真进程提交地表、碰撞和传感器状态时沿用现有体素链路。浏览器在一个地表修订版的必要块全部就绪后切换近景，可在视口工具栏选择 VSG 查看完整场景效果。

## 发布数据

在已经构建服务与 `earth_voxel_bake` 的仓库中执行。两个输出目录应为新目录。

```bash
export VSGRAYSIM_TAIPEI_DATA_ROOT=/home/mte/EarthData/Taipei80km
export VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo
build/earth_voxel_bake config/earth/global-voxel.map.json \
  build/earth-packages/web-native 25.078 121.772 7 12 10 -15000 --global-roots
python3 tools/earth/publish_web_voxels.py \
  build/earth-packages/web-native build/earth-packages/web-publication
```

使用项目工具链执行 `npm --prefix web/sim-console ci` 和 `npm --prefix web/sim-console run build`。以 `VSGRAYSIM_WEB_EARTH_ROOT` 指向 `web-publication` 的绝对路径启动 `vsgraysim_sim_service`。模型工作台使用 VSG，地球视口在浏览器提供 WebGPU 且瓦片已发布时使用 WebGPU。URL 参数 `renderer=vsg` 可在页面打开时指定 VSG。

台北区域使用现有 GLO-30 椭球高 DSM 与 2024-02-14 Sentinel-2B 影像。全球粗瓦片和区域细瓦片经同一导出器生成。目录中的 `source_level` 表示实际数据来自哪一级，补齐树分支时按父级剖面裁切采样。

## HTTP 资源

请求使用正式服务的 `X-Client-Id` 会话。静态二进制和影像的文件名是内容 SHA-256，大小受服务端上限约束。仿真网格 URL 包含运行代次，切换场景或恢复后客户端重新订阅。

| 请求 | 响应 |
|---|---|
| `GET /api/v1/earth/manifest` | `vsgraysim-web-earth-v1` 目录，包含 WGS84 `global-geodetic-xyz`、数据版本、两个根瓦片、可细化标记、误差米数、边界和几何及影像的地址 |
| `GET /api/v1/earth/chunks/{hash}.vxt` | `VXT1` 列索引和占据区间 |
| `GET /api/v1/earth/chunks/{hash}.png` | 北起行序的 RGB 影像 |
| `GET /api/v1/runs/{id}/earth/{generation}` | 已提交地表的完整块目录，并携带 `tick`、`surface_revision`、原生地表修订号和场景锚点。请求头 `X-Wait-For-Surface: 1` 与 `X-After-Surface` 可等待较新的目录 |
| `GET /api/v1/runs/{id}/earth/{generation}/{hash}.vxm` | 后端已提交地表的局部三角面与 UV，切块后以哈希复用 |
| `POST /api/v1/runs/{id}/earth/{generation}/pick` | 输入已显示的 `surface_revision`、`origin_m` 和归一化 `direction`。返回同一版本地表的 `position_m`。版本过期返回 409，射线无命中返回 204 |

`VXT1` 多字节值采用小端序。24 字节头依次为魔数 `VXT1`、`u16` 网格边长、`u16` 保留值、`u32` 剖面数、`u32` 区间数和 `f64` 高程量化步长米数。其后为从北到南、每行从西到东的 `u16` 剖面索引。每个剖面记录为两个 `u32`（首区间下标、区间数量）、3 字节 RGB、1 字节保留值。区间为 `i32 [bottom, top)`，同列可有多个不相连的区间。压缩影像可独立缓存。

`VXM1` 多字节值采用小端序。12 字节头为魔数 `VXM1`、`u32` 顶点数和 `u32` 索引数。顶点含 `f32 × 6`，分别为相对块中心的东、上、南坐标、纹理 `u/v` 和着色系数。最后为 `u32` 三角形索引。就绪目录列出当前地表全部块的哈希，客户端仅获取当前缓存中缺失的块。

静态地形局部破坏时，发布器根据上一次成功发布后的占据变化，只对受影响块及接缝邻块重新编码。地形范围、网格基准或显示模式变化时按整个地表重建。`rebuilt_chunks`、`reused_chunks` 和 `publish_ms` 可用来区分重新编码开销和仿真重建开销。

## 操作和验收

- 场景设计器用“全地球”和“定位工程”切换观察尺度，滚轮连续缩放，Alt 单击拾取。编辑页面的地表拾取可用来填写部署预览。仿真页在已挂载的运行地表内可由运行服务确认同版本拾取结果。
- 首屏先观察根瓦片，再看台北细瓦片逐级覆盖。冷缓存记录首帧与最高级瓦片到达时间。热缓存按相同鼠标路线记录 GPU 完成帧数、滚轮延迟与资源流量。
- 在仿真页对含预设地形破坏事件的台北场景单步一次，确认运行代次、地表修订号、变化块及拾取高度均按步骤推进。可用 `sim_earth_gui_tests --scene-topic-still` 对单步前的场景和单步后保存的检查点拍摄同机位 Vulkan 参考图。
- 用多浏览器会话检查 `/api/v1/views` 请求和服务端子进程数。WebGPU 地球浏览会话无需服务端 VSG 视口。目标标记、地形和影像由浏览器显示，体积云、海洋和精细目标仍可通过 VSG 视图检查。
