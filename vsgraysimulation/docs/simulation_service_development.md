# 仿真服务与双端主控

`vsgraysim_sim_service` 提供同一套本机 HTTP/JSON 接口给现有 Qt 仿真主控和浏览器。服务按运行会话启动独立世界进程，每个进程拥有自己的 Broker 命名空间、地形与场景快照、检查点和 GPU 测量子进程。Qt 从本机共享的权威场景快照更新原有 Vulkan 视口。浏览器使用 TypeScript、Vite、原生 DOM 和 Canvas 2D。独立的 Vulkan 渲染进程输出三维地球帧，网页在视口上叠加半透明工作面板、相机控件和装备预览。

## 构建与打开

Qt SDK 需包含已有的网络模块和 Qt HTTP Server。浏览器构建工具使用 Node.js 22.12 或更新的 22.x 版本。网页构建产物由仿真服务直接提供。

```bash
cmake -S . -B build
cmake --build build --target vsgraysim_sim_service vsgraysim_sim_control vsgraysim_scene_designer -j 6
npm --prefix web/sim-console ci
npm --prefix web/sim-console run build

export VSGRAYSIM_TAIPEI_DATA_ROOT=/home/mte/EarthData/Taipei80km
export VSGRAYSIM_EARTH_DATA_ROOT=/home/mte/EarthData/World.earth.tms_repo
build/service/vsgraysim_sim_service
```

打开[场景设计](http://127.0.0.1:8765/design)、[模型工作台](http://127.0.0.1:8765/models)或[仿真控制](http://127.0.0.1:8765/simulation)。启动场景后，在网页的运行会话列表读取会话 ID。Qt 主控菜单选择「连接仿真服务…」并填入该 ID，也可以从命令行连接现有运行会话。

```bash
build/app/vsgraysim_sim_control --service-run r1 http://127.0.0.1:8765
```

三维网页视口使用 `vsgraysim_web_view`，需要启用 GUI 构建选项，并在渲染主机提供 X11 与 Vulkan。使用 `-DVSGRAYSIM_BUILD_GUI=OFF` 时可单独运行仿真 API。Qt 桌面模式仍可直接打开本地场景文件。服务的本机项目目录维护在 [projects.json](../config/service/projects.json)，支持登记已有的 `.vtopic.json` 和 `.vsimscene`。管理员可先将新版本放入独立路径，再用新的版本 ID 登记。

## 浏览器工作区

- **场景设计**启动时显示共用的场景目录、场景截图和描述。打开工程后，在同一个地球视口内通过滚轮连续调整距离，通过「全地球」和「定位工程」返回对应观察位置。项目的体素地形、目标和环境在该地球的地理锚点下加载。
- **协作编辑**可从视口下方输入「添加 skywalker-x8 编号 survey-air 到 (20, 250, 50) 米」或「添加 survey-vessel 编号 survey-sea 到 (30, 0, 40) 米」。坐标依次为东、上、南，单位为米。结构化部署方案也接受 `wgs84: [纬度, 经度, 椭球高]`。提交后其他客户端自动更新实时视口和场景树。
- **模型工作台**从场景设计打开。模型档案包含显示名称、预览姿态、参考温度、可见光反射率、中长波红外发射率和 RCS 功率倍率。登记过的场景目标可采用档案的指定版本，参数作用于该目标的传感材质。Qt 在「工具 → 模型工作台」使用同一模型目录、Vulkan 帧和档案接口。新导入支持最多 24 MiB、自包含纹理的 GLB 原模预览。缩略图来源见[模型资源](../models/ASSET_SOURCES.md)。
- **仿真控制**由独立网页入口加载，地球下方显示运行、暂停、单步和停止控件。工程、运行会话、平台状态和装备预览仍使用同一服务。普通相机操作不推进仿真。
- Qt 与网页通过同一套会话、工程与视口接口进行协作。Qt 默认打开共享场景目录，可用 `--collaborate http://127.0.0.1:8765` 指定本机服务。拖动旋转、Shift 拖动平移、滚轮缩放、双击定位工程，Home 和 End 对应全地球和工程观察位置。两个客户端分别持有相机状态，视口均由 Vulkan 渲染。
- 共享工程沿用 OpenUSD 的基础内容、属性覆盖和组合思路。服务按目标定义、位置、模型材质更新属性版本，串行原子保存。不同目标或属性上的过期预览可合并，同一属性在基版后有过提交则返回 409。图层事务目前保存为服务草稿 JSON。普通 Voxel Topic 已支持写入，已登记的场景包可以只读浏览。Qt 的本机文件工作流可用 `--local` 打开，继续提供本地模型部署、OpenUSD 文件和场景包工具。

## 双场景验收

| 场景 | 台北数据和状态 | 实际发布产品 |
| --- | --- | --- |
| 近海 | GLO-30 椭球高、Sentinel-2B 影像、Skywalker X8 与近岸测量船 | X8 定位真值，测量船单波束测深。海底采用场景内的合成示例地形 |
| 陆地 | 同一组台北新数据，沿用多谱段观测场景 | 原有 Vulkan GPU 的可见光和中波红外 P0 输出，经显示映射后预览 |

飞机和测量船各有独立的操控租约。航向和速度命令在原有固定步之间应用。本轮验证的是预设运动平台的航向和速度指令。装备数据租约可以共享。一个客户端退出后，其他客户端的租约仍可继续驱动场景和设备。

```bash
python3 tools/validate_simulation_service.py
```

验证脚本使用真实场景、世界进程和测量子进程。检查两个运行会话、独立操控、跨会话租约隔离、光学与红外载荷、暂停与固定单步，并注入一次世界进程故障来验证检查点恢复。报告写入 `build/service-validation/api/report.json`。

## 当前接口范围

基础路径为 `/api/v1`。运行、时钟命令、平台、装备和租约走统一服务。客户端以 `X-Client-Id` 识别会话，装备预览还需对应的 `X-Lease-Id`。Qt 和网页提交当前已展示的 `X-Through-Tick`，保证装备帧的仿真时间不超过该步。浏览器暂用约 450 ms 的状态轮询，图像使用限宽 512 像素的灰度显示映射。原有装备运行时保留采集分辨率及原始产物。

`GET /runs/{id}/native-preview` 用于同机 Qt 读取快照描述，其中的文件路径只在服务主机有效。Web、浏览器之外的语言客户端可通过同一套 HTTP 接口获取状态。当前限 IPv4 回环地址，用户认证、低延迟三维视频、跨主机任务调度和 OpenUSD 多人编辑仍按[整体接口设计](interfaces/voxel-service-v1.openapi.yaml)分阶段接入。
