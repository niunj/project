# ---------------------------------------------------------------------------
# vsgraysim_ipc: 进程间通信库
#
# 包含:
#   - ITransport: 传输层抽象接口
#   - InProcessTransport: 单进程调试实现
#   - ShmTransport: 共享内存 + UDS 实现 (默认)
#   - DataChannel: GPU 句柄 + 元数据发布/订阅
#   - ControlChannel: 命令/响应
#   - TelemetryChannel: 遥测数据
#   - ServiceDiscovery: 进程服务发现
# -----------------------------------------------------------------------------

set(IPC_SOURCES
    InProcessTransport.cpp
    ShmTransport.cpp
    DataChannel.cpp
    ControlChannel.cpp
    TelemetryChannel.cpp
    ServiceDiscovery.cpp
)

add_library(vsgraysim_ipc STATIC ${IPC_SOURCES})
target_include_directories(vsgraysim_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vsgraysim_ipc PUBLIC vsgraysim_common Threads::Threads)