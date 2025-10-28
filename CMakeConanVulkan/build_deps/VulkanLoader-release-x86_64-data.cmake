########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(vulkan-loader_COMPONENT_NAMES "")
if(DEFINED vulkan-loader_FIND_DEPENDENCY_NAMES)
  list(APPEND vulkan-loader_FIND_DEPENDENCY_NAMES VulkanHeaders)
  list(REMOVE_DUPLICATES vulkan-loader_FIND_DEPENDENCY_NAMES)
else()
  set(vulkan-loader_FIND_DEPENDENCY_NAMES VulkanHeaders)
endif()
set(VulkanHeaders_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(vulkan-loader_PACKAGE_FOLDER_RELEASE "d:/Conan/conan/p/vulkaaa76f2491d72d/p")
set(vulkan-loader_BUILD_MODULES_PATHS_RELEASE )


set(vulkan-loader_INCLUDE_DIRS_RELEASE "d:/Conan/conan/p/vulka83940be63d03e/p/res/vulkan/registry"
			"d:/Conan/conan/p/vulka83940be63d03e/p/include")
set(vulkan-loader_RES_DIRS_RELEASE )
set(vulkan-loader_DEFINITIONS_RELEASE )
set(vulkan-loader_SHARED_LINK_FLAGS_RELEASE )
set(vulkan-loader_EXE_LINK_FLAGS_RELEASE )
set(vulkan-loader_OBJECTS_RELEASE )
set(vulkan-loader_COMPILE_DEFINITIONS_RELEASE )
set(vulkan-loader_COMPILE_OPTIONS_C_RELEASE )
set(vulkan-loader_COMPILE_OPTIONS_CXX_RELEASE )
set(vulkan-loader_LIB_DIRS_RELEASE "${vulkan-loader_PACKAGE_FOLDER_RELEASE}/lib")
set(vulkan-loader_BIN_DIRS_RELEASE "${vulkan-loader_PACKAGE_FOLDER_RELEASE}/bin")
set(vulkan-loader_LIBRARY_TYPE_RELEASE SHARED)
set(vulkan-loader_IS_HOST_WINDOWS_RELEASE 1)
set(vulkan-loader_LIBS_RELEASE )
set(vulkan-loader_SYSTEM_LIBS_RELEASE )
set(vulkan-loader_FRAMEWORK_DIRS_RELEASE )
set(vulkan-loader_FRAMEWORKS_RELEASE )
set(vulkan-loader_BUILD_DIRS_RELEASE )
set(vulkan-loader_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(vulkan-loader_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${vulkan-loader_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${vulkan-loader_COMPILE_OPTIONS_C_RELEASE}>")
set(vulkan-loader_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${vulkan-loader_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${vulkan-loader_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${vulkan-loader_EXE_LINK_FLAGS_RELEASE}>")


set(vulkan-loader_COMPONENTS_RELEASE )