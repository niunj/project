# -----------------------------------------------------------------------------
# vsgraysim_objloader — 自研 Wavefront OBJ 加载器 (自包含, 零业务依赖)。
#
# 背景: 本环境的 vsgXchange 编译时**未启用 assimp**, 其 assimp reader 为
# 恒返回 null 的桩 → vsg::read_cast 对一切 .obj 静默失败。本加载器支持
# 项目模型实际使用的 OBJ 子集 (v/vn/vt/f + mtl Kd), 见
# include/vsgraysim/core/ObjLoader.h 头注释。
#
# 极小依赖 (vsg + spdlog 头), 由 vsgraysim_core (SceneBuilder) 与
# env_modules 测试程序 (env_viewer MeshLoader 注入) 共享, 单一实现源。
# -----------------------------------------------------------------------------
get_filename_component(objloader_repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT SPDLOG_INCLUDE_DIR)
    set(SPDLOG_INCLUDE_DIR "${objloader_repo}/third_party/spdlog/include")
endif()
add_library(vsgraysim_objloader STATIC ObjLoader.cpp)
target_compile_features(vsgraysim_objloader PUBLIC cxx_std_17)
target_link_libraries(vsgraysim_objloader PUBLIC vsg::vsg)
target_include_directories(vsgraysim_objloader
    PUBLIC  ${objloader_repo}/include
    PRIVATE ${SPDLOG_INCLUDE_DIR}
)
set_target_properties(vsgraysim_objloader PROPERTIES POSITION_INDEPENDENT_CODE ON)
