# 构建与依赖源码管理

源码快照由 `third_party/upstream.lock.json` 和逐文件清单管理。构建产物按组件
写入当前构建目录的 `third_party/` 下。OpenUSD 使用独立的 CMake 构建和安装目录。

| 用途 | 仓库位置 | 构建产物 |
|---|---|---|
| VSG、vsgXchange、Rocky、vsgBullet、vsgImGui | `third_party/<组件>` | `build/<配置>/third_party/<组件>/` |
| Bullet 3.25、Assimp 5.4.3 | `third_party/bullet`、`third_party/assimp` | `build/<配置>/third_party/<组件>/` |
| OpenUSD 26.08 C++ 核心 | `third_party/openusd` | `build/<配置>/third_party/openusd/` 和 `third_party/install/openusd/` |
| GLM、JSON、spdlog、EnTT、meshoptimizer | `third_party/support/` | 头文件或产品内置静态库 |
| GDAL、PROJ、GEOS、OpenEXR、IlmBase、Zlib、libzip、oneTBB | `deps/` | 解包到 `build/<配置>/third_party/base/` |

OpenUSD 构建包含场景组合、几何、材质、灯光和物理 schema。C++ 导出入口保存实体
层级、变换和素材路径。原生体素包继续负责占用、表面和物理快照。

构建读取已锁定的仓库文件。版本、源码校验、编译命令和实际链接位置记录在
`dependency-report.json` 和 `dependency-audit.json` 中。

## 标准入口

下列构建命令在仓库的 `vsgraysimulation/` 目录中运行。

要求 CMake ≥3.26、Ninja、C++17 编译器、Python ≥3.10 和下述平台 SDK。
主工程使用 Unix Makefiles，OpenUSD 使用 Ninja，默认最多并行 2 个 OpenUSD 编译任务。
通过 `VSGRAYSIM_CMAKE=/path/to/cmake` 选择新版 CMake。轻量构建可设置
`-DVSGRAYSIM_ENABLE_USD=OFF`。

```bash
./build_all.sh --jobs 4
./build_all.sh --test --jobs 4
./build_all.sh env_viewer --jobs 4
```

`./run_gui.sh` 从发布构建目录启动产品启动器。`BUILD_DIR` 可以为启动器、调试脚本和回归脚本选择同一个构建目录。

默认目录为 `build/source-release`。Debug 使用
`./build_all.sh --preset source-debug`。`--build-dir /绝对路径` 可以覆盖目录。
`--reconfigure` 保持兼容；入口每次都检查源码并配置，确保新增构建规则生效。
定向目标构建后的审计会明确列出未构建组件，不能当作完整构建验收。

直接调用 CMake 的等效流程：

```bash
python3 tools/dependencies/verify_sources.py --repo .
env -u LD_LIBRARY_PATH cmake --preset source-release
env -u LD_LIBRARY_PATH cmake --build --preset source-release
python3 tools/dependencies/audit_build.py --build-dir build/source-release \
  --json-output build/source-release/dependency-audit.json
env -u LD_LIBRARY_PATH ctest --preset source-release
```

`build_all.sh` 清理 `LD_LIBRARY_PATH`，隔离 IDE/agent 注入的旧 libstdc++；实际引擎库
依靠生成的本地 RPATH 解析，不再要求手工添加预装 VSG/Rocky 目录。

## 外部 SDK 与机器配置

验收平台是 Ubuntu 22.04 x86_64。目标平台提供系统工具链、桌面显示环境和下表三套可用 SDK。VSG 使用的 XCB、X11 和 GLVND 开发文件随底层库包提供。

| 依赖 | 配置入口 |
|---|---|
| Vulkan SDK / glslang / SPIRV-Tools | `VULKAN_SDK` |
| CUDA | `CMAKE_CUDA_COMPILER`、`CUDAToolkit_ROOT` |
| Qt6 | `Qt6_DIR` 或 `QT6_PREFIX`，当前使用 Qt 6.12 |

`THIRD_PARTY_ROOT` 保留对现有开发机 SDK 目录的兼容默认值；它不再决定 VSG/Rocky
引擎源码位置。新机器可复制 `CMakeUserPresets.json.example` 为
`CMakeUserPresets.json`，调整 SDK 路径，然后运行：

```bash
env -u LD_LIBRARY_PATH cmake --preset my-linux-release
env -u LD_LIBRARY_PATH cmake --build --preset my-linux-release
env -u LD_LIBRARY_PATH ctest --preset my-linux-release
```

也可传配置参数：

```bash
./build_all.sh --jobs 4 -- -DTHIRD_PARTY_ROOT=/opt/vsgraysim-sdk \
  -DVULKAN_SDK=/opt/vulkan/1.3.296.0/x86_64 \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc \
  -DCUDAToolkit_ROOT=/usr/local/cuda
```

资产端启用 Assimp 与 OpenEXR。场景体素导入支持 GLB、glTF、FBX、DAE、
PLY、STL、3DS、OBJ 和 VSG。仓库的主要示例场景已迁移为 VSG 资产。独立的旧 OSG/IVE
文件需要先在模型工具内转换成 GLB、OBJ 或 VSG，再进入正式工程。

## 修改第三方源码

需要快速调试时，可以显式允许本地源码变化：

```bash
./build_all.sh --allow-local-changes --jobs 4
```

这会报告 dirty 状态，并记录当前源树指纹；缺失文件、非法锁文件和越界路径仍然失败。
它不等同于锁定源码的干净验收。修改确认后，明确更新相应组件的文件清单：

```bash
python3 tools/dependencies/verify_sources.py --repo . \
  --record-local-changes vsg --reason 'Describe the reviewed renderer change'
python3 tools/dependencies/verify_sources.py --repo .
```

该操作保留上游 commit 和导入归档指纹，另记本地修改前后的源树摘要及原因。
提交时一并提交源码、相关 CMake 适配、锁文件及 manifest；不提交 build 目录。

接入 Git 时，保留根目录的 `.gitattributes`，Linux 工作副本使用 `core.autocrlf=false`。
首次导入先运行严格校验，再执行 `git add -f -- third_party`。上游忽略规则会跳过
ImGui 示例的部分工程文件，而这些文件也在锁定清单内。干净检出后再次运行严格校验。
日常只暂存经过审查的修改。

## 构建来源与验收

配置生成 `BUILD_DIR/dependency-report.json`，记录本地组件目标、源码和产物路径、
功能开关、工具链及外部 SDK 选择。`audit_build.py` 在构建后检查：

1. 当前源树仍与锁及配置时快照一致。
2. `compile_commands.json` 确实编译仓库内的引擎源码。
3. 实际链接命令中的引擎库指向当前构建目录。
4. Linux ELF 的运行时解析没有混入机器预装的同名 VSG/Rocky/桥接库。

