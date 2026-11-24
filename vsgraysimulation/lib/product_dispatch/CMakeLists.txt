# -----------------------------------------------------------------------------
# vsgraysim_product_dispatch — B5 分发时序桥接 (渲染链 → 传输帧)。
#
# ProductFrame (rocky_env 渲染链产品帧) → IrFrame/VisFrame/RadarFrame/SarFrame
# (vsgraysim/broker/SensorFrames.h 传输帧) 的映射 + stages/链接/供电三重门控。
# 仿真控制软件与 env_viewer 测试程序共享单一实现 (仿 objloader 先例)。
#
# 极小依赖: vsg 头 (SensorDesc 几何类型) + 双侧公共头 (纯数据结构, 零符号链接)。
# -----------------------------------------------------------------------------
add_library(vsgraysim_product_dispatch STATIC src/ProductDispatch.cpp)
target_include_directories(vsgraysim_product_dispatch
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include            # vsgraysim/broker/SensorFrames.h
            ${CMAKE_SOURCE_DIR}/env_modules/include # rocky_env/SensorApi.h + SceneSource.h
)
target_link_libraries(vsgraysim_product_dispatch PUBLIC vsg::vsg)
set_target_properties(vsgraysim_product_dispatch PROPERTIES POSITION_INDEPENDENT_CODE ON)
