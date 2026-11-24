#pragma once
/// @file DataChannel.h
/// @brief 数据通道 — GPU 句柄 + 元数据的发布/订阅。

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vsgraysim::ipc {

class ITransport;

/// 数据消息 (GPU 句柄 + 元数据)
struct DataMessage {
    uint64_t    frame_id = 0;        ///< 帧序号
    int32_t     sensor_type = 0;     ///< 传感器类型枚举值
    uint32_t    equipment_id = 0;    ///< 装备 ID
    double      sim_time = 0.0;      ///< 仿真时间戳
    uint32_t    width = 0;           ///< 数据宽度
    uint32_t    height = 0;          ///< 数据高度
    uint32_t    size_bytes = 0;      ///< GPU 缓冲区大小
    // GPU 句柄 (cudaIpcMemHandle 二进制)
    // 实际数据通过 cudaIpcMemHandle 在进程间共享 GPU 内存
    std::vector<uint8_t> gpu_handle; ///< CUDA IPC 句柄 (64 字节)
};

/// 数据通道 — 用于装备数据 (GPU 句柄) 的发布/订阅
///
/// 主题命名规则: "data/<equipment_type>/<equipment_id>"
///   - data/radar/0
///   - data/sar/1
///   - data/ir/0
class DataChannel {
public:
    explicit DataChannel(std::shared_ptr<ITransport> transport);
    ~DataChannel() = default;

    /// 发布数据消息
    /// @param equipment_type 装备类型字符串 (radar/sar/ir)
    /// @param equipment_id   装备 ID
    bool publish(const std::string& equipment_type,
                 uint32_t equipment_id,
                 const DataMessage& msg);

    /// 订阅数据消息
    using DataCallback = std::function<void(const DataMessage& msg)>;

    /// @param equipment_type 装备类型字符串 (radar/sar/ir)
    /// @param equipment_id   装备 ID
    bool subscribe(const std::string& equipment_type,
                   uint32_t equipment_id,
                   DataCallback cb);

    /// 取消订阅
    void unsubscribe(const std::string& equipment_type,
                     uint32_t equipment_id);

private:
    static std::string topic(const std::string& type, uint32_t id);

    std::shared_ptr<ITransport> transport_;
};

} // namespace vsgraysim::ipc