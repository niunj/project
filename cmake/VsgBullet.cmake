# Build the two used vsgBullet libraries from the locked source snapshot.
# The upstream legacy build configures headers in its source tree and discovers
# optional applications. This adapter keeps sources immutable and links exactly
# the same VSG target as the products.
set(_vsgbullet_source "${CMAKE_SOURCE_DIR}/third_party/vsgBullet")
set(_vsgbullet_build "${CMAKE_BINARY_DIR}/third_party/vsgBullet")
set(VSGBULLET_VERSION 3.00.00)
set(VSGBCOLLISION_MAJOR_VERSION 3)
set(VSGBCOLLISION_MINOR_VERSION 0)
set(VSGBCOLLISION_SUB_VERSION 0)
file(MAKE_DIRECTORY "${_vsgbullet_build}/include/vsgbCollision")
configure_file("${_vsgbullet_source}/include/vsgbCollision/Version.h.in"
    "${_vsgbullet_build}/include/vsgbCollision/Version.h" @ONLY)
set(_collision_names BoundingCone BoundingCylinder Chart CollectVerticesVisitor CollisionShapes ComputeCylinderVisitor ComputeShapeVisitor ComputeTriMeshVisitor Utils VertexAggOp Version GeometryOperation ReducerOp GeometryModifier)
set(_dynamics_names Constraints CreationRecord GroundPlane MotionState PhysicsState PhysicsThread RigidBody SoftBody Joint RigidBodyAnimation TripleBuffer World)
foreach(_component Collision Dynamics)
    string(TOLOWER "${_component}" _lower)
    set(_sources)
    foreach(_name IN LISTS _${_lower}_names)
        list(APPEND _sources "${_vsgbullet_source}/src/vsgb${_component}/${_name}.cpp")
    endforeach()
    add_library(vsgb${_component} ${_sources})
    target_include_directories(vsgb${_component} PUBLIC
        "${_vsgbullet_build}/include" "${_vsgbullet_source}/include" ${BULLET_INCLUDE_DIRS})
    if(BUILD_SHARED_LIBS)
        target_compile_definitions(vsgb${_component} PUBLIC VSGBULLET_SHARED)
    else()
        target_compile_definitions(vsgb${_component} PUBLIC VSGBULLET_STATIC)
    endif()
    target_link_libraries(vsgb${_component} PUBLIC vsg::vsg ${BULLET_LIBRARIES})
    # Bullet's SDK libraries rely on sibling libraries (e.g. Bullet3Common).
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(vsgb${_component} PRIVATE "-Wl,--disable-new-dtags")
    endif()
    set_target_properties(vsgb${_component} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${_vsgbullet_build}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${_vsgbullet_build}/lib"
        POSITION_INDEPENDENT_CODE ON VERSION "${VSGBULLET_VERSION}" SOVERSION "${VSGBULLET_VERSION}")
endforeach()
target_compile_definitions(vsgbCollision PRIVATE VSGBCOLLISION_LIBRARY)
target_compile_definitions(vsgbDynamics PRIVATE VSGBDYNAMICS_LIBRARY)
target_link_libraries(vsgbDynamics PUBLIC vsgbCollision)
set(VSGRAYSIM_BULLET_EXTRA_LIBRARIES)
foreach(_name BulletWorldImporter ConvexDecomposition BulletFileLoader Bullet2FileLoader)
    find_library(VSGRAYSIM_${_name}_LIBRARY NAMES "${_name}" HINTS "${BULLET_ROOT}/lib" REQUIRED)
    list(APPEND VSGRAYSIM_BULLET_EXTRA_LIBRARIES "${VSGRAYSIM_${_name}_LIBRARY}")
endforeach()
target_link_libraries(vsgbDynamics PRIVATE ${VSGRAYSIM_BULLET_EXTRA_LIBRARIES})
