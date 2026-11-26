# -----------------------------------------------------------------------------
# Qt6 GUI front-end for vsgraysim
#
# Module windows GUI:
#   - Qt6 Widgets (dark theme) instead of the old vsgImGui front-end
#   - Launcher (start page) -> 6 process executables (defined in app/)
#
# This CMakeLists.txt builds 2 shared components:
#   1. vsgraysim_viewport3d  (OBJECT library: 3D viewport features)
#   2. vsgraysim_gui_lib     (STATIC library: all GUI window code)
#
# The actual executables (vsgraysim_launcher, vsgraysim_scene_designer, etc.)
# are defined in ../app/CMakeLists.txt and link against these libraries.
# -----------------------------------------------------------------------------

# Qt6 已在顶层 CMakeLists.txt 中 find_package, 此处直接使用目标。
# -----------------------------------------------------------------------------
# gui/base: 三软件共用基础 UI 库 (主题/控件工厂/Dock 构建/窗口基类)
# -----------------------------------------------------------------------------
set(BASE_UI_SOURCES
    base/ThemeManager.cpp
    base/WidgetFactory.cpp
    base/DockBuilder.cpp
    base/BaseWindow.cpp
    base/PropertyTreeWidget.cpp
)

set(GUI_SOURCES
    LauncherWindow.cpp
    HealthPanel.cpp
    SceneDesignerWindow.cpp
    voxel/SceneAuthoringPanel.cpp
    SimulationControlWindow.cpp
    SimulationWorkerClient.cpp
    SimulationEngineWorker.cpp
    ${BASE_UI_SOURCES}
    PreviewRenderer.cpp
    AssetBrowserWidget.cpp
    SceneStartDialog.cpp
    SimStartDialog.cpp
    EquipmentLinkDialog.cpp
    PpiDisplay.cpp
    # ---- 显控工作台实际接入的后处理 (雷达面板) ----
    CfarProcessor.cpp
    MtiMtdProcessor.cpp
    TrackManager.cpp
    ScrollableImageView.cpp
    SimMonitorPanels.cpp
    # ---- 场景设计器专业增强 ----
    SceneValidator.cpp
    TerrainAnalyzer.cpp
    # ---- 显控工作台 (装订式多装备协同显控) ----
    display/WorkMode.cpp
    display/EquipmentSession.cpp
    display/ControlWorkbench.cpp
    display/VoxelEquipmentWorkbench.cpp
    display/WorkspaceProfile.cpp
    display/FlightPanel.cpp
    display/EarthPanel.cpp
    display/AircraftCockpitPanel.cpp
    display/CockpitControlSignals.cpp
    display/CockpitProfile.cpp
    display/PlatformInstruments.cpp
    display/ConsoleSelector.cpp
    display/JoystickInput.cpp
    display/SensorPanels.cpp
    display/SonarDisplayPanel.cpp
    display/PanelProcessing.cpp
    display/RadarTerrainMap.cpp
    display/EquipmentViewProcesses.cpp
    display/RelationshipGraphView.cpp
)

