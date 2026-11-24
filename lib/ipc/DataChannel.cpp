/// @file DataChannel.cpp
/// @brief 数据通道实现 — GPU 句柄消息的序列化/反序列化。

#include "DataChannel.h"
#include "ITransport.h"

#include <cstring>
#include <sstream>

namespace vsgraysim::ipc {

namespace {

/// 序列化 DataMessage 到字节流
std::vector<uint8_t> serialize(const DataMessage& msg)
{
    std::vector<uint8_t> buf;
    buf.reserve(sizeof(DataMessage) + msg.gpu_handle.size());

    // 逐个字段写入 (避免直接 memcpy 整个结构体, 保持跨平台兼容)
    auto append = [&](const void* p, size_t n) {
        auto* begin = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), begin, begin + n);
    };

    append(&msg.frame_id, sizeof(msg.frame_id));
    append(&msg.sensor_type, sizeof(msg.sensor_type));
    append(&msg.equipment_id, sizeof(msg.equipment_id));
    append(&msg.sim_time, sizeof(msg.sim_time));
    append(&msg.width, sizeof(msg.width));
    append(&msg.height, sizeof(msg.height));
    append(&msg.size_bytes, sizeof(msg.size_bytes));

    uint32_t handle_size = static_cast<uint32_t>(msg.gpu_handle.size());
    append(&handle_size, sizeof(handle_size));
    if (!msg.gpu_handle.empty())
        append(msg.gpu_handle.data(), msg.gpu_handle.size());

    return buf;
}

/// 反序列化字节流到 DataMessage
DataMessage deserialize(const void* data, size_t size)
{
    DataMessage msg;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t offset = 0;

    auto read = [&](void* dst, size_t n) -> bool {
        if (offset + n > size) return false;
        std::memcpy(dst, p + offset, n);
        offset += n;
        return true;
    };

    read(&msg.frame_id, sizeof(msg.frame_id));
    read(&msg.sensor_type, sizeof(msg.sensor_type));
    read(&msg.equipment_id, sizeof(msg.equipment_id));
    read(&msg.sim_time, sizeof(msg.sim_time));
    read(&msg.width, sizeof(msg.width));
    read(&msg.height, sizeof(msg.height));
    read(&msg.size_bytes, sizeof(msg.size_bytes));

    uint32_t handle_size = 0;
    read(&handle_size, sizeof(handle_size));
    if (handle_size > 0) {
        msg.gpu_handle.resize(handle_size);
        read(msg.gpu_handle.data(), handle_size);
    }

    return msg;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// DataChannel
// ---------------------------------------------------------------------------
DataChannel::DataChannel(std::shared_ptr<ITransport> transport)
    : transport_(std::move(transport))
{
}

bool DataChannel::publish(const std::string& equipment_type,
                           uint32_t equipment_id,
                           const DataMessage& msg)
{
    auto buf = serialize(msg);
    return transport_->publish(topic(equipment_type, equipment_id),
                               buf.data(), buf.size());
}

bool DataChannel::subscribe(const std::string& equipment_type,
                             uint32_t equipment_id,
                             DataCallback cb)
{
    return transport_->subscribe(
        topic(equipment_type, equipment_id),
        [cb = std::move(cb)](const void* data, size_t size) {
            auto msg = deserialize(data, size);
            cb(msg);
        });
}

void DataChannel::unsubscribe(const std::string& equipment_type,
                               uint32_t equipment_id)
{
    transport_->unsubscribe(topic(equipment_type, equipment_id));
}

std::string DataChannel::topic(const std::string& type, uint32_t id)
{
    return "data/" + type + "/" + std::to_string(id);
}

} // namespace vsgraysim::ipc