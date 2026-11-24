cmake_minimum_required(VERSION 3.22)

# CUDA toolkit 必须在 project() 之前给出默认路径: project() 启用 CUDA 语言时
# 就要定位 nvcc, 而新构建目录 (如 Qt Creator 的 Release kit) 没有缓存条目,
# 若等到下面 set(CUDAToolkit_ROOT ...) 才赋值就已经晚了。
set(THIRD_PARTY_ROOT "/home/mte/Code" CACHE PATH "Third-party install root")
if(NOT CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_COMPILER "${THIRD_PARTY_ROOT}/Cuda/cuda-12.4/bin/nvcc"
        CACHE FILEPATH "CUDA compiler")
endif()
if(NOT CUDAToolkit_ROOT)
    set(CUDAToolkit_ROOT "${THIRD_PARTY_ROOT}/Cuda/cuda-12.4"
        CACHE PATH "CUDA Toolkit")
endif()
# 架构列表在 project() 之前设置, 避免 CMake 在识别阶段探测本机架构
set(CMAKE_CUDA_ARCHITECTURES "75;86;89;90" CACHE STRING "CUDA architectures")

project(vsgraysimulation LANGUAGES CXX CUDA)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

# -----------------------------------------------------------------------------
# Dependency roots (pre-installed third-party tree at /home/mte/Code)
# -----------------------------------------------------------------------------

# Vulkan SDK
set(VULKAN_SDK "${THIRD_PARTY_ROOT}/vulkan/1.3.296.0/x86_64" CACHE PATH "Vulkan SDK")
set(ENV{VULKAN_SDK} "${VULKAN_SDK}")
# vsg 依赖 glslang / SPIRV-Tools, 它们都装在 Vulkan SDK 的 cmake 目录下,
# 加入 CMAKE_PREFIX_PATH 让 find_package 能一并解析
list(PREPEND CMAKE_PREFIX_PATH "${VULKAN_SDK}")

# CUDA toolkit (默认值已在 project() 之前设置)

# The engine family is always compiled from repository-owned source trees.
# External SDK roots are configured above and recorded in dependency-report.json.
option(VSGRAYSIM_BUILD_GUI "Build Qt software front-ends" ON)
include(cmake/Dependencies.cmake)

# -----------------------------------------------------------------------------
# OpenUSD (可选): USD<->VSG 场景桥接 (见 lib/usd_bridge)。默认 OFF —— 开启需要
# 本机存在完整构建/安装、且暴露 lib/cmake/pxr 包的 OpenUSD。当前开发机只有半截
# 构建树 (仅 pxr/base 层编译, usd/sdf/ar/usdGeom/usdShade/... 功能层尚未编译),
# 故默认关闭; 打开后 pxr 仅由桥接库 PRIVATE 链入 (ABI 隔离, 不外泄 Boost/TBB/half)。
# -----------------------------------------------------------------------------
option(VSGRAYSIM_ENABLE_USD
       "Build the OpenUSD<->VSG scene bridge (requires a full OpenUSD install exposing the pxr CMake package)"
       OFF)
if(VSGRAYSIM_ENABLE_USD)
    set(USD_ROOT "${THIRD_PARTY_ROOT}/openusd" CACHE PATH "OpenUSD install prefix containing lib/cmake/pxr")
    list(PREPEND CMAKE_PREFIX_PATH "${USD_ROOT}")
    set(pxr_DIR "${USD_ROOT}/lib/cmake/pxr" CACHE PATH "OpenUSD pxr CMake package directory")
    find_package(pxr REQUIRED)
    # pxrTargets.cmake 导出的真实 CMake 目标名 (usd/usdGeom/... 而非 libusd_usd)。
    set(VSGRAYSIM_PXR_LIBS
        usd usdGeom usdShade usdLux usdPhysics usdSkel usdUtils
        sdf ar pcp kind sdr gf vt tf arch plug work)
endif()

