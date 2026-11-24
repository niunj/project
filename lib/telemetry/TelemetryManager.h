#pragma once
/// @file TelemetryManager.h
/// @brief 进程遥测管理器 — 健康/日志/故障/性能上报。
///
/// 每个进程创建一个 TelemetryManager 实例:
///   1. 通过 ServiceDiscovery 向 Launcher 注册自身
///   2. 打开 UDS 遥测服务器, 等待 Launcher 连接
///   3. 定时推送健康状态 (CPU/内存/FPS/运行时长)
///   4. 提供日志/故障/性能指标上报 API
///
/// Launcher 的 HealthPanel 自动发现进程并连接其遥测服务器。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace vsgraysim::telemetry {

/// 遥测消息类型
enum class TelemetryMsgType : uint8_t {
    Health    = 0x01,  ///< 健康状态 (JSON)
    Log       = 0x02,  ///< 日志 (JSON)
    Fault     = 0x03,  ///< 故障上报 (JSON)
    Metric    = 0x04,  ///< 性能指标 (JSON)
};

/// 日志级别
enum class LogLevel : uint8_t {
    Debug   = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3,
    Fatal   = 4,
};

/// 遥测消息 (二进制格式: type[1] + payload_size[4] + payload[payload_size])
struct TelemetryPacket {
    TelemetryMsgType type;
    std::string      payload;  ///< JSON 字符串
};

/// 进程健康状态
struct ProcessHealth {
    std::string process_name;
    uint32_t    pid = 0;
    double      fps = 0.0;
    double      cpu_percent = 0.0;
    size_t      mem_bytes = 0;
    double      uptime_sec = 0.0;
    std::string status = "unknown";  ///< "running", "warning", "error", "stopped"
    int64_t     timestamp_ms = 0;    ///< 上报时间戳
};

/// 遥测服务器回调: 当 Launcher 连接时通知
using TelemetryConnectCallback = std::function<void()>;

/// 遥测管理器
class TelemetryManager {
public:
    TelemetryManager();
    ~TelemetryManager();

    // 禁用拷贝
    TelemetryManager(const TelemetryManager&) = delete;
    TelemetryManager& operator=(const TelemetryManager&) = delete;

    /// 初始化: 注册到 ServiceDiscovery, 启动 UDS 遥测服务器
    /// @param process_name  进程名称 (如 "scene_designer", "sim_control")
    /// @param discovery_path  ServiceDiscovery 套接字路径 (默认 Launcher 路径)
    /// @return 是否成功
    bool init(const std::string& process_name,
              const std::string& discovery_path = "/tmp/vsgraysim/discovery.sock");

    /// 关闭: 注销服务发现, 停止遥测服务器
    void shutdown();

    /// 是否已初始化
    bool isInitialized() const { return initialized_; }

    /// 设置健康上报间隔 (秒, 默认 2)
    void setHealthIntervalSec(int sec) { health_interval_sec_ = sec; }

    // ---- 日志 API ----
    void log(LogLevel level, const std::string& module, const std::string& message);
    void logInfo   (const std::string& module, const std::string& msg);
    void logWarn   (const std::string& module, const std::string& msg);
    void logError  (const std::string& module, const std::string& msg);
    void logFatal  (const std::string& module, const std::string& msg);

    // ---- 故障 API ----
    void reportFault(const std::string& module, int code,
                     const std::string& description);
    void clearFault(const std::string& module, int code);

    // ---- 性能指标 API ----
    void reportMetric(const std::string& name, double value,
                      const std::string& unit = "");

    // ---- 健康状态更新 ----
    void setFps(double fps);
    void setCpuPercent(double pct);
    void setMemBytes(size_t bytes);
    void setCustomStatus(const std::string& status);

    // ---- 连接回调 ----
    void setConnectCallback(TelemetryConnectCallback cb);

    // ---- 访问器 ----
    const std::string& processName() const { return process_name_; }

    // ---- 系统工具 ----
    /// 读取当前进程内存使用 (RSS, bytes)
    static size_t readMemoryBytes();

    /// 读取当前进程 CPU 使用率 (基于两次调用之间的差值)
    /// @param reset 第一次调用时设为 true 初始化基准
    /// @return CPU 使用率百分比 (0~100)
    static double readCpuPercent(bool reset = false);

private:
    // UDS 服务器线程
    void serverThread();
    void handleClient(int client_fd);
    void sendPacket(int fd, const TelemetryPacket& pkt);

    // 健康上报
    void sendHealthReport();
    void sendLog(LogLevel level, const std::string& module,
                 const std::string& message);
    void sendTelemetry(TelemetryMsgType type, const std::string& json_payload);

    // 状态
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};

    // 进程信息
    std::string process_name_;
    uint32_t    pid_ = 0;
    std::string discovery_path_;
    std::string telemetry_socket_path_;

    // 健康状态
    std::mutex     health_mutex_;
    double         fps_ = 0.0;
    double         cpu_pct_ = 0.0;
    size_t         mem_bytes_ = 0;
    std::string    custom_status_;
    std::chrono::steady_clock::time_point start_time_;

    // 服务器
    int server_fd_ = -1;
    std::thread server_thread_;
    std::atomic<int> health_interval_sec_{2};

    // 连接回调
    TelemetryConnectCallback connect_cb_;

    // 当前连接的客户端 (Launcher HealthPanel)
    std::mutex clients_mutex_;
    std::vector<int> client_fds_;

    // ServiceDiscovery 实例
    class ServiceDiscoveryClient;
    std::unique_ptr<ServiceDiscoveryClient> discovery_;
};

} // namespace vsgraysim::telemetry