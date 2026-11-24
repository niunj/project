#pragma once
/// @file TelemetryChannel.h
/// @brief 遥测通道 — 日志/故障/性能数据的发布/订阅。

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vsgraysim::ipc {

class ITransport;

/// 遥测消息类别
enum class TelemetryCategory : uint32_t {
    Log         = 0x01,  ///< 结构化日志
    Fault       = 0x02,  ///< 故障事件
    Performance = 0x03,  ///< 性能指标
    Health      = 0x04,  ///< 健康状态
};

/// 遥测消息
struct TelemetryMessage {
    TelemetryCategory category = TelemetryCategory::Log;
    uint64_t          timestamp_ms = 0;
    uint32_t          source_pid = 0;
    std::string       source_name;
    std::vector<uint8_t> payload;  ///< JSON 载荷
};

/// 遥测通道 — 各进程将日志/故障/性能数据发送到 Launcher 聚合
///
/// 主题: "telemetry/<category>"
///   - telemetry/log
///   - telemetry/fault
///   - telemetry/perf
///   - telemetry/health
class TelemetryChannel {
public:
    explicit TelemetryChannel(std::shared_ptr<ITransport> transport);
    ~TelemetryChannel() = default;

    /// 发送遥测消息
    bool send(TelemetryCategory category, const TelemetryMessage& msg);

    /// 订阅遥测类别 (由 Launcher 进程调用)
    using TelemetryCallback = std::function<void(const TelemetryMessage& msg)>;
    bool subscribe(TelemetryCategory category, TelemetryCallback cb);

    /// 取消订阅
    void unsubscribe(TelemetryCategory category);

private:
    static std::string topic(TelemetryCategory cat);

    std::shared_ptr<ITransport> transport_;
};

} // namespace vsgraysim::ipc