cmake_minimum_required (VERSION 2.6)

set( APP_NAME osgshadow )

project (sample_shadow)

if( EXISTS $ENV{OSGDIR} )
	set (OSGDIR $ENV{OSGDIR})
else()
	set (OSGDIR $ENV{OSG_DIR})	
endif()

if(NOT OSGDIR)
	message(FATAL_ERROR "OSGDIR or OSG_DIR Environment Variable not set!")
else(NOT OSGDIR)
	message(STATUS "OSGDIR environment variable is " ${OSGDIR})
endif(NOT OSGDIR)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules;${CMAKE_MODULE_PATH}")
set(SILVERLINING_DIR $ENV{SILVERLINING_PATH} PATH)


find_package(OpenSceneGraph 2.8.3 COMPONENTS osgDB osgUtil osgGA osgViewer osgText osgShadow REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SilverLining REQUIRED)


include_directories(
	${OPENSCENEGRAPH_INCLUDE_DIR}
	${OPENGL_INCLUDE_DIR}
	${SILVERLINING_INCLUDE_DIR}
)

file(GLOB cpp_files *.cpp src/*.cpp)
file(GLOB h_files *.h src/*.h)

add_executable(sample_shadow ${cpp_files} ${h_files} )

target_link_libraries(sample_shadow
	debug ${SILVERLINING_LIBRARY_DEBUG} optimized ${SILVERLINING_LIBRARY_RELEASE}
	${OPENSCENEGRAPH_LIBRARIES}
	${OPENGL_LIBRARY}
	${GLU_LIBRARY}
)