标准入口将结果保存为 `BUILD_DIR/dependency-audit.json`。审计支持当前的 Linux/
Unix Makefiles 配置；其他生成器或平台需要扩充对应产物审计，不能用仅配置通过
替代真实编译和链接验证。

完整无窗口回归：

```bash
env -u LD_LIBRARY_PATH BUILD_DIR="$PWD/build/source-release" \
  ./run_regress.sh --skip-visual
```

运行需要可用的 Vulkan ICD。软件验收可显式选择本机 lavapipe；例如 Linux 上
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`。这不代表完成硬件RT
性能验收。跨进程测试需要创建本地 Unix 套接字，视觉矩阵另外需要有效 DISPLAY。

本阶段的“可复现”指来源、功能配置及依赖选择可追踪，并可从已纳管源码离线重新
构建。外部 SDK、编译器及驱动仍影响结果，不宣称跨机器二进制逐字节相同；若需该
级别保证，还需要锁定完整工具链/系统镜像、所有外部依赖并做产物摘要对照。

### 2026-11-12 Linux Release 验收

- 发布目录完成全量构建。来源审计检查了 589 条编译记录、45 个链接命令和 32 个 ELF 的运行时依赖。三套产品及启动器均加载本次构建的六个引擎动态库。
- lavapipe 下 20 项 CTest、天空/云/海洋三项无窗口冒烟测试全部通过。Qt 启动器在 X11 下的帮助入口通过。本轮未运行截图矩阵或硬件光追性能测试。
- 第二个空目录使用同一预设、编译器和外部 SDK 独立构建全部六个引擎库。替换构建目录前缀后，两份配置报告一致。六个库各自的 ELF `.text` 段、九份测试 SPIR-V 均在两次构建中逐字节一致。五个库的完整 ELF 摘要不同，两边的运行时库搜索路径指向各自的构建目录。第二个目录的 vsgconv 已成功将 glTF 三角形转换为含 VertexDraw 的 VSG 场景。
- 源码和清单经临时 Git 索引暂存并重新检出，全部 11 个组件通过严格校验。此检查在临时目录进行。

## 独立构建体素地球

在仓库根目录运行以下命令。将 `VULKAN_SDK` 指向本机 Vulkan SDK，其 `bin` 下需有 `glslangValidator`。CMake 从 `third_party/vsg` 构建 VSG，默认关闭原生窗口和运行时 shader 编译。使用原生体素数据包的过程无需联网。

```bash
cmake -S lib/voxel_earth -B build/earth-only -DCMAKE_BUILD_TYPE=Release
cmake --build build/earth-only --parallel
ctest --test-dir build/earth-only --output-on-failure
build/earth-only/voxel_earth_smoke config/earth/native-volume.scene.json --gpu
```

最后一条命令在本机 Vulkan 设备上编译地球、天空、云、海面和 FFT 管线，支持无窗口运行。数据包由场景引用提供。可将最后一条命令的场景路径替换为已烘焙的 `earth.scene.json`。合成示例随源码提供。

| 修改内容 | 本地源码入口 |
| --- | --- |
| 原生体素分块、径向实心区间、LOD、快照和地表查询 | `src/earth`、`include/vsgraysim/earth` |
| 整球、局部地表绘制和版本化海床接口 | `lib/voxel_earth/src/OrbitalGlobe.cpp`、`src/earth/SurfaceMesh.cpp`、`lib/voxel_earth/src/EnvironmentTerrain.cpp` |
| 三套软件共用的视口和发布时机 | `gui/viewport3d/EarthViewFeature.cpp`、`gui/viewport3d/EnvFeatures.cpp` |
| 天空与星历、云、海洋与 FFT | `env_modules/src`、`env_modules/include/rocky_env/EnvApi.h` |
| 自有 GLSL 与编译清单 | `shaders/viewport/voxel_globe.*`、`shaders/env`、`lib/voxel_earth/CMakeLists.txt` |

只需要整球绘制的宿主可链接 `vsgraysim_earth_globe`，用 `OrbitalSurface` 提供两半球的列顶高度与 RGBA 图像。需要云海天空的宿主链接 `vsgraysim_earth_environment`。加载原生场景包、局部体素和环境的宿主链接 `vsgraysim_voxel_earth`。原生包的 JSON 编解码使用仓库内的 `third_party/support/json` 单头库。整球绘制接口独立于此编解码器。

环境栈的 `worldRoot()` 挂到 ECEF 世界根，`contentRoot()` 和局部地表挂到同一锚点变换下。宿主在更新线程发布地表时将快照绑定到 `EnvironmentTerrain`，通过 `OceanParams::terrain` 提供海床和版本号。每帧将仿真 UTC、锚点和相机传给环境模块。使用 FFT 海面时，在编译前调用 `setDevice` 或 `setWindow`，将 `fftCommands()` 挂到绘制 pass 之前。独立地球的 GLSL 在构建阶段转为内嵌 SPIR-V；修改后重新构建即可生效。

完整产品保留 GDAL/PROJ 数据导入和原有工程格式，Rocky 兼容后端由 `VSGRAYSIM_WITH_ROCKY` 控制。地形分块保存体素占据区间；云体积密度、大气散射和海浪谱属于环境模型，当前云和海面的高细节绘制范围为局部工作区。

本次 Linux 验证使用 RTX 4060 Laptop GPU、驱动 580.178.04。独立测试程序移至仓库外后，在无窗口环境下加载真实烘焙包，建立带纹理的地表、整球、天空、云及 512×512 FFT 海面管线。独立构建与产品构建生成的 18 个自有地球和环境着色器逐字节相同。开启与关闭 Rocky 的完整产品构建分别通过 20 项 CTest。

三种产品视口在原生包上读到同一地表版本 `12141316101950113715`。同一台机器的一次轨道和跨洲路线测试中，100 次活动地形切换的端到端 P95 为 22.61 ms，其中 54 次 LOD 切换的 P95 为 23.78 ms。36 次观察区切换的 P95 为 17.54 ms。该次运行的最大碰撞高度误差为 3.12 mm。该结果对应测试所用分辨率、数据包和仿真负载。

## 全球云海与近景过渡

全球云海的实现位于 env_modules/src/VoxelWeather.cpp、SparseCloudVolume.cpp 和
shaders/viewport/planet_*。全球粗气象场固定在地理坐标上，采用球面风平流；
相机周围由 32 m 体素、8³ 体素砖、40×20×40 页表和带一格边界的 R16 图集覆盖。
局部权威域外回退到全球场；域内未驻留页明确表示晴空，已驻留页由云水、
湿度、温度和三维速度状态驱动。
局部体素执行平流、浮力、水负载、凝结、蒸发、潜热和涡量约束，变化砖单独
打包。状态使用 kg/m³ 液态水和 kg/kg 水汽混合比；R16 的 1.0 对应
0.003 kg/m³，光学消光由滴半径和消光效率计算，不再把归一化密度直接当光深。
天空 UTC、云平流和海浪相位使用同一仿真累计秒数，暂停保持相位，
回放和单步以宿主提供的仿真时间驱动。

全球海面按 WGS84 壳面逐像素求交，并写入反向 Z 深度。
海洋范围来自本地保存的 Natural Earth v5.1.2 数据与生成脚本，
详细来源见 env_modules/data/README.md。海面反射采用水介质 Fresnel，
坡度统计使用 Cox–Munk 的风速关系，近景接入现有 FFT 海浪。
参考观测 https://science-data.larc.nasa.gov/CRAVE/papers/SuApplOpt02.pdf 。
全球云层通过光学厚度计算 Beer–Lambert 透射，并包含
Henyey–Greenstein 方向相函数的单次散射近似。

默认近景权重在椭球海拔 2.5–18 km 连续变化，并随距离在局部工作区边缘衰减。
全球海面提供远场底层，局部 FFT 海面叠加细节。
渲染顺序为天空、全球海面、局部海面、全球云层、局部云层。
局部渲染使用稳定切平面，观察区分块更换时转换海床查询坐标，
并固定使用同一份已发布的地形快照。轨道视角按风更新海况，
FFT 的纹理生成与 GPU 调度在近景权重非零时执行。

三套软件通过 EarthViewFeature 和 EnvFeatures 共用实现。
新增验收命令如下，需本机 X11 与 Vulkan 驱动。

    build/voxel-release/gui/sim_earth_gui_tests --weather-capture /tmp/weather scene_designer
    build/voxel-release/gui/sim_earth_gui_tests --weather-capture /tmp/weather simulation_display
    build/voxel-release/gui/sim_earth_gui_tests --weather-capture /tmp/weather earth_display
    build/voxel-release/gui/sim_earth_gui_tests --cloud-descent-capture /tmp/cloud earth_display
    build/voxel-release/gui/sim_earth_gui_tests --orbital-stream-benchmark

截图验收固定仿真时间 1234.5 秒，分别检查轨道、过渡和地表权重。
性能验收包含提交、物理步、地形发布、独立测量、实际绘制和 GPU 完成，
单独统计全球与局部天气同时参与绘制的帧。数据包通过
VSGRAYSIM_BENCH_EARTH_SCENE 指定。

全球天气场只决定湿度、温度、风和 CAPE；可见云由离散粗体素云团组成，不再
从程序化云壳求密度。局部 `VoxelCloudCluster` 按云团拥有独立 ID、生命周期
及 32 m 稀疏砖状态，多云团仅为绘制共享 GPU 图集。初始场由相互搭接的主热泡、
不同尺度侧向子热泡和干空气卷吸空洞组成，并直接固化为状态体素。产品默认体素演化当前在 CPU 以
10 Hz 执行并上传，尚未接入实测气象同化。独立的 `GpuVoxelCloud` 单云实验场已把
平流、微物理、涡量约束、压力投影以及最多 8 个移动球形外物的排开/尾流注入迁移到
Vulkan compute；它使用固定输出槽并提供液态水和速度三维纹理。无窗口 RTX 回读测试
得到液态水积分 9.01548、飞机通过后的液态水场差 3.03034 和 78.34 m/s 峰值速度扰动。
该实验场还未绑定到产品默认 ray-march、传感器逐射线或真实机体 SDF，不能把它等同于
全球多云 GPU 调度已经完成。
因此本阶段是高分辨率稀疏体素主链，不应描述为已经达到 SilverLining 的最终
云型库、时间重投影和多次散射质量。全球海岸掩膜受源数据比例尺约束。
云层照明采用近似散射，真实观测气象、完整多次散射和海气耦合仍有后续工作。
RTX 连续 16000 km 至 120 m 的 97 帧验收中，后台按云对象并行准备、帧边界
整体提交，最近一次含 104 步近场积分和 16 步光线遮蔽的渲染 fence P95 为
21.29 ms，满足 33 ms 目标；最大帧 46.50 ms，仍需通过时间重投影、计算队列
和图集增量提交处理长尾。

本轮 RTX 4060 Laptop 验收记录保存在
build/voxel-release/planet-weather-final-rtx.log。100 次活动地形切换
P95 为 31.86 ms，54 次 LOD 切换为 31.76 ms，36 次观察区切换为 18.33 ms，
130 个云海过渡帧为 18.02 ms。活动切换中 3 帧超过 33 ms，
云海过渡中 2 帧超过 33 ms。此结果满足本次 P95 门限，仍存在长尾帧。
固定仿真时间下，三种产品视口的轨道、过渡、地表截图在中央
700×460 区域逐像素相同。截图前缀为 physical-weather-。
独立源码构建和无窗口 GPU 编译通过，Rocky 开关两种配置各通过 20 项 CTest。

## 太阳与月亮照明

本地体素基座、全球云层和海面使用同一星历太阳/月亮方向与月相。
全球表面按所在位置计算太阳高度和近似 RGB 大气消光，晨昏呈现暖色，
日照面和夜侧随仿真 UTC 旋转。海面新增月光反射。
太阳/月亮显示开关同时作用于整球、云海和场景光源。

天空修复了轨道射线未穿过大气时提前返回黑色的问题。
日月盘在真空仍可见，地球遮挡与大气透射分别计算。
太阳采用约 0.00465 弧度视半径，月亮采用约 0.00452 弧度视半径。
月盘使用按视角缩放的球面受光坐标、像素抗锯齿边缘和本地 NASA LROC
可视化底图，支持满月与弦月。月光照明归一化采用满月相对白昼
约 2.5e-6 的量级，并按月相衰减。固定白昼曝光下夜间较暗。
曝光属于显示映射，物理辐射查询仍使用原有量纲。

验收入口为 sim_earth_gui_tests --lighting-capture PREFIX earth_display，
生成 day、dusk、night、sun、moon、quarter 六张截图。
月面底图使用可视化产品，当前朝向采用潮汐锁定近似，
月球天平动、高精度月面地形阴影与完整多次散射仍待完善。

本轮日月照明验收使用 RTX 4060 Laptop。六种视角在三种产品视口中央
700×460 区域逐像素一致。体素产品与 Rocky 兼容构建各通过 20 项 CTest，
独立源码构建通过无窗口 GPU 编译。截图前缀为 build/voxel-release/celestial。
最新切换基准记录为 build/voxel-release/celestial-rtx.log。
100 次活动地形切换 P95 为 21.18 ms，54 次 LOD 切换为 19.78 ms，
36 次观察区切换为 18.97 ms，130 个云海过渡帧为 25.29 ms。
活动切换有 2 帧超过 33 ms，云海过渡有 3 帧超过 33 ms。
测试期间曾出现一次 Vulkan 设备丢失，重新创建测试进程后上述路线通过，
该失败运行不计入成功结果。GPU 设备丢失的稳定性仍需后续长期运行验证。

## 2026-11-17 体素场景工坊与固定步运行内核

`vsgraysim_voxel_workshop` 是独立的原生 Qt/VSG 搭建入口，共用现有视口、
原生体素参考地球和星历天空。示例为 `config/voxel/workshop.vscene.json`。
示例锚点海拔 182 m，使用仓库内的合成参考地球，不能作为真实测绘高程。
场景设计器的“工具 → 体素场景工坊”可启动同目录下的工坊进程。
工坊可新建空场景、新增米制盒体、导入 OBJ/VSGB 并体素化、编辑对象位置/俯仰/航向/横滚、
预设速度、密度、温度、发射率、反射率和颜色，也可删除对象、安排/取消损伤与
局部 Y 列沉降事件、运行/暂停/单步、保存和打开自包含检查点。
近场预览使用 400 m 阴影范围与 0.0005 深度偏置，保留日月投影并减少米制对象
的自阴影条纹；它属于显示参数，不能用作传感器的遮挡或辐射真值。

场景格式 `voxel-scene-v1` 由 `VoxelScene` 校验：

- `coordinates` 固定为 `east-up-south-metres`，即 X 东、Y 上、Z 南，单位米。
  `geo_anchor` 给出 WGS84 经纬度、椭球海拔和航向；局部对象采用刚体变换，禁止缩放和镜像。
- `fixed_step_seconds` 范围 0.001..0.1，默认 0.02；`utc_origin` 包含 `year/month/day/hours_utc`，
  校验公历日期并写入检查点。工坊在各视口特性采样前推进场景，再发布已提交的仿真秒数，
  因而日月、地球天气与对象使用同一累计时间；暂停和恢复均不累计墙钟时间。
- `objects` 使用稳定且唯一的 `id`，`role` 为 target/terrain/structure。
  `source` 支持 box、mesh 和编码后的 volume；mesh 相对路径以文档目录为基准。
  相同源的对象共享不可变烘焙体积。检查点保存实际占据状态，不依赖源模型继续存在。
- `position_m`、`rotation_deg`（俯仰/航向/横滚）和 `velocity_mps` 定义预设刚体运动；
  位置按整数 tick 从运动基准直接计算，避免显示帧率和分段续跑带来的积分漂移。
- `material` 为每对象均匀材质：density_kg_m3、temperature_k、emissivity、rho_tv、rho_swir、color_rgba。
  质量由剩余占据格数、格体积及密度计算。当前不保留导入模型的逐面材质，也不求解热传导。
- `events` 使用未来整数 tick、kind 和 object；damage 支持 point_m、scene/object 空间及
  显式 radius_m 或现有经验能量半径，settle 按局部 Y 列压实。相同 tick 按稳定序号执行。
  日志保存执行序号、时刻、删除格数和移动格数。编辑命令与仿真事件属于不同接口，
  当前事件日志不是任意场景编辑的撤销栈或完整命令回放日志。

运行内核在 owner 线程处理编辑和推进，读者可并发持有不可变快照。编辑事务带期望版本，
任一命令错误即拒绝整个事务；运行中禁止搭建编辑。每 tick 的几何变化先准备候选资源，
准备失败时保留原占据状态、事件队列和时刻。默认每次最多推进 8 步，余量明确返回，
不把显示卡顿转换为丢失的仿真时间。限制为每体积 400 万格/200 万顶点、
每场景 256 对象/1600 万格/400 万顶点及最多 65536 个待执行和已执行事件。

`voxel-scene-checkpoint-v1` 保存体积 RLE、运动基准、tick、版本、材质、地理锚点、UTC、
待执行事件及日志。RLE 线性索引为 `(z*ny+y)*nx+x`，run 为 `[start,count]`。
文件通过同目录临时写入后 rename 发布；不承诺断电后的 fsync 持久性。
`VoxelSceneFeature` 先编译候选显示资源，发布后再整体挂载；仅位姿变化时更新变换矩阵。

`makeVoxelSceneSensorData(snapshot)` 从同一占据网格生成五数组传感器几何、位姿、温度、
谱材质和颜色槽。调用 `SharedSceneResources::buildInstances`、`GpuSensorRuntime::initialize`
或 `replaceScene` 时应同时传入该包的 `textures`。动态资源池保留 UV/颜色槽；
所有对象销毁或空场景时使用内部不可见、零面积存储占位，输出天空/背景，不保留碰撞目标。
该适配器可接既有测量链，但没有把所有旧产品的场景序列化器自动迁移到新文档格式。
宿主仍须在每个曝光/CPI 时刻选择匹配的几何与环境版本；本阶段未保存 GPU 云的完整
速度/温湿/压力历史，也不等同于全环境的精确检查点、结构断裂、FEM 或刚体接触求解。

构建与验收：

    cmake --build build --target vsgraysim_voxel_workshop voxel_scene sim_voxel_scene_tests sim_gpu_runtime_tests -j4
    build/app/vsgraysim_voxel_workshop
    build/voxel_scene validate config/voxel/workshop.vscene.json
    build/voxel_scene run config/voxel/workshop.vscene.json 20 /tmp/voxel-20.json
    build/voxel_scene run /tmp/voxel-20.json 40 /tmp/voxel-60.json
    ctest --test-dir build -R 'voxel_scene_runtime|simulation_offscreen_gpu_runtime' --output-on-failure
    build/app/vsgraysim_voxel_workshop --smoke /tmp/voxel-workshop
    cmake -S lib/voxel_earth -B build/earth-only
    cmake --build build/earth-only -j3
    ctest --test-dir build/earth-only --output-on-failure

独立体素地球构建也提供轻量 `voxel_scene` CLI 与场景契约测试，仅链接体素库，
不依赖 Qt/CUDA/主仿真器；OBJ 加载库按自身路径解析头文件并在独立配置时显式构建。独立天气 smoke 将
`cloudFraction` 按柱消光量 1−T 验证有限范围、晴空与光学厚度单调性；不再把
超过任意消光阈值的柱比例误当作用户设置的面积云量。

新增 CPU 验收覆盖事务回滚、过期版本、旋转碰撞、取消事件、固定步追赶、待执行事件续跑、
旧快照隔离、发布失败、空体积、线程归属和文件恢复。GPU 验收回读 320 K 目标温度、
对象 RGB 比例、损伤开孔、几何替换后的测量帧号及全毁/空场景背景。原生 smoke 执行
定时损伤和沉降，恢复检查点后继续一步，与独立恢复的状态比较，并检查天空时钟后截图。

本轮 Release 验收：整工程构建通过，主工程 24 项 CTest 通过，独立体素库 3 项
CTest 通过；新增 OBJ 相对路径导入测试在删除原资产后仍可从检查点恢复同一碰撞体积。
原生工坊在 RTX 4060 Laptop 上运行至 tick 60，恢复后继续至 tick 61，
状态与独立恢复运行逐字段相同，天空累计秒数与场景一致。示例损伤在 tick 25
删除 136 格，tick 50 沉降移动 188 格；20+40 步与连续 60 步的完整检查点一致。
本轮截图和检查点在 `build/voxel-workshop-final/`，构建及测试日志前缀为
`build/voxel-workshop-`。这些是本次场景与驱动下的功能验收，不是长期运行性能保证。

## 2026-11-17 体素目标断裂、刚体接触与测量版本

本轮在 `VoxelScene` 正式运行路径接入面键断裂和 Bullet 3.25 刚体接触。
工坊导入框支持 GLB、glTF、FBX、DAE、PLY、STL、3DS、OBJ 和 VSG，提供源单位换算、
Y/Z 向上轴、封闭内部填充、动态/静态/预设运动、角速度、固定支撑、断裂能、摩擦和恢复系数。
新增“断裂面”“刚体冲量”操作，碎块使用自己的稳定对象 ID，能继续被编辑、命中或销毁。
默认示例改为真正的固定底部目标：tick 25 损伤切断支撑后，上部碎块通过重力与接触下落，
不再用列压实假装刚体坍塌。“离散沉降”仅保留给旧的 prescribed 几何模型。

### 模型读取

OSG 解码是宿主自己的 CPU 导入桥，不修改锁定的 vsgXchange 源码配置。
历史 OSG 导入构建曾使用 osg、osgDB、OpenThreads 及 osg/ive 插件，
把选定插件目录加入 Registry，避免 SDK 内置旧安装前缀导致“有库却读不了文件”。
旧场景保留同名 VSGB 资产时可沿用旧引用。
本机本轮使用 `/home/mte/Code/osg_365_glx`，可以在配置时指定其他兼容 SDK。

读取会累积节点变换、选择最细 LOD/PagedLOD、解析外部精细级资产，并将
Geometry/DrawIndexed 的绘制范围和顶点偏移转换为三角形。VSG 的 triangle list/strip/fan
按输入装配拓扑展开。`.vsg` 是带 `#vsga` 或 `#vsgb` 头的原生内容别名。
源坐标明确指定 `source.units_to_metres` 与 `source.up_axis`，Z-up 转为 `(x,z,-y)`。
导入的是静态几何姿态；动画蒙皮、本构和原模型逐面贴图不随体素保留。
每个体素对象使用场景定义的均匀材质，碎块继承相同温度、发射率和可见光颜色。

