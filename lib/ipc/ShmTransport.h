#pragma once
/// @file ShmTransport.h
/// @brief 共享内存进程间传输实现 (基于 Unix Domain Socket + POSIX 共享内存)。

#include "ITransport.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vsgraysim::ipc {

/// POSIX 共享内存传输实现
///
/// 通信机制:
///   - Unix Domain Socket (UDS): 控制消息交换 (订阅注册、通知、小数据)
///   - POSIX Shared Memory (shm_open): 大数据传输 (GPU 句柄、元数据)
///   - POSIX Semaphores: 读写同步
///
/// 服务发现:
///   - 每个进程在 /tmp/vsgraysim/ 目录下创建 UDS 监听套接字
///   - Launcher 进程作为服务注册中心
class ShmTransport final : public ITransport {
public:
    ShmTransport();
    ~ShmTransport() override;

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

    /// 注册一个已知远端进程 (显式添加; 也可由 refreshKnownProcesses 扫描获得)
    void setKnownProcess(const std::string& name, const std::string& socket_path);

    /// 扫描套接字目录, 把除自身外的所有进程套接字加入已知列表。
    /// @return 当前已知进程数
    size_t refreshKnownProcesses();
    uint64_t receivedBytes(const std::string& topic) const;

private:
    void listenerThread();
    void handleConnection(int client_fd);
    bool saveSubscriptionsLocked();

    std::string process_name_;
    std::string socket_path_;
    std::string socket_directory_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    uint64_t seq_ = 0;
    int64_t  last_scan_ms_ = 0;   // 最近进程发现扫描时刻 (steady ms, 节流)

    mutable std::mutex mutex_;

    std::unique_ptr<std::thread> listener_thread_;

    // 发布的主题 -> 共享内存名称
    std::map<std::string, std::string> published_topics_;

    // 订阅的主题 -> 回调
    std::map<std::string, std::vector<SubscribeCallback>> subscribers_;
    std::map<std::string,uint64_t> received_bytes_;

    // 服务 -> 处理器
    std::map<std::string, RequestHandler> services_;

    // 已知的所有进程 (从服务发现获取)
    // process_name -> socket_path
    std::map<std::string, std::string> known_processes_;
};

} // namespace vsgraysim::ipc
