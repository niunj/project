# 智能部署模块

场景设计器打开一个地球体素专题后，在「场景专题」菜单中选择「智能部署」。可以在三维视口选取地形点，在场景树选取目标，再输入自然语言。生成方案后检查 JSON，执行「预检」和「应用方案」。也可以直接载入示例或粘贴 JSON。应用后的目标、航迹和挂载设备显示在原有场景树中，通过原有流程保存专题或发布 `.vsimscene`。

例如，选中一块地形后输入：在选点正上方 250 米放一架 Skywalker X8，编号 survey-x8，挂一台可见光测绘相机。把天气设为小雨。

## 离线推理

部署时同时准备用户机器上的推理程序和模型权重。推理只连接回环地址上的 `POST /v1/chat/completions`。例如用 llama.cpp 启动已有权重：

```sh
llama-server -m /path/to/instruct.gguf --alias vsim-local --host 127.0.0.1 --port 8080 -c 8192
```

界面默认使用上面的地址和模型别名，也可通过 `VSGRAYSIM_DEPLOYMENT_ENDPOINT`、`VSGRAYSIM_DEPLOYMENT_MODEL` 设置启动值。模型可替换，服务须支持非流式 JSON 对话输出。权重和推理程序独立分发。目标资源及设备配置来自本机目录，方案只包含目录中的别名。

## 模块接口

| 接口 | 职责 |
| --- | --- |
| `Deployment.h/.cpp` | 版本化 JSON 计划、对话请求和响应解析、字段与数值校验。依赖 C++17 和 nlohmann_json。 |
| `VoxelDeploymentAdapter.h/.cpp` | 把计划换算到当前专题坐标，解析本地资源和设备档案，生成预检结果，通过体素世界发布事务应用和撤销。无需 Qt。 |
| `SmartDeploymentPanel` | Qt 界面、视口地形拾取、回环 HTTP、后台导入作业和场景树更新。 |
| `config/deployment/catalog.json` | 宿主资源目录。登记模型文件和现有型号、传感器档案和挂点、材质参数及天气预设。 |

移植时可以把 `scene_authoring` 加入任意宿主的 CMake 项目，使用 `scene_deployment` 目标。调用 `modelRequest(model,text,context)` 获得对话请求，或直接用 `parse(planJson)` 读取结构化方案。将 `modelPlan(response)` 返回的校验结果交给宿主场景编辑 API。坐标换算、资源目录、拾取和事务由新宿主适配。可用 `cmake -S lib/scene_authoring -B deployment-build` 独立配置库。

## scene-deployment-v1

```json
{
  "format": "scene-deployment-v1",
  "actions": [
    {"op":"add_target","id":"survey-x8","model":"skywalker-x8",
     "position":{"relative_m":[0,0,0],"agl_m":250}},
    {"op":"attach_sensor","object":"survey-x8","profile":"survey-tv"},
    {"op":"material","object":"survey-x8","parameters":{"rho_tv":0.36,"eps_lwir":0.88,"radar":{"rcs_scale":1.1}}},
    {"op":"weather","preset":"light_rain"}
  ]
}
```

- `place` 可移动没有既有航迹的普通目标。`route` 为规定运动目标写入时序轨迹，用两到 64 个位置和 `speed_mps`，从当前仿真时刻起按段长计算时间。依据高程校验指定航点，航点之间按现有插值运动。
- `local_m` 是专题局部东／上／南轴，随专题 heading 旋转。`relative_m` 是相对选中地图点的局部偏移。`wgs84` 依次是纬度、经度、椭球高。`agl_m` 依据当前专题地形求离地高度。
- `material` 可指定预设或显式参数。省略 `region` 时修改整对象。分区可用对象局部坐标的 `min_m` 和 `max_m`，也可用 `center` 加 `size_m`，例如以拾取的地图点为中心定义长方体。按已有体素归属应用光学、红外和雷达属性。预设是场景参数估计，`rcs_scale` 是相对功率倍率。
- `weather` 用预设替换当前天气配置。设备档案覆盖可见光、MWIR、LWIR 和脉冲雷达。挂载使用现有登记平台和正式传感器链路，预检用正式测量规则检查航点处的地形覆盖。
- 模型目录目前提供两种测绘无人机。把其他已登记的型号和可读取模型加入目录即可扩展候选；不同装配可各自设定挂点。底层轨迹、碰撞、体素预算和材料约束仍由宿主验证。
- 应用以同一个暂停态发布，后台导入期间若专题或装备配置改变则拒绝旧方案。可以撤销最近一次部署，之后推进仿真或执行另一项编辑会使撤销快照失效。

模型参考见 [llama.cpp 本地服务的对话接口](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md#post-v1chatcompletions-openai-compatible-chat-completions-api)。
