# ---------------------------------------------------------------------------
# vsgraysim 进程入口点
#
# 每个软件为独立进程, 通过 QProcess 由 Launcher 启动管理。
# 所有 GUI 窗口代码在 vsgraysim_gui_lib (STATIC) 中,
# 3D 视口代码在 vsgraysim_viewport3d (OBJECT) 中。
# -----------------------------------------------------------------------------

# ---- 1. Launcher 进程管理器 ----
add_executable(vsgraysim_launcher
    launcher/main.cpp
)
target_link_libraries(vsgraysim_launcher PRIVATE
    vsgraysim_gui_lib
    vsgraysim_algo
    vsgraysim_ipc
    vsgraysim_telemetry
    vsgraysim_common
    Qt6::Widgets
)
set_target_properties(vsgraysim_launcher PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)

# ---- 2. 场景设计软件 (含地形编辑能力: 程序化生成/笔刷/水面/分析/工程存取) ----
add_executable(vsgraysim_scene_designer
    scene_designer/main.cpp
)
target_link_libraries(vsgraysim_scene_designer PRIVATE
    vsgraysim_gui_lib
    vsgraysim_viewport3d
    vsgraysim_algo
    vsgraysim_core
    vsgraysim_ipc
    vsgraysim_telemetry
    vsgraysim_common
    Qt6::Widgets
)
set_target_properties(vsgraysim_scene_designer PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)

# ---- 4. 仿真控制软件 ----
add_executable(vsgraysim_sim_control
    sim_control/main.cpp
)
target_link_libraries(vsgraysim_sim_control PRIVATE
    vsgraysim_gui_lib
    vsgraysim_viewport3d
    vsgraysim_algo
    vsgraysim_core
    vsgraysim_ipc
    vsgraysim_telemetry
    vsgraysim_platform
    vsgraysim_common
    Qt6::Widgets
)
set_target_properties(vsgraysim_sim_control PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)

# ---- 5. 统一显控工作台 (装订式工作模式, 由导航页以 --assemble 启动) ----
add_executable(vsgraysim_control
    control/main.cpp
)
target_link_libraries(vsgraysim_control PRIVATE
    vsgraysim_gui_lib
    vsgraysim_algo
    vsgraysim_core
    vsgraysim_ipc
    vsgraysim_telemetry
    vsgraysim_common
    Qt6::Widgets
)
set_target_properties(vsgraysim_control PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)

# ---- 5b. rocky_env 环境模块验证台 (sky / cloud / ocean) ----
# 独立 VSG+Qt 测试程序: 分别/组合加载三个环境模块, 面板驱动全部 Control/Query
# 接口。rocky 地球底座 (rocky_map, 内置离线粗底图, 无需地理数据/网络)。
add_executable(vsgraysim_env_lab
    env_lab/main.cpp
)
target_link_libraries(vsgraysim_env_lab PRIVATE
    vsgraysim_gui_lib
    vsgraysim_viewport3d
    vsgraysim_algo
    vsgraysim_core
    vsgraysim_ipc
    vsgraysim_telemetry
    vsgraysim_common
    Qt6::Widgets
)
set_target_properties(vsgraysim_env_lab PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)
# EnvLabWindow.h 位于 gui/viewport3d/ (随 vsgraysim_viewport3d 目标编译)。
target_include_directories(vsgraysim_env_lab PRIVATE
    "${CMAKE_SOURCE_DIR}/gui/viewport3d")

# ---- 体素场景工坊: authoring / deterministic runtime / checkpoints ----
add_executable(vsgraysim_voxel_workshop voxel_workshop/main.cpp)
target_link_libraries(vsgraysim_voxel_workshop PRIVATE
    vsgraysim_gui_lib vsgraysim_viewport3d vsgraysim_core
    vsgraysim_algo vsgraysim_ipc vsgraysim_telemetry vsgraysim_common Qt6::Widgets)
set_target_properties(vsgraysim_voxel_workshop PROPERTIES AUTOMOC ON AUTOUIC ON)

# ---- 公共 RPATH 设置 ----
foreach(_tgt vsgraysim_launcher vsgraysim_scene_designer
             vsgraysim_sim_control vsgraysim_control vsgraysim_voxel_workshop)
    set_target_properties(${_tgt} PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib;${VSGRAYSIM_GUI_RPATH_DIRS}"
        BUILD_RPATH "$ORIGIN;${VSGRAYSIM_GUI_RPATH_DIRS}")
    # Use DT_RPATH (old dtag) instead of DT_RUNPATH so the rpath also applies
    # to transitive dependencies (librocky -> libgdal -> libgeos_c; vsgBullet ->
    # Bullet libs). DT_RUNPATH only resolves direct NEEDED entries.
    target_link_options(${_tgt} PRIVATE "-Wl,--disable-new-dtags")
    target_include_directories(${_tgt} PRIVATE
        "${QT6_PREFIX}/include/QtGui/6.12.0/QtGui")
    target_compile_definitions(${_tgt} PRIVATE
        QT6_PLUGIN_PATH="${QT6_PREFIX}/plugins")
    add_dependencies(${_tgt} vsgraysim_assets)
endforeach()

# env_lab 单独设置 (同样的 RPATH 处理; rocky→gdal→geos 传递依赖需要 DT_RPATH)。
if(TARGET vsgraysim_env_lab)
    set_target_properties(vsgraysim_env_lab PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib;${VSGRAYSIM_GUI_RPATH_DIRS}"
        BUILD_RPATH "$ORIGIN;${VSGRAYSIM_GUI_RPATH_DIRS}")
    target_link_options(vsgraysim_env_lab PRIVATE "-Wl,--disable-new-dtags")
    target_include_directories(vsgraysim_env_lab PRIVATE
        "${QT6_PREFIX}/include/QtGui/6.12.0/QtGui")
    target_compile_definitions(vsgraysim_env_lab PRIVATE
        QT6_PLUGIN_PATH="${QT6_PREFIX}/plugins")
    add_dependencies(vsgraysim_env_lab vsgraysim_assets)
endif()
# Scene topics use the isolated CPU model compiler; deploy it beside products.
add_dependencies(vsgraysim_scene_designer voxel_scene)