# -----------------------------------------------------------------------------
# 自研 OBJ 加载器 (vsgXchange 无 assimp 的兜底, 极小静态库):
# 先于 env_modules 定义, 供 vsgraysim_core 与 env_viewer 共享单一实现。
# -----------------------------------------------------------------------------
add_subdirectory(lib/objloader)
add_subdirectory(lib/simcore)

# -----------------------------------------------------------------------------
# vsgraysim_usd_bridge — OpenUSD<->VSG 场景桥接 (见 lib/usd_bridge/CMakeLists.txt)。
# 始终构建: ENABLE_USD=OFF 时编译安全桩 (仅 vsg/STL, 不触 pxr), ON 时 PRIVATE 链入 pxr。
# -----------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/usd_bridge/CMakeLists.txt")
    add_subdirectory(lib/usd_bridge)
elseif(VSGRAYSIM_ENABLE_USD)
    message(FATAL_ERROR "USD bridge sources are absent; configure with VSGRAYSIM_ENABLE_USD=OFF")
endif()

# -----------------------------------------------------------------------------
# vsgraysim_product_dispatch — B5 分发时序桥接 (ProductFrame → 传输帧映射 +
# stages/链接/供电门控)。先于 env_modules 定义, env_viewer 与后续仿真控制
# 软件共享单一实现 (见 lib/product_dispatch/CMakeLists.txt)。
# -----------------------------------------------------------------------------
add_subdirectory(lib/product_dispatch)

# -----------------------------------------------------------------------------
# rocky_env: sky / cloud / ocean environment modules (independent static lib;
# see env_modules/). The voxel sky uses VSG; Rocky support is optional.
# -----------------------------------------------------------------------------
set(VSGRAYSIM_EARTH_PRODUCT_ADAPTERS ON)
add_subdirectory(lib/voxel_earth)
add_subdirectory(env_modules)

