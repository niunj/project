#pragma once
/// @file ITransport.h
/// @brief 进程间通信传输层抽象接口。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vsgraysim::ipc {

/// 消息头 (所有 IPC 消息的固定前缀)
struct MessageHeader {
    uint64_t    seq;              ///< 序列号
    uint32_t    source_id;        ///< 源进程 ID
    uint32_t    msg_type;         ///< 消息类型 (由通道定义)
    uint64_t    timestamp_ms;     ///< 时间戳 (毫秒)
    uint32_t    payload_size;     ///< 载荷大小 (字节)
};

/// 传输层抽象接口
///
/// 支持两种通信模式:
///   - 发布/订阅 (Publish/Subscribe): 一对多数据分发
///   - 请求/回复 (Request/Reply): 点对点命令/响应
///
/// 三种实现:
///   - InProcessTransport: 单进程调试 (函数回调)
///   - ShmTransport:      共享内存 + Unix Domain Socket (默认)
///   - ZmqTransport:      ZMQ 多机部署 (预留)
class ITransport {
public:
    virtual ~ITransport() = default;

    /// 初始化传输层
    /// @param process_name 进程名称 (用于服务发现)
    virtual bool init(const std::string& process_name) = 0;

    /// 启动传输层 (开始监听)
    virtual bool start() = 0;

    /// 停止传输层
    virtual void stop() = 0;

    /// 返回进程名称
    virtual std::string processName() const = 0;

    // ---- 发布/订阅 ----

    using SubscribeCallback = std::function<void(const void* data, size_t size)>;

    /// 发布消息到主题
    virtual bool publish(const std::string& topic, const void* data, size_t size) = 0;

    /// 订阅主题
    virtual bool subscribe(const std::string& topic, SubscribeCallback cb) = 0;

    /// 取消订阅
    virtual void unsubscribe(const std::string& topic) = 0;

    // ---- 请求/回复 ----

    using RequestHandler = std::function<bool(const void* req, size_t req_size,
                                              std::vector<uint8_t>& resp)>;

    /// 注册服务处理器
    virtual bool serve(const std::string& service, RequestHandler handler) = 0;

    /// 发送请求并等待回复
    /// @return true 表示收到回复, false 表示超时或错误
    virtual bool request(const std::string& service,
                         const void* req, size_t req_size,
                         std::vector<uint8_t>& resp,
                         int timeout_ms = 5000) = 0;
};

} // namespace vsgraysim::ipc