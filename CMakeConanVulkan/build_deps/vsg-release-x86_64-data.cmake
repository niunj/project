########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(vsg_COMPONENT_NAMES "")
if(DEFINED vsg_FIND_DEPENDENCY_NAMES)
  list(APPEND vsg_FIND_DEPENDENCY_NAMES VulkanLoader)
  list(REMOVE_DUPLICATES vsg_FIND_DEPENDENCY_NAMES)
else()
  set(vsg_FIND_DEPENDENCY_NAMES VulkanLoader)
endif()
set(VulkanLoader_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(vsg_PACKAGE_FOLDER_RELEASE "d:/Conan/conan/p/vsgaf133ae3ad3cc/p")
set(vsg_BUILD_MODULES_PATHS_RELEASE )


set(vsg_INCLUDE_DIRS_RELEASE "${vsg_PACKAGE_FOLDER_RELEASE}/include")
set(vsg_RES_DIRS_RELEASE )
set(vsg_DEFINITIONS_RELEASE )
set(vsg_SHARED_LINK_FLAGS_RELEASE )
set(vsg_EXE_LINK_FLAGS_RELEASE )
set(vsg_OBJECTS_RELEASE )
set(vsg_COMPILE_DEFINITIONS_RELEASE )
set(vsg_COMPILE_OPTIONS_C_RELEASE )
set(vsg_COMPILE_OPTIONS_CXX_RELEASE )
set(vsg_LIB_DIRS_RELEASE "${vsg_PACKAGE_FOLDER_RELEASE}/lib")
set(vsg_BIN_DIRS_RELEASE "${vsg_PACKAGE_FOLDER_RELEASE}/bin")
set(vsg_LIBRARY_TYPE_RELEASE SHARED)
set(vsg_IS_HOST_WINDOWS_RELEASE 1)
set(vsg_LIBS_RELEASE vsg)
set(vsg_SYSTEM_LIBS_RELEASE )
set(vsg_FRAMEWORK_DIRS_RELEASE )
set(vsg_FRAMEWORKS_RELEASE )
set(vsg_BUILD_DIRS_RELEASE )
set(vsg_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(vsg_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${vsg_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${vsg_COMPILE_OPTIONS_C_RELEASE}>")
set(vsg_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${vsg_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${vsg_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${vsg_EXE_LINK_FLAGS_RELEASE}>")


set(vsg_COMPONENTS_RELEASE )