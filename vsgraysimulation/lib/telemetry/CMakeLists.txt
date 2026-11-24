# ---------------------------------------------------------------------------
# vsgraysim_telemetry: 进程遥测库
#
# 包含:
#   - TelemetryManager: 进程健康/日志/故障/性能上报
#   - 每个进程创建一个 TelemetryManager 实例,
#     通过 ServiceDiscovery 向 Launcher 注册,
#     通过 UDS 向 Launcher 的 HealthPanel 推送遥测数据。
# -----------------------------------------------------------------------------

add_library(vsgraysim_telemetry STATIC
    TelemetryManager.cpp
)

target_include_directories(vsgraysim_telemetry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vsgraysim_telemetry PUBLIC
    vsgraysim_ipc
    vsgraysim_common
    Threads::Threads
)