### 结构与碰撞模型

对象增加如下物理定义，例如固定在底层的可破坏结构：

    "physics": {
      "motion": "static", "fracture": true, "support": "base",
      "fracture_energy_j_m2": 1000,
      "friction": 0.6, "restitution": 0.1,
      "linear_damping": 0.02, "angular_damping": 0.02
    }

`motion` 为 prescribed/static/dynamic；旧文档默认 prescribed 保持固定速度基准。
`angular_velocity_rad_s` 为世界系弧度/秒；线速度统一指模型原点，Bullet 内部转换为质心速度。
`support` 的 none/base/all 分别表示无固定点、初始网格底层固定、所有剩余格固定。
检查点保存实际 anchor_cells，分割时重映射，不能把新裁剪碎块的底层错误地重新固定。
all 支撑使用紧凑标志，体积同时缓存占据格数及质心，避免逐帧扫描整个密实地形。

损伤删除体素后执行六邻域连通分析：有固定点的部分保持静态，无固定点的部分成为动态刚体。
独立的 fracture 事件以 point_m/normal/radius_m/energy_j 定义有限断裂面；
每断开一个面积为 cell² 的面键，消耗 `fracture_energy_j_m2 * cell²`，
优先从作用点附近断开，低于一个面键的能量不触发断裂。
面键切断不删除质量，完全断开的块才生成独立几何。小块也保留；超过 256 个对象时
整次发布失败，不悄悄丢掉碎屑。断裂能是待标定的工程参数，当前未求解连续应力、弹性、疲劳
或接触冲击自动开裂；外部毁伤机制以 damage/fracture 事件触发结构变化。