# -----------------------------------------------------------------------------
# viewport3d: 三软件共用 3D 视口库 (可配置特性组装, 见 gui/viewport3d/)
#
# 用 OBJECT 库而非 STATIC: 特性的静态注册器 (VSGVP_REGISTER_FEATURE 全局对象)
# 若打进归档可能被链接器当未引用符号剥离, OBJECT 库目标文件直接链入可执行体。
# -----------------------------------------------------------------------------
set(VIEWPORT3D_SOURCES
    viewport3d/VsgQtWidget.cpp
    viewport3d/ViewportKit.cpp
    viewport3d/ViewportContext.cpp
    viewport3d/FeatureRegistry.cpp
    viewport3d/ViewportConfig.cpp
    viewport3d/Viewport3DView.cpp
    viewport3d/OcclusionQueryService.cpp
    viewport3d/CommonFeatures.cpp
    viewport3d/TerrainLayerFeature.cpp
    viewport3d/ReferenceGridFeature.cpp
    viewport3d/BrushEditFeature.cpp
    viewport3d/AnalysisOverlayFeature.cpp
    viewport3d/TerrainEditFacade.cpp
    viewport3d/TrajectoryFeature.cpp
    viewport3d/SensorFovFeature.cpp
    viewport3d/DetectionEffectFeature.cpp
    viewport3d/ScenarioPreviewFeature.cpp
    viewport3d/VoxelSceneFeature.cpp
    viewport3d/TerrainDisplayPages.cpp
    viewport3d/AircraftCockpitFeature.cpp
    viewport3d/CockpitInterior.cpp
    viewport3d/CockpitSpatial.cpp
    viewport3d/CockpitPhotoPanel.cpp
    viewport3d/AircraftExterior.cpp
    viewport3d/PlaybackFeature.cpp
    viewport3d/SceneDesignFacade.cpp
    viewport3d/SimulationDisplayFacade.cpp
    viewport3d/RelationshipVisualFeature.cpp
    viewport3d/EngagementVisualFeature.cpp
    viewport3d/AviationParticles.cpp
    viewport3d/PlatformMotionFeature.cpp
    viewport3d/PlatformAnimationEffects.cpp
    viewport3d/SceneEffectsAdapter.cpp
    viewport3d/SceneAudioFeature.cpp
    audio/SceneAudioMixer.cpp
    audio/SceneAudioOutput.cpp
    viewport3d/TrailFeature.cpp
    viewport3d/ImGuiToolbarFeature.cpp
)

# Rocky geo-globe + 环境特性 (rocky 为硬依赖, 恒编译)。
list(APPEND VIEWPORT3D_SOURCES
    viewport3d/EarthViewFeature.cpp
    viewport3d/RockyGlobeBackend.cpp
    viewport3d/GeoTerrainExporter.cpp
    viewport3d/EnvFeatures.cpp
    viewport3d/EnvLabWindow.cpp)   # 环境模块验证台主窗口 (依赖 rocky_env)

add_library(vsgraysim_platform_animation STATIC
    platform_animation/PlatformAnimation.cpp
    platform_animation/M1A2Animation.cpp
    platform_animation/F16CAnimation.cpp
    platform_animation/CG57Animation.cpp
    platform_animation/CockpitModel.cpp)
