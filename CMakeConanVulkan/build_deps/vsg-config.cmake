########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(vsg_FIND_QUIETLY)
    set(vsg_MESSAGE_MODE VERBOSE)
else()
    set(vsg_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/vsgTargets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${vsg_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(vsg_VERSION_STRING "1.0.9")
set(vsg_INCLUDE_DIRS ${vsg_INCLUDE_DIRS_RELEASE} )
set(vsg_INCLUDE_DIR ${vsg_INCLUDE_DIRS_RELEASE} )
set(vsg_LIBRARIES ${vsg_LIBRARIES_RELEASE} )
set(vsg_DEFINITIONS ${vsg_DEFINITIONS_RELEASE} )

# 添加Vulkan SDK路径到包含目录
if(DEFINED ENV{VULKAN_SDK})
    list(APPEND vsg_INCLUDE_DIRS "$ENV{VULKAN_SDK}/include")
endif()

# 如果找不到Vulkan SDK，尝试常见的安装路径
if(NOT EXISTS "${vsg_INCLUDE_DIRS}/vulkan/vulkan.h")
    set(POSSIBLE_VULKAN_INCLUDE_DIRS
        "C:/VulkanSDK/*/Include"
        "C:/Program Files/VulkanSDK/*/Include"
    )
    foreach(DIR ${POSSIBLE_VULKAN_INCLUDE_DIRS})
        file(GLOB VULKAN_DIRS ${DIR})
        foreach(VULKAN_DIR ${VULKAN_DIRS})
            if(EXISTS "${VULKAN_DIR}/vulkan/vulkan.h")
                list(APPEND vsg_INCLUDE_DIRS ${VULKAN_DIR})
                message(STATUS "VSG: 找到Vulkan头文件目录: ${VULKAN_DIR}")
                break()
            endif()
        endforeach()
    endforeach()
endif()


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${vsg_BUILD_MODULES_PATHS_RELEASE} )
    message(${vsg_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