每块碰撞体由互不重叠的占据体素长方体合并构成精确并集，孔洞与凹部保持为空。
质量来自占据体积和用户密度；质心与完整惯量通过合并体积的主轴计算，
碎块继承原模型原点速度和角速度，因而保留线动量与角动量。
这不是原始装备质量标定：导入航空器使用统一密度时，数值只代表该体素实体的模型质量。
冲量事件以牛顿秒和作用点施加，偏心冲量通过惯量产生角运动。

Bullet 执行固定步内的自适应碰撞子步，同时考虑平移、角速度与最小格尺寸，
单步上限 256 子步；超限时拒绝推进并提示减小场景步长。它是有界离散接触积分，
不承诺任意速度下的解析连续碰撞。每体积最多 65536 个合并碰撞长方体，
场景最多 131072 个；不使用凸包或总包围盒填补空洞来绕过预算。
接触报告记录对象、点、法线、穿透量及最后子步冲量，不作为整个物理步的累计冲量。

物理步先积分到 tick 边界，再在边界执行有序事件。每个候选 tick 从已发布值重建
Bullet 世界，关闭 warm-start/休眠，并把穿透位置修正与速度修正分开。
这样失败的显示/传感器准备不会改变下一次动力学结果，检查点也不依赖未保存的接触缓存。
缓存的只是不可变体积对应的碰撞形状。检查点保存刚体模式、角速度、面键、固定点、
碎块序号、重力、`dynamics_model` 和 Bullet 版本，版本不兼容时明确拒绝恢复。
当前单世界用于锚点附近的局部米制工作区，超出浮点精度范围需分区。

