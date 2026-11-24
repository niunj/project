# 体素装备仿真与显控

正式仿真和显控链路让可见光、SWIR/MWIR/LWIR 和雷达设备共用 `VoxelScene` 的实际几何、位姿和版本，通过 `VoxelSensorSession` 执行 Vulkan 成像/回波计算，按装备 ID 发布到原有 `SimDataBroker`。显控复用原有成像、RD、链路和电源面板。正式仿真软件通过 `TimingBus → VoxelEquipmentSource` 接入这一运行时，再通过已有 BrokerNet 发布给原显控。原有传统场景路径仍保留，不会自动改成体素。

正式使用流程：

1. 在现有场景设计器的“场景专题”菜单打开 `config/topics/taipei-equipment.vtopic.json`，必要时用“绑定体素装备配置”选择已有 `.vequipment.json`。
2. 保存专题；“保存并启动仿真”把同一个专题交给现有 `vsgraysim_sim_control`。保存文件也会出现在工作区启动页的已有场景中。
3. 仿真初始化完成后启动原显控 `vsgraysim_control`，在原有在线平台/装备树勾选设备并点击“连接所选”，再播放仿真。
4. 显控接收源端的真实产品和只读场景快照；开关机沿用原有请求和状态回报。显控内不推进另一套体素物理。

```bash
./build/app/vsgraysim_scene_designer --scene-topic config/topics/taipei-equipment.vtopic.json
# 也可分别启动已经保存的工程和原显控：
./build/app/vsgraysim_sim_control /绝对路径/台北场景.vtopic.json
./build/app/vsgraysim_control
```

本地 `--voxel-equipment=/绝对路径/配置.vequipment.json` 保留为明确的开发联调入口，生产默认不打开新选择页。

默认配置是台北新数据的 4×4 km 陆地工作集，外部地球背景仍使用新数据覆盖。载入带原模型表面信息的 F-16 体素目标，提供可见光、MWIR、LWIR、X 波段雷达四个通道。右侧标签切换设备，图像自动适配窗口，双击恢复适配；下方分别显示通道时刻、电源和收帧情况，可关闭某台设备。三维视图中的导航不改变设备孔径；观测位置由装备配置控制。

示例中的固定空中目标、295 K 蒙皮、580 K 尾部局部热源、发射率、介电参数、粗糙度、相对散射强度均为演示假设，未经实机标定。Sentinel-2 RGB 影像用于颜色纹理，并非逐材料 BRDF、温度或 RCS 的实测数据。30 m DSM 不是裸地 DTM，10 m 影像也不提供建筑几何。飞机没有飞行动力学或发动机热传导求解。保留原模型细节不改变物理破坏单元大小。

## 大显控统一入口

直接运行 `./build/app/vsgraysim_control`，进入已有在线平台与装备链接装配界面。链接之后可在工具栏“显控台”或同名菜单切换综合任务、飞机、雷达、红外、光学；沿用已有 F-16、AH-64、A320、MQ-9 机型面板。面板选择不创建源端平台。

综合任务台包含飞机操作台和各传感器面板；雷达、红外、光学模式分别为每台已连接的同类设备生成独立面板。每个面板有设备来源选择，支持 MWIR/LWIR 等同类多设备。切换显控类型保留设备连接，已选机型面板按工作台记忆；隐藏/关闭飞机操纵界面仍会释放飞控接管。

正常工作流程仍为“场景设计 → 仿真控制软件 → 大显控订阅平台遥测与装备数据”。原有跨进程发现、建链、控制回报继续使用。`--voxel-equipment` 是同一大显控的本地体素联调模式，不是新的独立传感器显控程序；体素正式数据源现由仿真控制软件拥有，显控经跨进程链路接收；数值计算目前仍在仿真软件进程内的专用线程，独立后台工作进程属于后续阶段。

本地体素配置可声明 `platforms: [{"id":72,"object":"aircraft","name":"F-16 体素平台","kind":"aircraft","model_heading_deg":180}]`。仪表读取该对象的实际局部位置、地速、姿态与时间；外部三维视图复用同一个场景。物理对象没有提供发动机或飞行控制器时，相应仪表显示缺失，接管按钮禁用；没有用面板选择推定飞行动力学。

也可直接选择启动界面，例如 `--voxel-equipment=/绝对路径/配置.vequipment.json --workmode=infrared` 或 `--workmode=aircraft`。选择机型面板只改变显示与操纵布局，不会把场景中现有目标自动替换成该机型。

## 专题保存和正式运行边界

