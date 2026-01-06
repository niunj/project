cmake_minimum_required (VERSION 3.7)

project(vsgviewer)

set(CMAKE_MODULE_PATH "${vsgviewer_SOURCE_DIR}/CMakeModules;${CMAKE_MODULE_PATH}")
set(SILVERLINING_DIR $ENV{SILVERLINING_PATH} PATH)

find_package(vsg REQUIRED)
vsg_setup_dir_vars()
vsg_setup_build_vars()
find_package(SilverLining REQUIRED)

# set the use of C++17 globally as all examples require it
set(CMAKE_CXX_STANDARD 17)

include_directories(
	${SILVERLINING_INCLUDE_DIR}
)

add_executable(vsgviewer vsgviewer.cpp 
SetUpLayers.h SetUpLayers.cpp 
SilverLiningCommand.h SilverLiningCommand.cpp 
SilverLiningGroup.h SilverLiningGroup.cpp)

target_link_libraries(vsgviewer 
	vsg::vsg	
	debug ${SILVERLINING_LIBRARY_DEBUG} optimized ${SILVERLINING_LIBRARY_RELEASE}
)