### 显示与测量发布

`VoxelScene::addPublicationParticipant` 允许显示与传感器分别准备候选资源，
全部成功后才提交同一快照、事件、碎块 ID 和轨迹历史。图形更新在替换出站资源前等待
设备完成，VSG 非 std::exception 错误转换为可回滚错误；视口初始化失败发出错误信号，
不会因 Vulkan 设备创建失败直接 terminate。

`VoxelSensorSession` 使用相同的场景对象、温度、材质与占据网格。
它在几何版本边界先排空旧版本测量，再交换已编译的新资源，保留测量时钟和帧序号。
模型位姿来自 `VoxelScene::sample()` 的有界已提交历史（最多约 2 秒/512 个快照），
采用位置插值与旋转 SLERP；不外推缺失真值。测量窗口端点与断裂边界重合时使用旧区间
的左极限，不能把边界刚生成的碎块放入之前的曝光。
跨几何边界的曝光/CPI 当前按不完整真值窗口拒绝，计入 `rejectedSceneWindows()`，
帧号仍递增；当前不做跨断裂过程的曝光积分或 CPI 合成。积压超过历史范围明确报错。
消费者应在物理推进期间及时调用 advanceTo；一次最多缓存 8 个未排空的几何版本。
场景必须比 VoxelSensorSession 活得更久，文件重载或回退时重建该消费者。