既有 `earth-scene-topic-v1` 增加可选 `equipment` 与 `checkpoint` 字段。`equipment` 可引用已有装备配置文件，或内嵌同一配置；内嵌时由外层专题决定所属工程，不保留旧 `topic` 回指。保存使用现有 `voxel-scene-checkpoint-v1`，包含几何、纹理/表面、地形、单元材料、断裂、刚体初态和待执行事件，并保存当前局部机位及 up 方向。编辑器专题预览的天空按该场景时刻显示，关闭重开不会累计墙钟时间。

Save As 先解析源场景、地图与装备引用再保存，防止换目录后相对路径指向错误资源；依赖仍使用本机/部署数据根目录，并非已经实现所有资源的跨机器打包发布。传统编辑器中未接入体素真值的模型拖入、程序化地形/水面编辑会明确拒绝；体素几何与事件编辑继续沿用已有体素场景工坊，本阶段主要完成已有专题的保存、交付和运行。

正式源的场景创建、推进、GPU 装备生成和销毁都在同一专用线程，主时序只提交有界任务，保持原 owner-thread 约束。仿真预览与设备共用本进程已有 Vulkan Device；多个显控三维窗口复用本进程图形设备，避免部署版 VSG 仅支持 deviceID 0 时第二次分配失败。这是进程内设备共享，不是跨进程 GPU 零拷贝。

三维消费者读取源端提交的检查点几何和精确位姿；只做解码/绘制，不独立推进物理。大几何通过同一台机器的临时缓存文件引用，姿态、目录和观测产品继续走已有 UDS。缓存最多保留 8 个几何版本/512 MiB；单版本、专题文件上限 256 MiB。慢消费者丢失旧几何时需按最新描述重同步，不能把旧几何当成新版本。退出或重新初始化回收源端缓存，因此这不是跨机器场景分发协议。

背景地球的数据版本与 `voxel_scene.content_revision` 分开处理：后者用于本链路测量和体素显示一致性，不能传给背景 DEM 的版本校验。显控不叠加默认海面/云量，飞机外部观察使用权威体素目标，不另画一架不响应毁伤的外观代理。

本阶段尚未完成独立 worker 进程、按客户端租约的多显控订阅、全部参数控制，以及体素全场景录制回放；体素模式相关旧录制/回放入口暂禁用，避免产生不完整记录。首次载入与几何资源准备仍有同步阶段；不能据短时联调宣布全部工作负载实时。

## 单元属性

对象级 `material` 向下兼容原来的密度、温度、发射率、反射率和显示颜色，新增：

| 字段 | 含义 |
|---|---|
| `eps_mwir` / `eps_lwir` | 独立波段发射率，0..1；省略时使用 `emissivity` |
| `radar.dielectric_real` / `dielectric_imag` | 复介电参数 |
| `radar.dielectric_model` | 现有后端的固定 / Debye / 金属 / 频率缩放模型，0..3 |
| `radar.sigma0_db` | 现有散射模型的强度参数 |
| `radar.surface_rough` | 雷达粗糙度参数，0..1 |
| `radar.rcs_scale` | 相对回波功率倍数；1 保持强度，0 为零散射 |

`rcs_scale` 通过 `sigma0_db + 10 log10(scale)` 接入现有着色器；零值使用零菲涅尔反射条件。当前回波合成是距离单元内线性分配的短脉冲等效模型；这一入口要求 `pulse_width_s = 1/sample_rate_hz`，使匹配滤波参考为单样本。原后端没有生成长脉冲调频波形，不能直接给脉冲回波施加长 chirp 匹配滤波，否则会产生错误展宽和环绕峰；此类配置会明确拒绝。

它不是以 m² 标定的整机绝对 RCS，不提供实测角度表、极化、完整电磁绕射或多径求解。没有把尚未工作的 RCS LUT 接口当成已实现功能。

可在每个对象上配置局部材料：

```json
"sensor_materials": {
  "palette": [
    {"name":"蒙皮假设", "temperature_k":295, "eps_mwir":0.8},
    {"name":"局部热源假设", "temperature_k":580, "eps_mwir":0.95,
     "radar":{"rcs_scale":0.5}}
  ],
  "regions": [
    {"min_m":[-1.2,-2,-7], "max_m":[1.2,1,-4], "material":1}
  ]
}
```

坐标是原对象局部米制坐标；区域按体素中心、左闭右开区间选择，后面的区域覆盖前面的区域。默认使用第 0 项。最多 256 个材料、256 个区域、400 万格。也支持有序不重叠的 `cell_runs: [[起点,数量,材料编号],…]`，线性编号为 `(z*ny+y)*nx+x`；其覆盖优先于区域。材料项继承对象级参数，雷达子字段也逐项继承。

这是传感器属性表，不修改显示贴图，也不修改物理密度。单元异质密度/惯量尚未实现，材料项禁止覆盖 `density_kg_m3` 和 `color_rgba`，防止与刚体或可见纹理产生隐含矛盾。

