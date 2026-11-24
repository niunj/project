# ---------------------------------------------------------------------------
# vsgraysim_algo: 算法抽象层库
#
# 包含:
#   - IAlgoBase: 算法公共基类
#   - core/: 核心算法基元接口 (FFT/滤波/窗函数/阈值检测)
#   - radar/: 雷达算法抽象接口 (MTI/MTD/CFAR/跟踪)
#   - sar/:   SAR 算法抽象接口 (成像/ATR)
#   - infrared/: 红外算法抽象接口 (图像/测温/检测/融合)
#   - simulation/: 仿真信号生成抽象接口
#   - factory/: 算法工厂 + 配置
# -----------------------------------------------------------------------------

set(ALGO_SOURCES
    factory/AlgoConfig.cpp
    factory/AlgoFactory.cpp
)

add_library(vsgraysim_algo STATIC ${ALGO_SOURCES})
target_include_directories(vsgraysim_algo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(vsgraysim_algo PUBLIC
    vsgraysim_common
)