# -----------------------------------------------------------------------------
# Global include dirs
# -----------------------------------------------------------------------------
add_library(vsgraysim_common INTERFACE)
target_link_libraries(vsgraysim_common INTERFACE vsgraysim_simcore)
target_include_directories(vsgraysim_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${JSON_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${BULLET_INCLUDE_DIRS}
)
target_link_libraries(vsgraysim_common INTERFACE
    vsg::vsg
    vsgXchange::vsgXchange
    Vulkan::Vulkan
    CUDA::cudart
    CUDA::cuda_driver
    Threads::Threads
    vsgImGui::vsgImGui
    ${BULLET_LIBRARIES}
    vsgbDynamics
    vsgbCollision
    ${VSGRAYSIM_BULLET_EXTRA_LIBRARIES}
    ZLIB::ZLIB
)

# -----------------------------------------------------------------------------
# CUDA signal processing library (Layer 3 kernels)
# -----------------------------------------------------------------------------
add_library(vsgraysim_cuda SHARED
    cuda/signal_processing.cu
)
target_link_libraries(vsgraysim_cuda PRIVATE vsgraysim_common)
target_include_directories(vsgraysim_cuda PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(vsgraysim_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

# -----------------------------------------------------------------------------
# Core simulation static library (Layers 0-5 + kernel + broker)
# -----------------------------------------------------------------------------
file(GLOB_RECURSE VSGRAYSIM_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layer0_phys/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layer1_scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layer2_rt/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layer3_cuda/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layer3b_signal/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layer4_sched/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layer5_vis/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Simulation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/broker/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/products/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/project/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/terrain/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/earth/VoxelTerrainDebris.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/earth/VoxelSensorSession.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/earth/VoxelEquipmentRuntime.cpp"
)

add_library(vsgraysim_core STATIC ${VSGRAYSIM_SRC})
target_include_directories(vsgraysim_core PUBLIC "${SIM_GDAL_INCLUDE}")
target_link_libraries(vsgraysim_core PUBLIC "${SIM_GDAL_LIBRARY}")
# BrokerNet (src/broker) 依赖 IPC 传输层 (ShmTransport) 实现跨进程链路网关
target_link_libraries(vsgraysim_core PUBLIC vsgraysim_voxel_earth vsgraysim_common vsgraysim_cuda vsgraysim_ipc
                                            vsgraysim_objloader vsgraysim_rocky_env zip)
if(TARGET vsgraysim_usd_bridge)
    target_link_libraries(vsgraysim_core PUBLIC vsgraysim_usd_bridge)
endif()

add_executable(voxel_scene tools/voxel_scene.cpp)
target_link_libraries(voxel_scene PRIVATE vsgraysim_voxel_earth)
target_include_directories(voxel_scene PRIVATE ${BULLET_INCLUDE_DIRS})
set_target_properties(voxel_scene PROPERTIES BUILD_RPATH "${ROCKY_EARTH3P_DIR}/lib")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(voxel_scene PRIVATE "-Wl,--disable-new-dtags")
endif()

add_executable(earth_voxel_bake tools/earth_voxel_bake.cpp)
target_link_libraries(earth_voxel_bake PRIVATE vsgraysim_core)
set_target_properties(earth_voxel_bake PROPERTIES BUILD_RPATH "${ROCKY_EARTH3P_DIR}/lib")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(earth_voxel_bake PRIVATE "-Wl,--disable-new-dtags")
endif()

# env_viewer (env_modules 内定义, 合并了原 sensor_viewer) 的传感器分支消费
# SimDataBroker/EquipmentRegistry (无 Qt), 其实现在 vsgraysim_core (src/broker);
# vsgraysim_cuda 传递提供 CUDAToolkit include (GpuBuffer.h 需 cuda_runtime.h)。
# CMP0079 NEW 允许跨目录补链。
if(TARGET env_viewer)
    target_link_libraries(env_viewer PRIVATE vsgraysim_core)
endif()

# -----------------------------------------------------------------------------
# (The legacy distributed system framework src/system + src/systems, the
#  DDS/protobuf transport layer and the console entry points were removed.
#  The Qt6 GUI (vsgraysim_gui: launcher + terrain / scene / simulation /
#  radar / SAR / infrared modules) is the only front-end; cross-software
#  data distribution is handled by SimDataBroker (GPU handle transfer).)
# -----------------------------------------------------------------------------

# Bake RPATHs into the binary so users do not have to manually set
# LD_LIBRARY_PATH for VSG / Vulkan / CUDA / vsgXchange / project libs.
set(_rpath_dirs
    "${CMAKE_BINARY_DIR}"
    "${VULKAN_SDK}/lib"
    ${VSGRAYSIM_ENGINE_RPATH_DIRS}
    "${BULLET_ROOT}/lib"
    "${ROCKY_EARTH3P_DIR}/lib"
)
if(CUDAToolkit_FOUND)
    list(APPEND _rpath_dirs "${CUDAToolkit_LIBRARY_DIR}")
endif()
if(VSGRAYSIM_ENABLE_USD)
    list(APPEND _rpath_dirs "${USD_ROOT}/lib")
endif()
list(REMOVE_DUPLICATES _rpath_dirs)
file(RELATIVE_PATH _rel "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}")
set_target_properties(vsgraysim_cuda PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib"
    BUILD_RPATH "$ORIGIN;${_rpath_dirs}")

# -----------------------------------------------------------------------------
# GLSL -> SPIR-V assets from the selected Vulkan SDK
# -----------------------------------------------------------------------------
find_program(GLSLC glslangValidator HINTS "${VULKAN_SDK}/bin" REQUIRED)
set(RT_SHADER_SPVS)
foreach(_src raygen.rgen closesthit.rchit miss.rmiss)
    set(_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_src}")
    set(_spv "${CMAKE_BINARY_DIR}/shaders/${_src}.spv")
    add_custom_command(
        OUTPUT "${_spv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${GLSLC}" --target-env vulkan1.2 "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                -V -o "${_spv}" "${_shader}"
        DEPENDS "${_shader}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vsgraysim_glsl_structs.glsl"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere_common.glsl"
        COMMENT "Compiling ${_src} -> SPIR-V" VERBATIM)
    list(APPEND RT_SHADER_SPVS "${_spv}")
endforeach()

# -----------------------------------------------------------------------------
# Install shaders & config next to the binary
# -----------------------------------------------------------------------------
add_custom_target(vsgraysim_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        ${CMAKE_BINARY_DIR}/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        ${CMAKE_BINARY_DIR}/models
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/workspace
        ${CMAKE_BINARY_DIR}/workspace
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        ${CMAKE_BINARY_DIR}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_CURRENT_SOURCE_DIR}/config
            ${CMAKE_CURRENT_SOURCE_DIR}/workspace ${CMAKE_CURRENT_SOURCE_DIR}/fonts ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${RT_SHADER_SPVS}
)

# -----------------------------------------------------------------------------
# 算法抽象层 (不依赖 Qt)
# -----------------------------------------------------------------------------
add_subdirectory(lib/algo)

# -----------------------------------------------------------------------------
# IPC 通信层 (不依赖 Qt)
# -----------------------------------------------------------------------------
add_subdirectory(lib/ipc)

# -----------------------------------------------------------------------------
# 遥测库 (不依赖 Qt)
# -----------------------------------------------------------------------------
add_subdirectory(lib/telemetry)

# -----------------------------------------------------------------------------
# 平台运动模型库 (不依赖 Qt)
# -----------------------------------------------------------------------------
add_subdirectory(lib/platform)
if(BUILD_TESTING)
    add_executable(scene_package tools/scene_package.cpp)
    target_link_libraries(scene_package PRIVATE vsgraysim_core)
    set_target_properties(scene_package PROPERTIES BUILD_RPATH "${ROCKY_EARTH3P_DIR}/lib")
    target_link_options(scene_package PRIVATE "-Wl,--disable-new-dtags")
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Qt6 GUI front-end
# -----------------------------------------------------------------------------
if(VSGRAYSIM_BUILD_GUI)
    # Qt6 查找 (在 root 中调用, 确保子目录均可见 Qt6::Widgets 等目标)
    # 跳过 Qt 插件自动包含: 官方 Qt 二进制的 Gui 插件依赖 Qt6WaylandClient,
    # 而本机未安装 Wayland 开发库, 插件链会导致 find_package(Qt6 Gui) 失败。
    set(QT_SKIP_AUTO_PLUGIN_INCLUSION ON CACHE BOOL "Skip Qt auto plugin inclusion")
    set(QT6_ROOT "${THIRD_PARTY_ROOT}/Qt6" CACHE PATH "Qt6 install root")
    set(QT6_PREFIX "${QT6_ROOT}/6.12.0/gcc_64" CACHE PATH "External Qt SDK prefix")
    set(Qt6_DIR "${QT6_PREFIX}/lib/cmake/Qt6" CACHE PATH "Qt6 CMake package directory")
    find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets)

    # Qt6 RPATH (供 gui/ 和 app/ 共用)
    set(VSGRAYSIM_GUI_RPATH_DIRS
        "${CMAKE_BINARY_DIR}"
        "${VULKAN_SDK}/lib"
        ${VSGRAYSIM_ENGINE_RPATH_DIRS}
        "${BULLET_ROOT}/lib"
        "${QT6_PREFIX}/lib"
        "${ROCKY_EARTH3P_DIR}/lib"
    )
    if(CUDAToolkit_FOUND)
        list(APPEND VSGRAYSIM_GUI_RPATH_DIRS "${CUDAToolkit_LIBRARY_DIR}")
    endif()
    if(VSGRAYSIM_ENABLE_USD)
        list(APPEND VSGRAYSIM_GUI_RPATH_DIRS "${USD_ROOT}/lib")
    endif()
    list(REMOVE_DUPLICATES VSGRAYSIM_GUI_RPATH_DIRS)

    add_subdirectory(gui)
    add_subdirectory(app)
endif()

vsgraysim_write_dependency_report()