每个规范三角形保留其原占用单元的编号。目标源表面、体素切口，以及高清/平滑体素地形的三角形均按该编号读取属性；共享边顶点按材料拆分，避免 GPU 平面材料 ID 跨单元串值。未改变几何或破坏精度。

断裂与裁剪碎块保留原属性坐标表；刚体运动只变换位姿；离散沉降会搬移对应单元材料。检查点保存完整属性表及坐标。编辑必须暂停并使用当前 revision，可使用 `kind: "sensor_materials"` 的事务重设该对象属性表，或传 `null` 清除。已有局部表是解析后的独立属性，后续仅修改对象级 `material` 不会隐式重写局部表；需要时显式重新提交局部表。

## 装备配置与数据

`voxel-equipment-v1` 文件声明专题、能见度以及独立的装备列表。支持 `TV`、`SWIR`、`MWIR`、`LWIR`、`RADAR`。同一种波段可有多台设备，ID 必须唯一。每台独立配置分辨率、水平视场、采样周期、光学、探测器和电子学参数。

- `P0`：成像场景物理辐亮度，单位 W/m²/sr。可见光当前沿用总线标量图像格式，取实际 RGB 辐亮度的亮度加权值；没有伪造彩色视频。
- `P4`：成像经过现有光学、探测器与电子学链后的归一化 DN。界面明确标注级别，不能把 DN 当成物理温度。
- 脉冲雷达可用 `P0` 发布 GPU 合成的复数 I/Q，或用 `P2` 发布 GPU 距离–多普勒产品。两者保留 PRF、CPI、载频、距离门和快时间采样率。P0 显控默认显示选定脉冲的完整 I/Q 采样，双线共用一个带零线的纵轴。P2 默认显示 A 扫包络。PPI 使用实际观测方位与斜距，固定波束只积累该方位，径向回波随仿真时间衰减。P2 的 CA/SO/GO 参考窗在线性功率域平均，默认排除峰值以下 100 dB 的 float32 数值尾迹。
- 雷达左侧可切换 PPI 和地物图层，右侧继续显示波形。地物图层在各显控进程后台读取当前场景的地理底图，显示北向、比例尺、帧内雷达站位和真北波束方向。像素提示包含 WGS84 坐标、底图参考椭球高、到雷达的参考斜距。此图层用于地理参照。破坏后的实际回波来自当前体素几何，参考图使用底图的原始高程和影像。雷达接收幅度仍为相对量。
- PPI 的“叠加地物散射”显示源端 GPU 从 CPI 中心脉冲提取的地形散射。它使用同一快照、可见性、波束增益和距离门，在小块传感器像素中保留一个实际散射点，并随帧发布真北方位、斜距和相对功率。点位与回波共用极坐标，地物层显示本帧照射区域。无命中帧清除原地物层。`radar.terrain_layer` 在普通雷达默认开启。台北装备测试区使用指定的陆地参考介电参数，回波幅度为相对仿真值。
- `config/topics/taipei-ground-radar.vtopic.json` 将雷达指向台北地表，可在场景设计和仿真主控中打开。地球测试程序也可使用对应的 `config/equipment/taipei-ground-radar.vequipment.json`。
- P0 默认显示快时间（往返微秒），可切换斜距。脉冲条、PRI、脉宽、首发和采样率由源端的 CPI 元数据推导。`radar.receiver_noise_iq_rms` 是相对复基带噪声 RMS，正式 P0 脉冲雷达默认 `0.015`，`0` 关闭。GPU 在接收归约时将噪声叠加到复数样本，种子按通道和帧变化，可复现。显控的“噪声细看”仅调整显示量程；原始数据也可以在 A 扫、矩阵和距离–多普勒视图中检查。
- `band: "SAR"` 使用与脉冲雷达相同的 `radar` 参数块，发布 `P0` 复数回波。现有 SAR 显控可查看 I/Q 和未聚焦距离–多普勒预览。单个固定孔径样例用于验证采集和传输，聚焦成像需另有逐脉冲孔径轨迹、相位参考与成像模型。
- SAR 面板的“ISAR 聚焦图”模式接收携带 `IsarImageAxes` 的 P3/P4 复数图像，以斜距（横轴）和带符号的横向距离（纵轴）显示散射强度，并保留实际米制比例。显示像素间距和可调的峰值相对 dB 标尺，光标可读取位置、幅度和相对峰值 dB。P0 与普通 SAR 数据可切回相应视图；缺少聚焦数据时，ISAR 模式显示等待状态。`build/gui/sim_display_sessions --radar-capture <输出目录>` 可生成合成回波 PPI、I/Q 曲线及 ISAR 格式演示截图。地球测试程序的 `--equipment-capture` 模式输出接收到地物散射时的同帧开关对照。
- 红外面板显示波段积分辐亮度；没有把已知场景温度伪装成经大气/发射率反演得到的测温结果。旧帧结构的标量发射率/大气透过率缺省值不能用于逐像元反演。

