/// @file TelemetryChannel.cpp
/// @brief 遥测通道实现。

#include "TelemetryChannel.h"
#include "ITransport.h"

#include <cstring>

namespace vsgraysim::ipc {

namespace {

/// 序列化 TelemetryMessage 到字节流
std::vector<uint8_t> serialize(const TelemetryMessage& msg)
{
    std::vector<uint8_t> buf;
    auto append = [&](const void* p, size_t n) {
        auto* begin = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), begin, begin + n);
    };

    uint32_t cat = static_cast<uint32_t>(msg.category);
    append(&cat, sizeof(cat));
    append(&msg.timestamp_ms, sizeof(msg.timestamp_ms));
    append(&msg.source_pid, sizeof(msg.source_pid));

    uint32_t name_size = static_cast<uint32_t>(msg.source_name.size());
    append(&name_size, sizeof(name_size));
    append(msg.source_name.data(), msg.source_name.size());

    uint32_t pl_size = static_cast<uint32_t>(msg.payload.size());
    append(&pl_size, sizeof(pl_size));
    if (!msg.payload.empty())
        append(msg.payload.data(), msg.payload.size());

    return buf;
}

/// 反序列化字节流到 TelemetryMessage
TelemetryMessage deserialize(const void* data, size_t size)
{
    TelemetryMessage msg;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t offset = 0;

    auto read = [&](void* dst, size_t n) -> bool {
        if (offset + n > size) return false;
        std::memcpy(dst, p + offset, n);
        offset += n;
        return true;
    };

    uint32_t cat = 0;
    read(&cat, sizeof(cat));
    msg.category = static_cast<TelemetryCategory>(cat);
    read(&msg.timestamp_ms, sizeof(msg.timestamp_ms));
    read(&msg.source_pid, sizeof(msg.source_pid));

    uint32_t name_size = 0;
    read(&name_size, sizeof(name_size));
    if (name_size > 0) {
        msg.source_name.resize(name_size);
        read(msg.source_name.data(), name_size);
    }

    uint32_t pl_size = 0;
    read(&pl_size, sizeof(pl_size));
    if (pl_size > 0) {
        msg.payload.resize(pl_size);
        read(msg.payload.data(), pl_size);
    }

    return msg;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// TelemetryChannel
// ---------------------------------------------------------------------------
TelemetryChannel::TelemetryChannel(std::shared_ptr<ITransport> transport)
    : transport_(std::move(transport))
{
}

bool TelemetryChannel::send(TelemetryCategory category,
                             const TelemetryMessage& msg)
{
    auto buf = serialize(msg);
    return transport_->publish(topic(category), buf.data(), buf.size());
}

bool TelemetryChannel::subscribe(TelemetryCategory category,
                                  TelemetryCallback cb)
{
    return transport_->subscribe(
        topic(category),
        [cb = std::move(cb)](const void* data, size_t size) {
            auto msg = deserialize(data, size);
            cb(msg);
        });
}

void TelemetryChannel::unsubscribe(TelemetryCategory category)
{
    transport_->unsubscribe(topic(category));
}

std::string TelemetryChannel::topic(TelemetryCategory cat)
{
    switch (cat) {
        case TelemetryCategory::Log:         return "telemetry/log";
        case TelemetryCategory::Fault:       return "telemetry/fault";
        case TelemetryCategory::Performance: return "telemetry/perf";
        case TelemetryCategory::Health:      return "telemetry/health";
    }
    return "telemetry/unknown";
}

} // namespace vsgraysim::ipc