遗留 `VoxelTerrainDebris` 保留为 AABB 演示兼容接口；正式工坊与新测量链使用
VoxelScene/VoxelDynamics，不再调用该接口来生成正式碎块。一个对象的刚体状态应由一个
动力学世界负责，不能同时在两套刚体接口重复积分。
本轮是目标及显式加入新场景的建模体素地形路径；全球影像/高程地球的活动 LOD 块、
飞行控制动力学、体素云的扰动与 GPU 场历史仍需按相同版本/时刻接口继续整合，
不能把地球背景显示或预览云场自动当成本世界的接触体或测量历史。

### 验收入口

    cmake -S . -B build
    cmake --build build -j3
    ctest --test-dir build -R 'voxel_|simulation_offscreen_gpu_runtime' --output-on-failure
    build/app/vsgraysim_voxel_workshop --smoke /tmp/voxel-fracture

独立库从同一份 Bullet 和 Assimp 源码构建，可单独配置：

    cmake -S lib/voxel_earth -B build/earth-only
    cmake --build build/earth-only -j2
    ctest --test-dir build/earth-only --output-on-failure

新增测试包含实际 OSG/IVE/OSGB/VSGT/VSGB 文件与精细级分页读取、显式单位/轴转换、
支撑切断与落地、旋转凹形孔洞、80 m/s 接触、面键耗能、质量/动量、偏心冲量、
重复毁伤、检查点续跑、发布失败回滚，以及 MWIR/TV 回读与采集时刻 DDA 真值比较。
仓库 AH-64 和 MQ-9 的 OSGB 原始资产也通过实际导入、破坏与质量核算，
本轮派生配置及检查点位于 `build/voxel-fracture-validation/`。

本阶段最终验收：完整工程与独立库构建通过；主工程 26/26 项 CTest、独立库 5/5 项
CTest 全部通过。主工程本次使用 Mesa llvmpipe 软件 Vulkan，包含真实 GPU API 的
测量回读和云/地球回归；这不能替代 NVIDIA 硬件执行验收。日志为
`build/voxel-fracture-ctest.log`、`build/voxel-fracture-standalone-ctest.log`。
原生工坊同样在软件 Vulkan 上运行至 tick 61，通过定时损伤、碎块运动、检查点恢复、
天空时钟与截图验证；产物在 `build/voxel-fracture-native/`，日志为
`build/voxel-fracture-native-software.log`。

实际 MQ-9 OSGB 在测试分辨率下有 325 个占据格，损伤删除 27 格后保留 298 格，
分成两个运动刚体；5+20 步恢复续跑与连续 25 步的完整检查点 JSON 完全相同。
AH-64 同样完成实际导入、删除和质量核算；该次局部损伤后仍连通，不错误地报告分块。

本机 NVIDIA Vulkan 当前返回 `VK_ERROR_INITIALIZATION_FAILED`（VkResult=-3），
未改动的旧程序也复现，且 `/dev/nvidia-uvm` 打开失败并返回 EIO。
本轮没有修改驱动或重置用户设备；因此暂不能报告 RTX 硬件验证通过。
新的视口错误路径已验证为清晰报告失败并退出 1，避免未捕获 VSG 异常导致进程中止；
记录见 `build/voxel-fracture-native-hardware.log`。

## 2026-11-17 影像高程地形接入统一体素场景

新增 `VoxelEarthPatch`，将 `EarthDataService` 加载的影像、高程与地理锚点固定为不可变数据，
在 `VoxelScene` 中生成 `source.type=earth` 的地形对象。它与目标使用同一个固定步、损伤事件、
Bullet 世界、发布事务、传感器历史和检查点。地理数据使用场景的 east/up/south 米制切平面，
保留 WGS84 椭球高、heading 与原始数据分辨率；静态地理根对象不能通过普通 place 命令
平移或旋转，需要调整场景 geo_anchor 后重新导入。分离后的碎块仍可进行刚体运动。

地形来源示例：

    "source": {
      "type": "earth", "map": "../earth/default.map.json",
      "earth": {
        "coverage": {"min_x": -40, "max_x": 40, "min_z": -40, "max_z": 40},
        "grid_size": 81, "imagery_size": 256, "strict": true
      },
      "cell_size_m": 2, "bottom_m": -220,
      "hd_subdivisions": 2, "mode": "hd"
    }

范围必须是完整体素格的整数倍，底面须低于工作区所有有效高程。省略 physics 时，
地形默认 static、允许断裂、固定初始底层；material 优先继承地图的温度、发射率、
反照率，再应用对象覆盖。雷达测量继承地图介电常数、粗糙度与 sigma0。
示例 `config/voxel/earth-workshop.vscene.json` 使用默认离线全球数据及显式有限深度。

### 两种模式与损伤

hd 模式将高程采样到与体素对齐的细网格，在剩余占据体素内裁剪高程表面，并生成
开挖底面、竖壁、洞顶与外边界。显示、CPU 射线、Bullet 静态三角网格和 GPU 测量
使用同一份裁剪几何，不再出现高清表面覆盖被挖空地形的问题。
voxel 模式则统一使用体素外露面和精确占据体并集碰撞。底面是显式模拟边界，
没有隐式无限平面、没有自动填补洞穴或在深部恢复原 DEM。

模式切换可用暂停编辑 `{"kind":"terrain_mode","object":"earth-terrain","mode":"voxel"}`，
也可增加 tick 作为运行事件。切换会同时替换显示、碰撞和测量几何，因而会改变几何真值，
并非只改变着色：体素顶面采用覆盖高程实体的格边界，垂向误差由格边长及格内地形起伏
共同决定；陡坡需要更小的体素，不能承诺所有坡面都只有一个格高以内的误差。
高清细分保留高程形状，不能制造原始 DEM 或影像没有的真实细节；原始采样间距和
Coarse/assumed 标记保留在 provenance 中。

