cmake_minimum_required (VERSION 2.6)

project(sample_single)

if( EXISTS $ENV{OSGDIR} )
	set (OSGDIR $ENV{OSGDIR})
else()
	set (OSGDIR $ENV{OSG_DIR})	
endif()

if(NOT OSGDIR)
	message(FATAL_ERROR "OSGDIR Environment Variable not set!")
else(NOT OSGDIR)
	message(STATUS "OSGDIR environment variable is " $ENV{OSGDIR})
endif(NOT OSGDIR)

set(CMAKE_MODULE_PATH "${sample_single_SOURCE_DIR}/CMakeModules;${CMAKE_MODULE_PATH}")
set(SILVERLINING_DIR $ENV{SILVERLINING_PATH} PATH)


find_package(OpenSceneGraph 2.0.0 COMPONENTS osgDB osgUtil osgGA osgViewer osgText REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLU REQUIRED)
find_package(SilverLining REQUIRED)


include_directories(
	${OPENSCENEGRAPH_INCLUDE_DIR}
	${OPENGL_INCLUDE_DIR}
	${SILVERLINING_INCLUDE_DIR}
)

add_executable(sample_single CloudsDrawable.cpp osgviewer.cpp SkyDrawable.cpp)

target_link_libraries(sample_single 
	debug ${SILVERLINING_LIBRARY_DEBUG} optimized ${SILVERLINING_LIBRARY_RELEASE}
	${OPENSCENEGRAPH_LIBRARIES}
	${OPENGL_LIBRARY}
	${GLU_LIBRARY}
)
