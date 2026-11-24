#pragma once
/// @file ControlChannel.h
/// @brief 控制通道 — 命令/响应通信。

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vsgraysim::ipc {

class ITransport;

/// 控制消息类型
enum class ControlMsgType : uint32_t {
    PowerOn         = 0x01,  ///< 开机
    PowerOff        = 0x02,  ///< 关机
    SetParam        = 0x03,  ///< 参数调整
    RequestStatus   = 0x04,  ///< 状态查询
    ReportHealth    = 0x05,  ///< 健康报告
    Custom          = 0xFF,  ///< 自定义命令
};

/// 控制消息
struct ControlMessage {
    ControlMsgType  type = ControlMsgType::Custom;  ///< 消息类型
    uint32_t        equipment_id = 0;               ///< 装备 ID
    uint32_t        seq = 0;                        ///< 序列号 (去重)
    std::vector<uint8_t> payload;                   ///< 载荷 (JSON)
};

/// 控制通道 — 用于显控 → 仿真控制的命令/响应
///
/// 服务命名规则: "control/<equipment_type>/<equipment_id>"
///   - control/radar/0
///   - control/sar/1
///   - control/ir/0
class ControlChannel {
public:
    explicit ControlChannel(std::shared_ptr<ITransport> transport);
    ~ControlChannel() = default;

    /// 注册控制命令处理器 (由仿真控制进程调用)
    /// @param equipment_type 装备类型字符串 (radar/sar/ir)
    /// @param equipment_id   装备 ID
    using ControlHandler = std::function<bool(const ControlMessage& cmd,
                                              ControlMessage& response)>;
    bool serve(const std::string& equipment_type,
               uint32_t equipment_id,
               ControlHandler handler);

    /// 发送控制命令并等待响应 (由显控进程调用)
    /// @param equipment_type 装备类型字符串 (radar/sar/ir)
    /// @param equipment_id   装备 ID
    bool sendCommand(const std::string& equipment_type,
                     uint32_t equipment_id,
                     const ControlMessage& cmd,
                     ControlMessage& response,
                     int timeout_ms = 5000);

private:
    static std::string serviceName(const std::string& type, uint32_t id);

    std::shared_ptr<ITransport> transport_;
};

} // namespace vsgraysim::ipc