# -----------------------------------------------------------------------------
# rocky_env — 环境渲染模块库 (sky / cloud / ocean)。
#
# 地球视口共享天空、云和海面。体素天空使用 VSG，Rocky 兼容后端按构建选项启用。
# -----------------------------------------------------------------------------
add_library(vsgraysim_rocky_env STATIC
    src/sensor/ShaderPath.cpp
    src/sensor/SensorPipeline.cpp    # S1: GPU 传感器链 RTT 级联 (rad→optics→det→elec)
    src/sensor/GpuContext.cpp
    src/sensor/DynamicScene.cpp
    src/sensor/GpuSensorRuntime.cpp
    src/sensor/BandRT.cpp            # B2/P1.1: 波段着色 compute (band_shade 消费 G-buffer)
    src/sensor/GBufferPool.cpp       # P1.1: 全场景唯一求交 G-buffer 池 (单 TraceRays)
    src/sensor/SharedSceneResources.cpp # P0: 场景级资源池 (几何/BLAS/TLAS/LUT/env UBO 一份)
    src/sensor/SceneSource.cpp       # S2: 场景数据源抽象 (JSON 驱动 + 热属性 → VSG 几何)
    src/sensor/SensorInstance.cpp    # B1: 传感器实例模型 (SensorDesc→Profile + 场景驱动注册)
    src/sensor/EnvPhysicsService.cpp # B3: 环境契约实现者 (IEnvStack→波段级 EnvAnswer)
    src/sensor/SensorScheduler.cpp   # B4: 传感器帧调度 (成像节拍/CPI 边界事件)
    src/sensor/GimbalModel.cpp       # B7: 扫描机构纯函数组件 (8 模式 LOS(t) + 状态机 + 相干性校验)
    src/sensor/RadarChain.cpp        # B6/P1.1: 雷达回波链 GPU 前端 (G-buffer 散射 + 回波合成 compute)
    src/sensor/RadarStages.cpp       # B6: 雷达段处理 CPU 装配 (脉压/RD 图/幅度图)
)

