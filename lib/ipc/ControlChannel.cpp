/// @file ControlChannel.cpp
/// @brief 控制通道实现 — 命令/响应消息的序列化/反序列化。

#include "ControlChannel.h"
#include "ITransport.h"

#include <cstring>
#include <sstream>

namespace vsgraysim::ipc {

namespace {

/// 序列化 ControlMessage 到字节流
std::vector<uint8_t> serialize(const ControlMessage& msg)
{
    std::vector<uint8_t> buf;
    buf.reserve(4 + 4 + 4 + msg.payload.size());

    auto append = [&](const void* p, size_t n) {
        auto* begin = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), begin, begin + n);
    };

    uint32_t type_val = static_cast<uint32_t>(msg.type);
    append(&type_val, sizeof(type_val));
    append(&msg.equipment_id, sizeof(msg.equipment_id));
    append(&msg.seq, sizeof(msg.seq));

    uint32_t pl_size = static_cast<uint32_t>(msg.payload.size());
    append(&pl_size, sizeof(pl_size));
    if (!msg.payload.empty())
        append(msg.payload.data(), msg.payload.size());

    return buf;
}

/// 反序列化字节流到 ControlMessage
ControlMessage deserialize(const void* data, size_t size)
{
    ControlMessage msg;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t offset = 0;

    auto read = [&](void* dst, size_t n) -> bool {
        if (offset + n > size) return false;
        std::memcpy(dst, p + offset, n);
        offset += n;
        return true;
    };

    uint32_t type_val = 0;
    read(&type_val, sizeof(type_val));
    msg.type = static_cast<ControlMsgType>(type_val);
    read(&msg.equipment_id, sizeof(msg.equipment_id));
    read(&msg.seq, sizeof(msg.seq));

    uint32_t pl_size = 0;
    read(&pl_size, sizeof(pl_size));
    if (pl_size > 0) {
        msg.payload.resize(pl_size);
        read(msg.payload.data(), pl_size);
    }

    return msg;
}

/// 包装处理函数, 使 RequestHandler 适配 ControlHandler
bool wrapControlHandler(const ControlMessage& cmd,
                         ControlMessage& response,
                         const ControlChannel::ControlHandler& handler,
                         const void* req_data, size_t req_size,
                         std::vector<uint8_t>& resp_data)
{
    auto req = deserialize(req_data, req_size);
    ControlMessage resp;
    bool ok = handler(req, resp);
    if (ok) {
        resp_data = serialize(resp);
    }
    return ok;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ControlChannel
// ---------------------------------------------------------------------------
ControlChannel::ControlChannel(std::shared_ptr<ITransport> transport)
    : transport_(std::move(transport))
{
}

bool ControlChannel::serve(const std::string& equipment_type,
                            uint32_t equipment_id,
                            ControlHandler handler)
{
    auto svc = serviceName(equipment_type, equipment_id);

    // 包装 ControlHandler 为 RequestHandler
    return transport_->serve(
        svc,
        [handler = std::move(handler)](const void* req, size_t req_size,
                                        std::vector<uint8_t>& resp) -> bool {
            auto cmd = deserialize(req, req_size);
            ControlMessage response;
            bool ok = handler(cmd, response);
            if (ok) {
                resp = serialize(response);
            }
            return ok;
        });
}

bool ControlChannel::sendCommand(const std::string& equipment_type,
                                  uint32_t equipment_id,
                                  const ControlMessage& cmd,
                                  ControlMessage& response,
                                  int timeout_ms)
{
    auto svc = serviceName(equipment_type, equipment_id);
    auto req_data = serialize(cmd);
    std::vector<uint8_t> resp_data;

    bool ok = transport_->request(svc, req_data.data(), req_data.size(),
                                   resp_data, timeout_ms);
    if (ok && !resp_data.empty()) {
        response = deserialize(resp_data.data(), resp_data.size());
    }
    return ok;
}

std::string ControlChannel::serviceName(const std::string& type, uint32_t id)
{
    return "control/" + type + "/" + std::to_string(id);
}

} // namespace vsgraysim::ipc