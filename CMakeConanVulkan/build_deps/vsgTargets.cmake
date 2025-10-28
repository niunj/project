# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/vsg-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${vsg_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${vsg_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET vsg::vsg)
    add_library(vsg::vsg INTERFACE IMPORTED)
    message(${vsg_MESSAGE_MODE} "Conan: Target declared 'vsg::vsg'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/vsg-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()