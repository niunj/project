add_library(vsgraysim_engine_worker STATIC SimulationEngineWorker.cpp ServiceProducts.cpp BrowserEarthSurface.cpp)
target_include_directories(vsgraysim_engine_worker PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vsgraysim_engine_worker PUBLIC vsgraysim_core PRIVATE Qt6::Core Qt6::Gui)
target_link_libraries(vsgraysim_engine_worker PRIVATE vsgraysim_deployment_adapter)

if(VSGRAYSIM_BUILD_SERVICE)
    find_package(Qt6 6.12 REQUIRED COMPONENTS HttpServer)
    add_executable(vsgraysim_sim_service main.cpp SimulationService.cpp WebWorkspace.cpp ModelLibrary.cpp)
    target_link_libraries(vsgraysim_sim_service PRIVATE vsgraysim_engine_worker Qt6::HttpServer Qt6::Network scene_deployment)
    target_compile_features(vsgraysim_sim_service PRIVATE cxx_std_20)
    target_compile_definitions(vsgraysim_sim_service PRIVATE QT_NO_KEYWORDS)
    set_target_properties(vsgraysim_sim_service PROPERTIES
        BUILD_RPATH "${VSGRAYSIM_GUI_RPATH_DIRS}"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib;${VSGRAYSIM_GUI_RPATH_DIRS}")
    target_link_options(vsgraysim_sim_service PRIVATE "-Wl,--disable-new-dtags")
    add_dependencies(vsgraysim_sim_service vsgraysim_assets voxel_scene)
endif()