target_include_directories(vsgraysim_platform_animation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/include")

add_library(vsgraysim_viewport3d OBJECT ${VIEWPORT3D_SOURCES})
set_target_properties(vsgraysim_viewport3d PROPERTIES AUTOMOC ON)
target_include_directories(vsgraysim_viewport3d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # qpa/qplatformnativeinterface.h lives in the versioned private include dir.
    "${QT6_PREFIX}/include/QtGui/6.12.0/QtGui"
)
target_link_libraries(vsgraysim_viewport3d PUBLIC
    vsgraysim_platform_animation
    vsgraysim_core
    vsgraysim_common
    Qt6::Widgets
)
if(TARGET Qt6::Multimedia)
    target_link_libraries(vsgraysim_viewport3d PUBLIC Qt6::Multimedia)
    target_compile_definitions(vsgraysim_viewport3d PUBLIC VSG_HAS_SCENE_AUDIO=1)
endif()
target_include_directories(vsgraysim_viewport3d PRIVATE
    "${ROCKY_EARTH3P_DIR}/include")
target_link_libraries(vsgraysim_viewport3d PUBLIC
    vsgraysim_voxel_earth vsgraysim_rocky_env
    ${VSGRAYSIM_ROCKY_TARGET}
    GDAL::GDAL)

# -----------------------------------------------------------------------------
# vsgraysim_gui_lib: 所有窗口代码的静态库
# 各进程可执行体 (app/ 中定义) 链接此库 + 各自需要的 viewport3d 特性
# -----------------------------------------------------------------------------
add_library(vsgraysim_gui_lib STATIC ${GUI_SOURCES})
set_target_properties(vsgraysim_gui_lib PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)
target_link_libraries(vsgraysim_gui_lib PUBLIC
    vsgraysim_viewport3d
    vsgraysim_algo
    vsgraysim_core
    vsgraysim_common
    vsgraysim_ipc
    vsgraysim_telemetry
    vsgraysim_platform
    Qt6::Widgets
)
target_include_directories(vsgraysim_gui_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)
target_include_directories(vsgraysim_gui_lib PRIVATE
    "${QT6_PREFIX}/include/QtGui/6.12.0/QtGui"
)

if(BUILD_TESTING)
    add_executable(sim_scene_audio_tests "${CMAKE_SOURCE_DIR}/tests/scene_audio_tests.cpp")
    target_link_libraries(sim_scene_audio_tests PRIVATE vsgraysim_gui_lib)
    set_target_properties(sim_scene_audio_tests PROPERTIES BUILD_RPATH "${VSGRAYSIM_GUI_RPATH_DIRS}")
    target_link_options(sim_scene_audio_tests PRIVATE "-Wl,--disable-new-dtags")
    add_test(NAME scene_audio_contracts COMMAND sim_scene_audio_tests)
    set_tests_properties(scene_audio_contracts PROPERTIES TIMEOUT 45 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(sim_platform_animation_tests ../tests/platform_animation_tests.cpp)
    target_link_libraries(sim_platform_animation_tests PRIVATE vsgraysim_platform_animation)
    add_test(NAME platform_animation_models COMMAND sim_platform_animation_tests)
    target_sources(vsgraysim_gui_lib PRIVATE testing/FormalChainValidation.cpp)
    target_compile_definitions(vsgraysim_gui_lib PUBLIC VSGRAYSIM_FORMAL_VALIDATION=1)
    foreach(test_spec "sim_display_sessions;display_session_tests.cpp;display_sensor_sessions" "sim_workspace_tests;workspace_tests.cpp;display_workspace_profiles" "sim_flight_ui_tests;flight_ui_tests.cpp;display_flight_controls" "sim_aircraft_cockpit_tests;aircraft_cockpit_tests.cpp;display_aircraft_cockpit" "sim_flight_scene_tests;flight_scene_tests.cpp;designer_flight_configuration" "sim_earth_gui_tests;earth_gui_tests.cpp;earth_gui_handoff" "sim_earth_display_tests;earth_display_tests.cpp;display_earth_context")
        list(GET test_spec 0 test_target)
        list(GET test_spec 1 test_source)
        list(GET test_spec 2 test_name)
        add_executable(${test_target} "${CMAKE_SOURCE_DIR}/tests/${test_source}")
        target_link_libraries(${test_target} PRIVATE vsgraysim_gui_lib)
        set_target_properties(${test_target} PROPERTIES BUILD_RPATH "${VSGRAYSIM_GUI_RPATH_DIRS}")
        target_link_options(${test_target} PRIVATE "-Wl,--disable-new-dtags")
        add_test(NAME ${test_name} COMMAND ${test_target})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_dependencies(sim_earth_gui_tests voxel_scene)
    target_sources(sim_earth_gui_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/earth_unified_capture.cpp" "${CMAKE_SOURCE_DIR}/tests/earth_detail_capture.cpp" "${CMAKE_SOURCE_DIR}/tests/earth_ocean_capture.cpp")
    target_sources(sim_earth_gui_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/earth_aviation_capture.cpp" "${CMAKE_SOURCE_DIR}/tests/earth_aircraft_debris_capture.cpp")
    target_sources(sim_earth_gui_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/earth_ah64_rotor_capture.cpp")
    target_sources(sim_earth_gui_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/earth_platform_motion_capture.cpp")
    target_sources(sim_aircraft_cockpit_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/cockpit_exterior_capture.cpp")
endif()
