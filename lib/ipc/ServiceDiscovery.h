#pragma once
/// @file ServiceDiscovery.h
/// @brief 进程服务发现 (本地)。

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vsgraysim::ipc {

/// 进程信息
struct ProcessInfo {
    std::string name;          ///< 进程名称
    uint32_t    pid = 0;       ///< 进程 PID
    std::string socket_path;   ///< UDS 套接字路径
    bool        alive = false; ///< 是否存活
};

/// 进程状态变化回调
using ProcessStatusCallback = std::function<void(const ProcessInfo& info)>;

/// 服务发现 — 本地进程注册与发现
///
/// 机制:
///   - Launcher 进程作为服务注册中心
///   - 子进程启动后通过 UDS 向 Launcher 注册
///   - 子进程退出时自动注销
///   - Launcher 定期健康检查
class ServiceDiscovery {
public:
    ServiceDiscovery();
    ~ServiceDiscovery();

    /// 初始化服务发现
    /// @param process_name 本进程名称
    /// @param socket_path  UDS 套接字路径 (空则自动生成)
    /// @param is_launcher  是否为 Launcher 进程
    bool init(const std::string& process_name,
              const std::string& socket_path = {},
              bool is_launcher = false);

    /// 启动服务发现
    bool start();

    /// 停止服务发现
    void stop();

    /// 注册本进程 (子进程启动后调用)
    bool registerSelf();

    /// 注销本进程
    void unregisterSelf();

    /// 获取所有已知进程
    std::vector<ProcessInfo> knownProcesses() const;

    /// 获取特定进程信息
    ProcessInfo findProcess(const std::string& name) const;

    /// 设置进程状态变化回调
    void setStatusCallback(ProcessStatusCallback cb);

    /// 返回本进程信息
    ProcessInfo selfInfo() const { return self_; }

private:
    ProcessInfo self_;
    bool is_launcher_ = false;
    bool running_ = false;
    int listen_fd_ = -1;
    std::unique_ptr<class ServiceDiscoveryImpl> impl_;
};

} // namespace vsgraysim::ipc