可破坏质量以占据格体积和密度计量；高清地形为静态地表表示，不把裁剪薄层当作额外质量。
损伤、断裂后失去底部支撑的部分会转成体素动态碎块，保留质量、材质、影像映射与来源；
只有静态部分可使用 hd 三角网格碰撞，不能给动态凹形碎块错误地绑定静态 BVH。

### 影像与测量

普通地图现在也加载 GDALImage/TMSImage。支持带 CRS 的 Byte 灰度/RGB/RGBA 栅格，
或明确 global-geodetic 的 TMS 图层；高程格点与影像面积像素采用各自正确的采样约定。
有声明但缺失或透明的有效地表影像会导致加载失败。当前不解码调色板、非 Byte 彩色
或进行半透明层混合，应先转换为完整 RGB/RGBA。地形工作区必须有完整高程，
即使上游允许部分 NoData，也不会把缺测区填成实体地面。

影像按 sRGB 颜色解释，在地表平面投影映射到高清面、体素面与新暴露的开挖面。
显示采用 sRGB 纹理，测量纹理先解码到线性 albedo 并合并对象颜色，再上传 UNORM，
避免把 gamma 编码值直接当作反射率。不同大小的对象图集会在线性域重采样到共同层尺寸，
碎块复用同一影像槽；线性 8 bit 上传存在正常量化误差。影像只是可见光反照率的工程代理，
不等于辐射定标结果，也不用于从 RGB 推算地下材质或热温度。

`VoxelSensorSession` 在每个真实采集时刻验证严格地形覆盖。当前采用保守完整视锥检查：
地面视锥在显式底面上的投影必须落在一个已加载工作区内；跨地平线的混合视锥明确拒绝。
测量越出工作区不会被伪造为天空或空背景。应预先加载足够大的 ROI，或为明确的有限
试验台声明 strict=false；该选项不允许缺失高程。现阶段未自动将多个小工作区拼成任意
复杂的严格视锥覆盖域。跨几何版本的曝光/CPI 继续拒绝并计数，时钟和帧号不重置。

原生工坊新增“导入影像高程”“高清/体素切换”。有统一地形时隐藏旧预览地球，
地理拾取与高度查询切换到当前物理场景，只查询地形，避免把目标顶部误认为地面。
工作区外返回不可用，不退回未受损的背景地球。当前接入的是全球数据中的有界物理
工作区；整球远景已有独立 LOD 显示，尚未实现沿全球长航程自动分页、迁移损伤和
物理世界分区。这些不能用整个星球常驻密实体素来替代。

检查点以 earth_patches 表共享高度场、影像与来源，碎块只引用对应条目；恢复与切换
不再访问原始文件。新地形检查点带 face-bonds-bullet-earth-v1 模型标识，使旧程序
明确拒绝，而不是把高清地形误读为未裁剪体素。每对象 4M 占据格、整场景 16M 格，
每地形最多 1025² 高程样本、8 倍细分、1024² 影像，规范网格保持既有顶点预算，
地球数据/图集 128 MiB，传感器图集 128 MiB，磁盘检查点 256 MiB；超限整次事务失败。
未修改第三方锁定源码。Bullet 诊断射线改用毫米级 GJK，避免默认 subsimplex 在较大
合并块上的厘米级提前终止误差。

### 验收命令

    cmake --build build -j3
    ctest --test-dir build --output-on-failure
    build/tests/sim_voxel_earth_scene_tests --write-fixture build/voxel-earth-fixture
    build/app/vsgraysim_voxel_workshop build/voxel-earth-fixture/earth.vscene.json
    VSGRAYSIM_EARTH_DATA_ROOT=/path/to/World.earth.tms_repo build/voxel_scene run config/voxel/earth-workshop.vscene.json 60 /tmp/earth-60.json
    VSGRAYSIM_EARTH_DATA_ROOT=/path/to/World.earth.tms_repo build/app/vsgraysim_voxel_workshop config/voxel/earth-workshop.vscene.json

新增 CPU 测试使用真实 GeoTIFF 影像/高程、旋转地理锚点和原生高度图，覆盖双模式几何、
Bullet 交点/法线、目标落地、挖坑后下落、洞顶、断裂、质量、共享来源、资源发布回滚、
严格测量覆盖以及删掉源资产后的检查点续跑。新增 Vulkan 测试回读地形距离、温度、
发射率、南北影像颜色、sRGB 线性化与雷达回波，验证模式边界/CPI 门控和完全破坏。
独立库无需 GDAL 即可读取原生高度图或完整地形检查点；栅格导入仍需 GDAL adapter。

## 地球测试程序：地球、目标和云的组合验收

最终场景验收转入既有 `sim_earth_gui_tests`，不再用体素工坊替代组合验证。
场景文档 `config/earth/unified-scene.vscene.json` 加载真实影像/高程工作区、MQ-9 和
AH-64 的 OSGB 资产。MQ-9 是可断裂动态体，AH-64 沿预设速度穿越云场；
两者经同一导入器生成体素几何。地球程序同时保留全球远景、周围地表、日月与 GPU 体素云。
`makeTerrainContextDisplay` 将旧地表重投影到工作区坐标，并裁掉整个物理工作区的旧网格，
因此旧地球不会从坑洞中穿出；工作区外仅为可导航的显示上下文，仍不伪装成已加载物理真值。

可以在本机数据上生成含真实全球影像/高程根瓦片的显示数据包，再运行同一地球程序：

    export VSGRAYSIM_EARTH_DATA_ROOT=/path/to/World.earth.tms_repo
    cmake --build build --target earth_voxel_bake sim_earth_gui_tests -j3
    build/earth_voxel_bake config/earth/global-voxel.map.json build/earth-unified-dataset 25.078 121.772 7 9 1 -10000 --global-roots
    build/gui/sim_earth_gui_tests --unified-scene-capture build/earth-unified-acceptance config/earth/unified-scene.vscene.json build/earth-unified-dataset/earth.map.json
    build/gui/sim_earth_gui_tests --unified-scene-view config/earth/unified-scene.vscene.json build/earth-unified-dataset/earth.map.json

bake 输出目录须为新目录，不覆盖已有数据。交互窗口提供运行/暂停、单步、高清/体素、
地形毁伤、目标毁伤、云开关、地球轨道与工作区按钮，显示物理 tick、接触、云版本、
尾流目标数和热像/可见光测量计数。地图和场景可替换为其他已声明、覆盖完整的数据包。

