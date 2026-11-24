#pragma once
/// @file InProcessTransport.h
/// @brief 单进程内传输实现 (调试用, 零拷贝函数回调)。

#include "ITransport.h"

#include <map>
#include <mutex>
#include <vector>

namespace vsgraysim::ipc {

/// 单进程内传输实现
///
/// 所有 publish/subscribe 和 request/reply 都在同一进程内
/// 通过 std::function 回调完成, 不涉及任何系统 IPC 机制。
/// 用于单进程调试模式, 行为与多进程模式一致但无通信开销。
class InProcessTransport final : public ITransport {
public:
    InProcessTransport() = default;
    ~InProcessTransport() override;

    // ITransport
    bool init(const std::string& process_name) override;
    bool start() override;
    void stop() override;
    std::string processName() const override;

    bool publish(const std::string& topic, const void* data, size_t size) override;
    bool subscribe(const std::string& topic, SubscribeCallback cb) override;
    void unsubscribe(const std::string& topic) override;

    bool serve(const std::string& service, RequestHandler handler) override;
    bool request(const std::string& service,
                 const void* req, size_t req_size,
                 std::vector<uint8_t>& resp,
                 int timeout_ms = 5000) override;

private:
    std::string process_name_;
    bool running_ = false;
    uint64_t seq_ = 0;

    mutable std::mutex mutex_;

    // 发布/订阅: topic -> [callbacks]
    std::map<std::string, std::vector<SubscribeCallback>> subscribers_;

    // 请求/回复: service -> handler
    std::map<std::string, RequestHandler> services_;
};

} // namespace vsgraysim::ipc