`aperture.position_m/look_at_m/up` 默认使用场景局部坐标；提供 `mount_object` 时使用该对象局部坐标并跟随其刚体位姿。挂载体为空或删除后停止该设备采集；没有推定设备随哪块碎片继续工作。

现有雷达后端使用连续 CPI，设备周期必须等于 `cpi_pulses/prf_hz`（配置不一致会拒绝），FFT 长度为 2 的幂，距离门必须落在物理快时间样本覆盖范围内。关闭或取消订阅的设备跳过 GPU 采集，其他通道继续；重新启用不会补发停用期间的数据。

几何、材质、纹理和传感器资源在发布前共同准备；失败则回滚场景。曝光/CPI 跨破坏版本会丢弃并计数，绝不拼接新旧几何。显控按场景版本拒收旧数据。新数据源启动时清空同 ID 的旧缓存。

打开 `config/topics/taipei-receiver-suite.vtopic.json` 可并排检查两台不同视场和光学配置的可见光相机、两台不同波段红外、两台不同采样率和脉冲数的脉冲雷达，以及一路 SAR 回波。成像设备输出 P4，雷达和 SAR 输出 P0 并跳过源端距离–多普勒生成。多装备仍沿用租约启动 GPU 工作进程，操控台与各装备的显示进程沿用原有链接流程。

可使用 `python3 tools/validate_multi_display.py --receiver-only --topic config/topics/taipei-receiver-suite.vtopic.json --output build/sensor-receiver/formal` 保存并重开专题，再在原仿真控制器与七个独立显控窗口之间核对帧号、场景版本和载荷摘要。这个验收范围是装备收发与显示，综合装订窗口的三维压力由该验证器的完整模式覆盖。

这一首轮配置明确采用晴空陆地观测，星历、UTC 和能见度供显示与测量共同使用。尚未接通云场历史到这一装备启动入口；当前 GPU 光谱传感器也不支持海水折射、水下辐射传播，含海洋的场景会显式拒绝。没有移除原海洋场景或用屏幕着色替代海洋测量。

## 原生验收

```bash
./build/gui/sim_earth_gui_tests --equipment-capture \
  /home/mte/FOLD/GitProject/vsgraysimulation/config/equipment/taipei-multispectral.vequipment.json \
  /home/mte/FOLD/GitProject/vsgraysimulation/build/voxel-equipment/accepted

ctest --test-dir build -R 'voxel_equipment_channels|voxel_earth_sensor_consistency|display_sensor_sessions' --output-on-failure
```

验收输出包括实际 Vulkan 地球截图、每台设备面板、实际 float32/complex64 数据与元数据，以及 `report.json`。工作台截图由 Qt 面板截图与其原位置的 Vulkan 帧读回组成，因为 Qt backing store 无法读取原生 Vulkan 子窗口。

性能分别记录稳定冻结场景的 Vulkan 提交并等待完成、中位/P95，动态 tick 中的场景推进、装备生成和完整 tick 墙钟耗时。完整 tick 包括按各自周期采集的设备，不等于每个 tick 同时采集所有设备。截图编码、数据导出、初始加载与 UI 事件处理未计入 tick，不能据此宣称完整应用实时。

## 三个正式软件的实际联调

在可用的 X11/Vulkan 桌面与台北新数据环境中，使用 `BUILD_TESTING=ON` 构建后运行：

```bash
python3 tools/validate_formal_chain.py --output build/formal-chain/accepted
ctest --test-dir build -R 'voxel_formal_project_link|voxel_equipment_channels|simulation_product_process_link|display_sensor_sessions|display_workspace_profiles|display_earth_context|designer_flight_configuration' --output-on-failure
```

验证器启动原场景设计软件保存专题，退出后用第二个真实进程重开，再启动正式仿真和原显控。测试驱动点击原在线树、链接按钮及电源按钮；检查未链接时不出数、四台设备接收真实产品、单通道关闭不影响其他设备、重新开机恢复和暂停保留数据。暂停后校验源端与显控每个最新产品的帧号及原始载荷 SHA-256。截图来自各软件实际 Vulkan 读回与其所在 Qt 窗口，不替换传感器画面。

测试自动化仅在 `BUILD_TESTING` 构建且显式设置 `VSGRAYSIM_FORMAL_CHAIN_DIR` 时启用，不改变正常启动流程。验收条件、截图、JSON 与日志见 `build/formal-chain/accepted`；这些是晴空陆地四设备链路验收，不是全天候、海洋光谱或长期性能验收。
