cmake_minimum_required(VERSION 3.20)
get_filename_component(earth_repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(VoxelEarth LANGUAGES CXX)
    set(earth_standalone TRUE)
endif()
if(NOT VSGRAYSIM_EARTH_PRODUCT_ADAPTERS)
    set(VSGRAYSIM_WITH_ROCKY OFF)
endif()
if(NOT TARGET vsg::vsg)
    set(VSG_SUPPORTS_ShaderCompiler OFF CACHE BOOL "Use build-time Vulkan shaders")
    set(VSG_SUPPORTS_Windowing OFF CACHE BOOL "Host supplies its own surface")
    add_subdirectory("${earth_repo}/third_party/vsg" vsg EXCLUDE_FROM_ALL)
endif()
if(earth_standalone)
    set(VSGRAYSIM_DEPENDENCY_SOURCE "${earth_repo}/third_party")
    include("${earth_repo}/cmake/ThirdPartyBase.cmake")
    vsgraysim_prepare_base()
    set(ZLIB_INCLUDE_DIR "${VSGRAYSIM_BASE_ROOT}/include" CACHE PATH "Bundled zlib headers" FORCE)
    set(ZLIB_LIBRARY_RELEASE "${VSGRAYSIM_BASE_ROOT}/lib/x86_64-linux-gnu/libz.so" CACHE FILEPATH "Bundled zlib" FORCE)
    set(ZLIB_LIBRARY "${ZLIB_LIBRARY_RELEASE}" CACHE FILEPATH "Bundled zlib linkage" FORCE)
    set(ZLIB_LIBRARY_REL "${ZLIB_LIBRARY_RELEASE}" CACHE FILEPATH "Bundled zlib (FindZLIB release)" FORCE)
    set(ZLIB_LIBRARY_DBG "${ZLIB_LIBRARY_RELEASE}" CACHE FILEPATH "Bundled zlib (FindZLIB debug)" FORCE)
endif()
find_package(ZLIB REQUIRED)
if(NOT BULLET_LIBRARIES)
    set(VSGRAYSIM_DEPENDENCY_SOURCE "${earth_repo}/third_party")
    include("${earth_repo}/cmake/SourceBullet.cmake")
    include("${earth_repo}/cmake/SourceAssimp.cmake")
endif()
if(NOT TARGET vsgraysim_objloader)
    add_subdirectory("${earth_repo}/lib/objloader" objloader)
endif()
if(NOT TARGET vsgraysim_simcore)
    add_subdirectory("${earth_repo}/lib/simcore" simcore)
endif()

# Compile only the globe and visual environment. Every input is local and each
# changed GLSL file regenerates its own embedded SPIR-V, including FFT compute.
find_program(earth_glslang glslangValidator HINTS "${VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)
    set(earth_shaders
    viewport/voxel_globe.vert viewport/voxel_globe.frag
    viewport/surface_cluster_select.comp
    viewport/terrain_cloud_shadow.vert viewport/terrain_cloud_shadow.frag
    viewport/voxel_ocean.vert viewport/voxel_ocean.frag viewport/voxel_ocean_foam.vert viewport/voxel_ocean_foam.frag
    viewport/planet_weather.vert viewport/planet_sea.frag viewport/planet_cloud.frag
    viewport/precipitation.vert viewport/precipitation.frag
    viewport/cloud_light_field.comp
    viewport/planet_cloud_cached.frag viewport/planet_cloud_raw_cached.frag viewport/planet_cloud_reflection_cached.frag
    viewport/planet_cloud_raw.vert viewport/planet_cloud_raw.frag viewport/planet_cloud_reflection.vert viewport/planet_cloud_reflection.frag viewport/planet_sky_reflection.frag viewport/cloud_light_cache.comp viewport/cloud_temporal_resolve.comp viewport/cloud_temporal_composite.frag viewport/cloud_fog_sky.frag
    env/sky_dome.vert env/sky_dome.frag env/sky_dome_lut.frag env/moon_disk.frag
    env/cloud_sheet.vert env/cloud_sheet.frag env/cloud_volume.vert env/cloud_volume.frag
    env/cloud_sprite.vert env/cloud_sprite.frag env/ocean.vert env/ocean.frag
    env/ocean_fft.vert env/ocean_fft_spectrum.comp env/ocean_ifft.comp env/ocean_fft_compose.comp
    env/cloud_voxel_seed.comp env/cloud_voxel_advect.comp env/cloud_voxel_advect_reverse.comp
    env/cloud_voxel_advect_correct.comp env/cloud_voxel_microphysics.comp
    env/cloud_voxel_divergence.comp env/cloud_voxel_pressure.comp env/cloud_voxel_project.comp
    env/cloud_voxel_copy.comp env/cloud_voxel_rain.comp
    env/sky_scatter_lut.comp)
set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${shader_dir}")
set(shader_includes "")
set(shader_cases "")
foreach(shader IN LISTS earth_shaders)
    get_filename_component(name "${shader}" NAME)
    string(MAKE_C_IDENTIFIER "${name}" id)
    get_filename_component(ext "${name}" LAST_EXT)
    if(ext STREQUAL ".vert")
        set(stage VERTEX)
    elseif(ext STREQUAL ".frag")
        set(stage FRAGMENT)
    else()
        set(stage COMPUTE)
    endif()
    set(source "${earth_repo}/shaders/${shader}")
    set(header "${shader_dir}/${id}.h")
    set(extra_input "")
    if(name MATCHES "^voxel_ocean")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/voxel_ocean_field.glsl" "${earth_repo}/shaders/earth_atmosphere_optics.glsl")
    endif()
    if(name MATCHES "^planet_(sea|cloud)")
        set(extra_input "${earth_repo}/shaders/viewport/planet_shell.glsl")
    endif()
    if(name MATCHES "^(sky_dome.*frag|moon_disk.frag)$")
        list(APPEND extra_input "${earth_repo}/shaders/env/celestial_discs.glsl")
    endif()
    if(name MATCHES "^sky_dome.*frag$")
        list(APPEND extra_input "${earth_repo}/shaders/env/celestial_sky.glsl" "${earth_repo}/shaders/env/sky_weather.glsl")
    endif()
    if(name STREQUAL "planet_sky_reflection.frag")
        list(APPEND extra_input "${earth_repo}/shaders/env/sky_weather.glsl")
    endif()
    if(name MATCHES "^(sky_dome.*|planet_cloud|terrain_cloud_shadow)\\.frag$")
        list(APPEND extra_input "${earth_repo}/shaders/earth_atmosphere_optics.glsl")
    endif()
    if(name MATCHES "^(planet_(sea|cloud)|voxel_globe).*frag$")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/planet_light.glsl" "${earth_repo}/shaders/viewport/planet_cloud_shadow.glsl")
    endif()
    if(name STREQUAL "terrain_cloud_shadow.frag")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/terrain_weather.glsl")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/planet_cloud_shadow.glsl" "${earth_repo}/shaders/viewport/planet_light.glsl")
    endif()
    if(name MATCHES "^(voxel_ocean|terrain_cloud_shadow).*frag$")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/marine_fog.glsl")
    endif()
    if(name STREQUAL "voxel_ocean.frag")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/marine_specular.glsl" "${earth_repo}/shaders/viewport/rain_water.glsl")
    endif()
    if(name STREQUAL "cloud_light_cache.comp" OR name STREQUAL "cloud_light_field.comp" OR name MATCHES "^planet_cloud.*frag$")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/cloud_light_cache.glsl" "${earth_repo}/shaders/viewport/planet_cloud_shadow.glsl")
    endif()
    if(name STREQUAL "cloud_light_field.comp" OR name MATCHES "^planet_cloud.*frag$")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/cloud_light_field.glsl")
    endif()
    if(name MATCHES "^(planet_cloud|planet_sky_reflection|cloud_temporal|cloud_fog_sky|planet_sea)")
        list(APPEND extra_input "${earth_repo}/shaders/viewport/weather_reflection.glsl" "${earth_repo}/shaders/viewport/planet_cloud_common.glsl" "${earth_repo}/shaders/viewport/cloud_temporal.glsl" "${earth_repo}/shaders/viewport/marine_fog.glsl" "${earth_repo}/shaders/viewport/planet_shell.glsl" "${earth_repo}/shaders/viewport/planet_light.glsl" "${earth_repo}/shaders/viewport/planet_cloud_shadow.glsl" "${earth_repo}/shaders/earth_atmosphere_optics.glsl")
    endif()
    add_custom_command(OUTPUT "${header}"
        COMMAND "${earth_glslang}" -V --target-env vulkan1.1 --vn "${id}" -o "${header}" "${source}"
        DEPENDS "${source}" ${extra_input} VERBATIM)
    list(APPEND shader_headers "${header}")
    string(APPEND shader_includes "#include \"${id}.h\"\n")
    string(APPEND shader_cases "    if (name == \"${name}\") return makeStage(VK_SHADER_STAGE_${stage}_BIT, ${id});\n")
endforeach()
configure_file(src/EarthShader.cpp.in "${shader_dir}/EarthShader.cpp" @ONLY)

set(env_src "${earth_repo}/env_modules/src")
add_library(vsgraysim_earth_shaders STATIC "${shader_dir}/EarthShader.cpp" ${shader_headers})
target_include_directories(vsgraysim_earth_shaders PRIVATE src)
target_link_libraries(vsgraysim_earth_shaders PUBLIC vsg::vsg)
target_compile_features(vsgraysim_earth_shaders PUBLIC cxx_std_17)

add_library(vsgraysim_earth_environment STATIC
    ${env_src}/EnvCommon.cpp ${env_src}/MoonTexture.cpp ${env_src}/StarTexture.cpp ${env_src}/EphemerisBridge.cpp ${env_src}/AtmospherePhysics.cpp
    ${env_src}/AtmosphereLUT.cpp ${env_src}/SkyModule.cpp ${env_src}/EnvStack.cpp
    ${env_src}/WaveField.cpp ${env_src}/OceanFFTField.cpp ${env_src}/CloudModule.cpp
    ${env_src}/VoxelWeather.cpp ${env_src}/CloudTemporal.cpp ${env_src}/WeatherReflection.cpp ${env_src}/CloudLightCache.cpp ${env_src}/CloudLightField.cpp ${env_src}/SparseCloudVolume.cpp ${env_src}/VoxelCloudCluster.cpp
    ${env_src}/GpuVoxelCloud.cpp ${env_src}/Precipitation.cpp
    ${env_src}/OceanModule.cpp ${env_src}/sensor/SensorBand.cpp)
target_include_directories(vsgraysim_earth_environment PUBLIC "${earth_repo}/env_modules/include"
    PRIVATE "${env_src}" src)
target_link_libraries(vsgraysim_earth_environment PUBLIC vsg::vsg vsgraysim_simcore PRIVATE vsgraysim_earth_shaders)
target_compile_definitions(vsgraysim_earth_environment PRIVATE VSGRAYSIM_WITH_ROCKY=$<BOOL:${VSGRAYSIM_WITH_ROCKY}>)
if(VSGRAYSIM_WITH_ROCKY)
    target_link_libraries(vsgraysim_earth_environment PRIVATE ${VSGRAYSIM_ROCKY_TARGET})
endif()

add_library(vsgraysim_earth_globe STATIC src/OrbitalGlobe.cpp)
target_include_directories(vsgraysim_earth_globe PUBLIC "${earth_repo}/include")
target_link_libraries(vsgraysim_earth_globe PUBLIC vsg::vsg vsgraysim_earth_environment
    PRIVATE vsgraysim_simcore vsgraysim_earth_shaders)
add_library(vsgraysim_voxel_earth STATIC
    src/EnvironmentTerrain.cpp src/TerrainCloudShadow.cpp src/VoxelOceanDisplay.cpp src/SurfaceClusters.cpp
    ${earth_repo}/src/earth/EarthDataService.cpp
    ${earth_repo}/src/earth/TiledVoxelTerrain.cpp ${earth_repo}/src/earth/VoxelTerrain.cpp
    ${earth_repo}/src/earth/VoxelTerrainPhysics.cpp
    ${earth_repo}/src/earth/TiledVoxelTerrainPhysics.cpp
    ${earth_repo}/src/earth/VoxelModelVoxelizer.cpp ${earth_repo}/src/earth/AssimpVoxelImport.cpp
    ${earth_repo}/src/earth/VoxelVolumeCodec.cpp ${earth_repo}/src/earth/VoxelScene.cpp
    ${earth_repo}/src/earth/VoxelScenePackage.cpp ${earth_repo}/src/earth/SceneWeather.cpp
    ${earth_repo}/src/earth/VoxelSculpt.cpp
    ${earth_repo}/src/earth/VoxelFracture.cpp ${earth_repo}/src/earth/VoxelDynamics.cpp
    ${earth_repo}/src/earth/VoxelSceneSensors.cpp ${earth_repo}/src/earth/VoxelSensorMaterials.cpp
    ${earth_repo}/src/earth/VoxelEarthPatch.cpp ${earth_repo}/src/earth/VoxelOcean.cpp ${earth_repo}/src/earth/VoxelBuoyancy.cpp ${earth_repo}/src/earth/VoxelOceanOptics.cpp
    ${earth_repo}/src/earth/SurfaceMesh.cpp)

add_library(vsgraysim_meshoptimizer STATIC
    "${earth_repo}/third_party/support/meshoptimizer/allocator.cpp"
    "${earth_repo}/third_party/support/meshoptimizer/simplifier.cpp"
    "${earth_repo}/third_party/support/meshoptimizer/spatialorder.cpp")
target_include_directories(vsgraysim_meshoptimizer PUBLIC "${earth_repo}/third_party/support/meshoptimizer")
set_target_properties(vsgraysim_meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vsgraysim_voxel_earth PUBLIC "${earth_repo}/include" "${earth_repo}/third_party/support/json/include")
target_link_libraries(vsgraysim_voxel_earth PUBLIC vsgraysim_earth_globe vsgraysim_earth_environment
    PRIVATE assimp::assimp vsgraysim_objloader vsgraysim_meshoptimizer vsgraysim_earth_shaders ${BULLET_LIBRARIES} ZLIB::ZLIB)
target_include_directories(vsgraysim_voxel_earth PRIVATE ${BULLET_INCLUDE_DIRS} "${env_src}" "${earth_repo}/third_party/vsgXchange/src/stbi")
if(NOT VSGRAYSIM_EARTH_PRODUCT_ADAPTERS)
    target_compile_definitions(vsgraysim_voxel_earth PRIVATE VSGRAYSIM_EARTH_RASTER=0)
    if(earth_standalone)
        include(CTest)
    endif()
    if(earth_standalone AND BUILD_TESTING)
        add_executable(voxel_earth_scene_contracts "${earth_repo}/tests/voxel_earth_scene_tests.cpp")
        target_compile_definitions(voxel_earth_scene_contracts PRIVATE VOXEL_EARTH_TEST_RASTER=0)
        target_link_libraries(voxel_earth_scene_contracts PRIVATE vsgraysim_voxel_earth)
        add_test(NAME voxel_earth_unified_scene COMMAND voxel_earth_scene_contracts)
        add_executable(voxel_scene "${earth_repo}/tools/voxel_scene.cpp")
        target_link_libraries(voxel_scene PRIVATE vsgraysim_voxel_earth)
        add_executable(voxel_asset_import_contracts "${earth_repo}/tests/voxel_asset_import_tests.cpp")
        target_link_libraries(voxel_asset_import_contracts PRIVATE vsgraysim_voxel_earth)
        add_test(NAME voxel_vsg_asset_contracts COMMAND voxel_asset_import_contracts)
        add_executable(assimp_voxel_import_contracts "${earth_repo}/tests/assimp_voxel_import_tests.cpp")
        target_link_libraries(assimp_voxel_import_contracts PRIVATE vsgraysim_voxel_earth)
        add_test(NAME voxel_model_format_import COMMAND assimp_voxel_import_contracts)
        add_executable(voxel_fracture_contracts "${earth_repo}/tests/voxel_fracture_tests.cpp")
        target_link_libraries(voxel_fracture_contracts PRIVATE vsgraysim_voxel_earth)
        add_test(NAME voxel_structural_fracture COMMAND voxel_fracture_contracts)
        add_executable(voxel_scene_contracts "${earth_repo}/tests/voxel_scene_tests.cpp")
        target_link_libraries(voxel_scene_contracts PRIVATE vsgraysim_voxel_earth)
        add_test(NAME voxel_scene_runtime COMMAND voxel_scene_contracts)
        add_test(NAME voxel_scene_document COMMAND voxel_scene validate "${earth_repo}/config/voxel/workshop.vscene.json")
        add_executable(voxel_earth_smoke tests/native_scene.cpp)
        target_link_libraries(voxel_earth_smoke PRIVATE vsgraysim_voxel_earth)
        add_test(NAME voxel_earth_native_scene COMMAND voxel_earth_smoke "${earth_repo}/config/earth/native-volume.scene.json")
    endif()
else()
    target_compile_definitions(vsgraysim_voxel_earth PRIVATE VSGRAYSIM_EARTH_RASTER=1)
    target_include_directories(vsgraysim_voxel_earth PRIVATE "${SIM_GDAL_INCLUDE}")
    target_link_libraries(vsgraysim_voxel_earth PRIVATE "${SIM_GDAL_LIBRARY}")
endif()
target_link_libraries(vsgraysim_voxel_earth PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(vsgraysim_earth_shaders vsgraysim_earth_globe vsgraysim_voxel_earth vsgraysim_earth_environment PROPERTIES POSITION_INDEPENDENT_CODE ON)