target_include_directories(vsgraysim_rocky_env
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# 场景数据源依赖 (SceneSource.cpp 私有; 公共头 SceneSource.h 不暴露):
#   nlohmann 单头 JSON (JSON_INCLUDE_DIR, 顶层缓存) + GeoFrame WGS84/ENU
#   (lib/simcore 共享数学头, 不依赖 GUI)。
target_include_directories(vsgraysim_rocky_env
    PRIVATE ${JSON_INCLUDE_DIR}
)

# VSG 是所有管线的底座 (PUBLIC: EnvApi.h 暴露 vsg 类型)。
target_link_libraries(vsgraysim_rocky_env PUBLIC vsgraysim_earth_environment)
target_compile_definitions(vsgraysim_rocky_env PUBLIC VSGRAYSIM_WITH_ROCKY=$<BOOL:${VSGRAYSIM_WITH_ROCKY}>)

set_target_properties(vsgraysim_rocky_env PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- 冒烟测试 (无窗口): Sky 链路 (Rocky 单后端) ----
add_executable(env_smoke_sky tests/smoke_sky.cpp)
# 直调 rocky (VSGContextFactory), 需 rocky 头与库。
target_link_libraries(env_smoke_sky PRIVATE vsgraysim_rocky_env ${VSGRAYSIM_ROCKY_TARGET})
# rocky->gdal->geos 传递依赖: 与 app/ 相同的 RPATH 策略 (DT_RPATH 覆盖传递 NEEDED)。
set_target_properties(env_smoke_sky PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "$ORIGIN/../lib;${VSGRAYSIM_ENGINE_RPATH_DIRS};${ROCKY_EARTH3P_DIR}/lib")
target_link_options(env_smoke_sky PRIVATE "-Wl,--disable-new-dtags")

# ---- 冒烟测试 (无窗口): Ocean 波场/采样/控制链路 ----
add_executable(env_smoke_ocean tests/smoke_ocean.cpp)
target_link_libraries(env_smoke_ocean PRIVATE vsgraysim_rocky_env)
set_target_properties(env_smoke_ocean PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "$ORIGIN/../lib;${VSGRAYSIM_ENGINE_RPATH_DIRS};${ROCKY_EARTH3P_DIR}/lib")
target_link_options(env_smoke_ocean PRIVATE "-Wl,--disable-new-dtags")

# ---- 冒烟测试 (无窗口): Cloud 多层/控制/查询链路 ----
add_executable(env_smoke_cloud tests/smoke_cloud.cpp)
target_link_libraries(env_smoke_cloud PRIVATE vsgraysim_rocky_env)
set_target_properties(env_smoke_cloud PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "$ORIGIN/../lib;${VSGRAYSIM_ENGINE_RPATH_DIRS};${ROCKY_EARTH3P_DIR}/lib")
target_link_options(env_smoke_cloud PRIVATE "-Wl,--disable-new-dtags")

# ---- 独立可视化验证程序 (窗口): 环境三模块 + 多传感器仿真渲染 (合并版) ----
# 纯 VSG, 不依赖 Qt / vsgraysim GUI 框架; Rocky ECEF 地球基座 (自建
# VSGContext, sky 为 ECEF 世界根, 云/海挂锚点 ENU 内容根)。
# 传感器分支 (--scene/--band): 蓝方物理 (P0 辐射亮度) + 红方信号级
# (光学/探测器/电子学) GPU RTT 级联, 观察口 (左) + 传感器视口 (右) 双视口。
if(VSGRAYSIM_WITH_ROCKY)
add_executable(cloud_viewpoints tools/cloud_viewpoints.cpp)
target_link_libraries(cloud_viewpoints PRIVATE vsgraysim_rocky_env vsgraysim_earth_environment vsgraysim_core vsg::vsg)
target_include_directories(cloud_viewpoints PRIVATE "${CMAKE_SOURCE_DIR}/env_modules/src" "${CMAKE_SOURCE_DIR}/lib/voxel_earth/src" "${CMAKE_SOURCE_DIR}/include")

add_executable(voxel_model_states tools/voxel_model_states.cpp)
target_link_libraries(voxel_model_states PRIVATE vsgraysim_core vsgraysim_voxel_earth vsg::vsg)
target_include_directories(voxel_model_states PRIVATE "${CMAKE_SOURCE_DIR}/env_modules/src" "${CMAKE_SOURCE_DIR}/lib/voxel_earth/src" "${CMAKE_SOURCE_DIR}/include")

add_executable(env_viewer tools/env_viewer.cpp)
target_compile_definitions(env_viewer PRIVATE
    VSGRAYSIM_ENV_VIEWER_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../fonts/NotoSansCJKSC.ttf")
# 直调 rocky (VSGContextFactory) + GeoFrame.h 纯数学共享头 (头文件级复用, 不链接 gui);
# vsgraysim_objloader: 传感器分支 OBJ 场景加载; vsgraysim_product_dispatch:
# 传感器分支产品发布 (EquipmentRegistry / ProductDispatcher)。
target_link_libraries(env_viewer PRIVATE vsgraysim_rocky_env vsgraysim_objloader
    vsgraysim_product_dispatch ${VSGRAYSIM_ROCKY_TARGET} vsg::vsg vsgXchange::vsgXchange
    vsgImGui::vsgImGui)   # 方案A: 半透明 TV/MWIR 切换按钮 overlay
target_include_directories(env_viewer PRIVATE
    ${JSON_INCLUDE_DIR})   # --map/--scene JSON 解析 (nlohmann 单头)
# X11: 仅用于截图验证 (可选; 缺失时程序自动降级, 不影响渲染)。
find_package(X11 QUIET)
if(X11_FOUND)
    target_link_libraries(env_viewer PRIVATE X11::X11)
    target_compile_definitions(env_viewer PRIVATE ENV_VIEWER_HAVE_X11=1)
endif()
# xcb: 用于读取 VSG Xcb_Window 回写的原生窗口句柄并校验窗口映射状态。
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB QUIET IMPORTED_TARGET xcb)
endif()
if(XCB_FOUND)
    target_link_libraries(env_viewer PRIVATE PkgConfig::XCB)
    target_compile_definitions(env_viewer PRIVATE ENV_VIEWER_HAVE_XCB=1)
endif()
set_target_properties(env_viewer PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "$ORIGIN/../lib;${VSGRAYSIM_ENGINE_RPATH_DIRS};${ROCKY_EARTH3P_DIR}/lib")
target_link_options(env_viewer PRIVATE "-Wl,--disable-new-dtags")
endif()

# ---- 最小 VSG 窗口探针 (定位渲染链路问题, 非交付物) ----
