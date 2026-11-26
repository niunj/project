include(ExternalProject)

# OpenUSD has its own project settings and generated schema resources. Keep its
# CMake cache and install private to this build's third_party tree.
set(VSGRAYSIM_OPENUSD_CMAKE "${CMAKE_COMMAND}" CACHE FILEPATH "CMake 3.26 or newer for OpenUSD")
set(VSGRAYSIM_OPENUSD_JOBS 2 CACHE STRING "Parallel OpenUSD compiler jobs")
execute_process(COMMAND "${VSGRAYSIM_OPENUSD_CMAKE}" --version
    OUTPUT_VARIABLE _usd_cmake_version RESULT_VARIABLE _usd_cmake_status)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _usd_cmake_version "${_usd_cmake_version}")
if(NOT _usd_cmake_status EQUAL 0 OR _usd_cmake_version VERSION_LESS 3.26)
    message(FATAL_ERROR "OpenUSD requires CMake 3.26 or newer. Set VSGRAYSIM_OPENUSD_CMAKE or use a newer CMake for the whole build.")
endif()
find_program(_usd_ninja ninja REQUIRED)

set(USD_ROOT "${CMAKE_BINARY_DIR}/third_party/install/openusd")
set(_usd_build "${CMAKE_BINARY_DIR}/third_party/openusd")
set(_usd_component_names usd usdGeom usdShade sdf tf gf vt)
file(MAKE_DIRECTORY "${USD_ROOT}/include")
set(_usd_byproducts)
foreach(_component IN LISTS _usd_component_names)
    list(APPEND _usd_byproducts "${USD_ROOT}/lib/libusd_${_component}.so")
endforeach()
ExternalProject_Add(vsgraysim_openusd
    SOURCE_DIR "${VSGRAYSIM_DEPENDENCY_SOURCE}/openusd"
    BINARY_DIR "${_usd_build}"
    PREFIX "${CMAKE_BINARY_DIR}/third_party/stamps/openusd"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_COMMAND "${VSGRAYSIM_OPENUSD_CMAKE}"
    CMAKE_GENERATOR Ninja
    LIST_SEPARATOR "|"
    CMAKE_ARGS
        "-DCMAKE_MAKE_PROGRAM=${_usd_ninja}"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG"
        "-DCMAKE_INSTALL_PREFIX=${USD_ROOT}"
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
        "-DCMAKE_INSTALL_RPATH=\$ORIGIN|\$ORIGIN/../../../base/oneTBB/lib/intel64/gcc4.8"
        "-DTBB_DIR=${VSGRAYSIM_BASE_ROOT}/oneTBB/lib/cmake/tbb"
        -DPXR_FIND_TBB_IN_CONFIG=ON
        -DPXR_ENABLE_PYTHON_SUPPORT=OFF
        -DPXR_BUILD_IMAGING=OFF -DPXR_BUILD_USD_IMAGING=OFF -DPXR_BUILD_USDVIEW=OFF
        -DPXR_BUILD_TESTS=OFF -DPXR_BUILD_EXAMPLES=OFF -DPXR_BUILD_TUTORIALS=OFF
        -DPXR_BUILD_USD_TOOLS=OFF -DPXR_BUILD_EXEC=OFF -DPXR_BUILD_USD_VALIDATION=OFF
    BUILD_COMMAND "${VSGRAYSIM_OPENUSD_CMAKE}" --build <BINARY_DIR> --parallel "${VSGRAYSIM_OPENUSD_JOBS}"
    INSTALL_COMMAND "${VSGRAYSIM_OPENUSD_CMAKE}" --install <BINARY_DIR>
    BUILD_BYPRODUCTS ${_usd_byproducts}
    BUILD_ALWAYS TRUE
    LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_INSTALL TRUE LOG_OUTPUT_ON_FAILURE TRUE)
add_dependencies(vsgraysim_openusd vsgraysim_verify_dependencies)
foreach(_component IN LISTS _usd_component_names)
    add_library(OpenUSD::${_component} SHARED IMPORTED GLOBAL)
    set_target_properties(OpenUSD::${_component} PROPERTIES
        IMPORTED_LOCATION "${USD_ROOT}/lib/libusd_${_component}.so"
        INTERFACE_INCLUDE_DIRECTORIES "${USD_ROOT}/include;${VSGRAYSIM_BASE_ROOT}/oneTBB/include")
    add_dependencies(OpenUSD::${_component} vsgraysim_openusd)
    list(APPEND VSGRAYSIM_PXR_LIBS OpenUSD::${_component})
endforeach()
