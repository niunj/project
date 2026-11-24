# Equivalent library source list to upstream vsgImGui 0.7.0, with generated
# public ImGui headers in the build tree instead of mutating vendored sources.
set(_imgui_source "${CMAKE_SOURCE_DIR}/third_party/vsgImGui")
set(_imgui_build "${CMAKE_BINARY_DIR}/third_party/vsgImGui")
file(MAKE_DIRECTORY "${_imgui_build}/include/vsgImGui")
foreach(_header imgui.h imconfig.h imgui_internal.h imstb_textedit.h imstb_rectpack.h imstb_truetype.h)
    configure_file("${_imgui_source}/src/imgui/${_header}" "${_imgui_build}/include/vsgImGui/${_header}" COPYONLY)
endforeach()
configure_file("${_imgui_source}/src/imgui/misc/cpp/imgui_stdlib.h" "${_imgui_build}/include/vsgImGui/imgui_stdlib.h" COPYONLY)
foreach(_header implot.h implot_internal.h)
    configure_file("${_imgui_source}/src/implot/${_header}" "${_imgui_build}/include/vsgImGui/${_header}" COPYONLY)
endforeach()
set(_imgui_files
    vsgImGui/RenderImGui.cpp vsgImGui/SendEventsToImGui.cpp vsgImGui/Texture.cpp
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_vulkan.cpp imgui/misc/cpp/imgui_stdlib.cpp
    implot/implot.cpp implot/implot_items.cpp vsgImGui/fallback_demo.cpp)
set(_imgui_sources)
foreach(_source IN LISTS _imgui_files)
    list(APPEND _imgui_sources "${_imgui_source}/src/${_source}")
endforeach()
add_library(vsgImGui ${_imgui_sources})
add_library(vsgImGui::vsgImGui ALIAS vsgImGui)
target_compile_definitions(vsgImGui PRIVATE "IMGUI_USER_CONFIG=<vsgImGui/Export.h>")
if(BUILD_SHARED_LIBS)
    target_compile_definitions(vsgImGui INTERFACE VSGIMGUI_SHARED_LIBRARY)
endif()
target_include_directories(vsgImGui PUBLIC
    "${_imgui_build}/include" "${_imgui_build}/include/vsgImGui"
    "${_imgui_source}/include" "${_imgui_source}/include/vsgImGui")
target_link_libraries(vsgImGui PUBLIC vsg::vsg)
set_target_properties(vsgImGui PROPERTIES VERSION 0.7.0 SOVERSION 0 POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${_imgui_build}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${_imgui_build}/lib")
