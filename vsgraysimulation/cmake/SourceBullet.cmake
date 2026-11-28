# Build Bullet in the same target graph as the bridges and collision runtime.
if(NOT TARGET BulletDynamics)
    foreach(_option BUILD_BULLET2_DEMOS BUILD_CPU_DEMOS BUILD_UNIT_TESTS
            BUILD_BULLET3 BUILD_PYBULLET BUILD_BULLET_ROBOTICS_GUI_EXTRA
            BUILD_BULLET_ROBOTICS_EXTRA BUILD_INVERSE_DYNAMIC_EXTRA
            BUILD_OBJ2SDF_EXTRA BUILD_HACD_EXTRA BUILD_GIMPACTUTILS_EXTRA
            USE_GRAPHICAL_BENCHMARK USE_DOUBLE_PRECISION)
        set(${_option} OFF CACHE BOOL "Product Bullet configuration" FORCE)
    endforeach()
    set(BUILD_EXTRAS ON CACHE BOOL "Build Bullet serialization and convex decomposition" FORCE)
    add_subdirectory("${VSGRAYSIM_DEPENDENCY_SOURCE}/bullet"
        "${CMAKE_BINARY_DIR}/third_party/bullet" EXCLUDE_FROM_ALL)
endif()

# Upstream only declares these dependencies for shared builds. Static
# consumers also need them so CMake puts each provider after its users.
get_target_property(_bullet_type BulletSoftBody TYPE)
if(_bullet_type STREQUAL "STATIC_LIBRARY")
    target_link_libraries(BulletSoftBody INTERFACE BulletDynamics BulletCollision LinearMath)
    if(TARGET BulletWorldImporter)
        target_link_libraries(BulletWorldImporter INTERFACE BulletDynamics BulletCollision BulletFileLoader LinearMath)
    endif()
endif()

set(BULLET_INCLUDE_DIRS "${VSGRAYSIM_DEPENDENCY_SOURCE}/bullet/src"
    "${VSGRAYSIM_DEPENDENCY_SOURCE}/bullet/Extras" "${VSGRAYSIM_DEPENDENCY_SOURCE}/bullet/Extras/Serialize")
set(BULLET_LIBRARIES BulletSoftBody BulletDynamics BulletCollision LinearMath)
set(VSGRAYSIM_BULLET_EXTRA_LIBRARIES
    BulletWorldImporter ConvexDecomposition BulletFileLoader)
