if(NOT TARGET assimp)
    foreach(_option ASSIMP_BUILD_TESTS ASSIMP_BUILD_ASSIMP_TOOLS ASSIMP_WARNINGS_AS_ERRORS
            ASSIMP_BUILD_ZLIB ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT ASSIMP_BUILD_DRACO
            ASSIMP_BUILD_USD_IMPORTER ASSIMP_BUILD_SAMPLES)
        set(${_option} OFF CACHE BOOL "Product Assimp configuration" FORCE)
    endforeach()
    set(ASSIMP_INSTALL ON CACHE BOOL "Export the local Assimp CMake target" FORCE)
    set(ASSIMP_NO_EXPORT ON CACHE BOOL "Use Assimp for model import" FORCE)
    set(ASSIMP_IGNORE_GIT_HASH ON CACHE BOOL "Build the locked source snapshot" FORCE)
    foreach(_format OBJ GLTF FBX COLLADA PLY STL 3DS)
        set(ASSIMP_BUILD_${_format}_IMPORTER ON CACHE BOOL "Supported model format" FORCE)
    endforeach()
    function(_vsgraysim_add_assimp)
        # The upstream export branch treats a pre-set ZLIB_FOUND as evidence
        # that a parent built zlib/zlibstatic targets. Run its normal discovery.
        set(ZLIB_FOUND FALSE)
        add_subdirectory("${VSGRAYSIM_DEPENDENCY_SOURCE}/assimp"
            "${CMAKE_BINARY_DIR}/third_party/assimp" EXCLUDE_FROM_ALL)
    endfunction()
    _vsgraysim_add_assimp()
    export(TARGETS assimp NAMESPACE assimp:: FILE "${CMAKE_BINARY_DIR}/third_party/assimp/assimpTargets.cmake")
endif()