组合验收在同一窗口执行 50 个固定步：高清→体素、挖坑、目标破坏与碎块接触，再切回高清；
每步产生一帧 MWIR 和 TV，测量距离与同一历史时刻的地形/目标真值比较。
同设备的 CloudGpuResources 同时供地球云渲染与传感器采样，另有无云参考链用于隔离云消光，
不能通过替换目标温度或几何制造“穿云”结果。图像、物理检查点和 JSON 报告写入输出目录。

云采用固定边界间的零阶保持：先完成该时间区间的测量，再执行下一个原生云计算步。
目标位置、质心速度经 ECEF 转到云的 east/up/north 图表；尾流采用模型包围半径加半格的
有限分辨率包络，传感器几何和刚体接触保持原体素形状。当前只注入最多 32 个驻留目标，
这是尾流强迫模型，不是已完成的双向机翼气动力求解。没有海洋模块时，云仍使用自己的风。

为缩短软件 Vulkan 验收，自动运行仅在指定探针帧呈现云颜色；每个物理步照常执行云计算、
尾流、接触和测量。交互运行每帧正常呈现。`VSGRAYSIM_EARTH_SCENE_DIAGNOSTIC=1` 仅生成
初始有云/无云诊断图，明确打印 DIAGNOSTIC，不执行也不宣称完整验收通过。

组合场景揭示并修复了两项显示问题：近地场景设定适当阴影级联距离/偏差以消除地面自阴影
条纹；云在没有时间累积时取消随帧号变化的抖动，使用确定性双点 Gauss 积分和更密的近场
采样。暂停时不仅验证云版本不变，还实际比较天空区域截图，防止“场没变但画面闪动”。

当前本机参考数据的原始高程间距约 2456 m、影像约 9784 m。高清模式是连续高程表面与
原始影像的正确呈现，并不代表米级源数据；需要更高精度时应替换影像和高程。
物理检查点仍只恢复地形、目标和事件，报告中的 GPU 云版本不是完整云动力学检查点。

本机组合功能验收产物为 `build/earth-unified-final/`，完整日志为
`build/earth-unified-final.log`：50 个物理步、50 帧 MWIR、50 帧 TV、111 个累计观测接触点，
目标成功产生独立碎块，云版本推进到 51，AH-64 在驻留云场内参与尾流强迫。
验收中的 MWIR/TV 使用独立的目标跟踪俯视相机（场景局部 Y=1500 m），
地球浏览相机可自由切换；一致性比较使用相同的传感器光轴与采集时刻，不要求浏览机位等同于传感器。
暂停比较 113544 个天空区域像素，变化数为 0。截图包括整球、高清、体素、挖坑、目标断裂
及云中航空器。报告明确记载云颜色仅在探针帧绘制、云计算与测量每步执行。

功能验收与视觉审查分别记录：`earth-scene-report.json` 保存可复核的运行指标，
`earth-visual-review.json` 记录仍存在的云近景 LOD 分层外观，因此本轮不宣称云的最终视觉
质量已通过。地面自阴影条纹与帧间云抖动已经修复，近景云形态仍是后续真实地球场景中的
待修项。使用 Mesa llvmpipe 软件 Vulkan；本机 NVIDIA 再次复现逻辑设备创建失败
VkResult=-3，记录在 `build/earth-unified-nvidia.log`，未报告 NVIDIA 硬件通过。
本机可在上述地球程序命令前增加
`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json LP_NUM_THREADS=2`
复现软件 Vulkan 验收和交互界面；驱动恢复后应再执行完整硬件验收。

收尾验证：完整工程构建通过，主工程 28/28 项 CTest 和独立库 6/6 项 CTest 通过，
其中既有地球 GUI 契约新增了工作区裁剪与周围地表保留测试。
日志为 `build/earth-unified-final-build.log`、`build/earth-unified-ctest.log` 和
`build/earth-unified-standalone-ctest.log`。最终场景验收依据仍为上述地球程序的实际运行及视觉审查。

### 2026-11-18：显控 F-16C 贴图式仪表面板

`AircraftCockpitPanel` 默认使用 `TexturedCockpit`，通过 `CockpitProfile` 读取机型数据。
启动入口为 `./build/app/vsgraysim_control --workmode=aircraft`。F-16C 风格底图与透明
操作杆/油门图集保存在 `assets/cockpit/f16c/`，由内置 image_gen 原创生成；完整提示词与
用途记录在 `generation.json`，没有提取 DCS 游戏素材。资源随 `vsgraysim_assets` 同步到
构建目录，机型 JSON 的相对路径在源码目录和构建部署目录均可解析。

```bash
cmake --build build -j3
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=1 QT_FONT_DPI=96 \
./build/gui/sim_aircraft_cockpit_tests --native-capture \
  build/f16c-native build/earth-unified-dataset/earth.map.json
```

原生验收使用实际产品 `ControlWorkbench("aircraft")`、贴图面板和 PlatformManager/Bullet，
用鼠标操作杆经过真实 BrokerNet 命令链路改变飞机姿态，验证松手回中与暂停重绘不推进物理。
该模式按真实 Qt 窗口抓取仪表面板，不启动隐藏 Vulkan 地球，因此不作为外景/云的视觉验收。
可复核结果为 `build/f16c-native/f16c-acceptance.json`，窗口与仪表截图位于同目录；
日志为 `build/f16c-native.log`。离屏 `display_aircraft_cockpit` 同时覆盖机型配置校验与回滚、
可变仪表量程、独立数据绑定、缺失通道、热区/比例缩放、实体 MFD 按键、需求/实际分离、
操作杆回中、油门保持及既有控制权、失焦、离线、采样过期和工作台路由契约。

构建、回归、受保护文件复核分别记录在 `build/textured-cockpit-full-build.log`、
`build/textured-cockpit-ctest.log`、`build/textured-cockpit-architecture.json`。
受保护范围为 `include/vsgraysim/earth`、`src/earth`、`lib/voxel_earth`、`env_modules`、
`shaders/env`、`shaders/viewport` 共 209 个文件；本轮仅改变显控、视口表现与资源打包。
相机仍复用既有地球/HUD 特性，通用三维舱框可独立关闭。隐藏本机标记不会再反复创建删除
空标记节点，避免遥测刷新触发全图重编译。具体模拟范围与尚未接入的机载通道见
`docs/product_workbench_integration.md`。

F-16 使用侧杆的布局参考美国空军资料：
https://www.af.mil/About-Us/Fact-Sheets/Display/Article/104505/f-16-fighting-falcon/
面板美术为 F-16C 风格原型，不宣称已逐项还原某批次座舱或完整 DCS 航电。
