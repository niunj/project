# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(vsg_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(vsg_FRAMEWORKS_FOUND_RELEASE "${vsg_FRAMEWORKS_RELEASE}" "${vsg_FRAMEWORK_DIRS_RELEASE}")

set(vsg_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET vsg_DEPS_TARGET)
    add_library(vsg_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET vsg_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${vsg_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${vsg_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:Vulkan::Loader>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### vsg_DEPS_TARGET to all of them
conan_package_library_targets("${vsg_LIBS_RELEASE}"    # libraries
                              "${vsg_LIB_DIRS_RELEASE}" # package_libdir
                              "${vsg_BIN_DIRS_RELEASE}" # package_bindir
                              "${vsg_LIBRARY_TYPE_RELEASE}"
                              "${vsg_IS_HOST_WINDOWS_RELEASE}"
                              vsg_DEPS_TARGET
                              vsg_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "vsg"    # package_name
                              "${vsg_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${vsg_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## GLOBAL TARGET PROPERTIES Release ########################################
    set_property(TARGET vsg::vsg
                 APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                 $<$<CONFIG:Release>:${vsg_OBJECTS_RELEASE}>
                 $<$<CONFIG:Release>:${vsg_LIBRARIES_TARGETS}>
                 )

    if("${vsg_LIBS_RELEASE}" STREQUAL "")
        # If the package is not declaring any "cpp_info.libs" the package deps, system libs,
        # frameworks etc are not linked to the imported targets and we need to do it to the
        # global target
        set_property(TARGET vsg::vsg
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     vsg_DEPS_TARGET)
    endif()

    set_property(TARGET vsg::vsg
                 APPEND PROPERTY INTERFACE_LINK_OPTIONS
                 $<$<CONFIG:Release>:${vsg_LINKER_FLAGS_RELEASE}>)
    set_property(TARGET vsg::vsg
                 APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 $<$<CONFIG:Release>:${vsg_INCLUDE_DIRS_RELEASE}>)
    # Necessary to find LINK shared libraries in Linux
    set_property(TARGET vsg::vsg
                 APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                 $<$<CONFIG:Release>:${vsg_LIB_DIRS_RELEASE}>)
    set_property(TARGET vsg::vsg
                 APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                 $<$<CONFIG:Release>:${vsg_COMPILE_DEFINITIONS_RELEASE}>)
    set_property(TARGET vsg::vsg
                 APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                 $<$<CONFIG:Release>:${vsg_COMPILE_OPTIONS_RELEASE}>)

########## For the modules (FindXXX)
set(vsg_LIBRARIES_RELEASE vsg::vsg)
