/// @file InProcessTransport.cpp
/// @brief 单进程内传输实现 — 全部通过 std::function 回调完成。

#include "InProcessTransport.h"
#include <algorithm>
#include <cstring>

namespace vsgraysim::ipc {

InProcessTransport::~InProcessTransport()
{
    stop();
}

bool InProcessTransport::init(const std::string& process_name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    process_name_ = process_name;
    return true;
}

bool InProcessTransport::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = true;
    return true;
}

void InProcessTransport::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    subscribers_.clear();
    services_.clear();
}

std::string InProcessTransport::processName() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return process_name_;
}

bool InProcessTransport::publish(const std::string& topic,
                                  const void* data, size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(topic);
    if (it == subscribers_.end())
        return true; // 没有订阅者不算失败

    for (auto& cb : it->second) {
        if (cb)
            cb(data, size);
    }
    return true;
}

bool InProcessTransport::subscribe(const std::string& topic, SubscribeCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[topic].push_back(std::move(cb));
    return true;
}

void InProcessTransport::unsubscribe(const std::string& topic)
{
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(topic);
}

bool InProcessTransport::serve(const std::string& service, RequestHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    services_[service] = std::move(handler);
    return true;
}

bool InProcessTransport::request(const std::string& service,
                                  const void* req, size_t req_size,
                                  std::vector<uint8_t>& resp,
                                  int timeout_ms)
{
    (void)timeout_ms;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = services_.find(service);
    if (it == services_.end())
        return false;
    return it->second(req, req_size, resp);
}

} // namespace vsgraysim::ipc