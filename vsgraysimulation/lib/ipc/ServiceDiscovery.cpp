/// @file ServiceDiscovery.cpp
/// @brief 进程服务发现实现。

#include "ServiceDiscovery.h"
#include "IpcPaths.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace vsgraysim::ipc {

namespace {

/// 确保目录存在
bool ensureDir(const char* path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode);
    return mkdir(path, 0755) == 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ServiceDiscoveryImpl (PIMPL)
// ---------------------------------------------------------------------------
class ServiceDiscoveryImpl {
public:
    int listen_fd_ = -1;
    std::unique_ptr<std::thread> listener_thread_;
    std::map<std::string, ProcessInfo> processes_;
    ProcessStatusCallback status_cb_;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::string self_socket_path_;
    std::string socket_directory_,discovery_socket_path_;
};

// ---------------------------------------------------------------------------
// ServiceDiscovery
// ---------------------------------------------------------------------------
ServiceDiscovery::ServiceDiscovery()
    : impl_(std::make_unique<ServiceDiscoveryImpl>())
{
}

ServiceDiscovery::~ServiceDiscovery()
{
    stop();
}

bool ServiceDiscovery::init(const std::string& process_name,
                             const std::string& socket_path,
                             bool is_launcher)
{
    self_.name = process_name;
    self_.pid = static_cast<uint32_t>(getpid());
    is_launcher_ = is_launcher;
    impl_->socket_directory_=ipcDirectory();
    impl_->discovery_socket_path_=ipcSocketPath(impl_->socket_directory_,"discovery");
    if(impl_->discovery_socket_path_.empty()) return false;

    if (!socket_path.empty()) {
        self_.socket_path = socket_path;
    } else {
        self_.socket_path = ipcSocketPath(impl_->socket_directory_,process_name);
    }

    sockaddr_un address{};
    if(!unixSocketAddress(address,self_.socket_path)) return false;
    impl_->self_socket_path_ = self_.socket_path;
    return true;
}

bool ServiceDiscovery::start()
{
    if (impl_->running_.load())
        return true;

    if (impl_->discovery_socket_path_.empty() || !ensureDir(impl_->socket_directory_.c_str()))
        return false;

    if (is_launcher_) {
        // Launcher 进程: 创建服务发现监听套接字
        unlink(impl_->discovery_socket_path_.c_str());

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;

        struct sockaddr_un addr;
        if(!unixSocketAddress(addr,impl_->discovery_socket_path_)) { close(fd); return false; }

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return false;
        }

        if (listen(fd, 16) < 0) {
            close(fd);
            return false;
        }

        impl_->listen_fd_ = fd;
        impl_->running_.store(true);

        // 启动监听线程
        impl_->listener_thread_ = std::make_unique<std::thread>([this]() {
            struct pollfd pfd;
            pfd.fd = impl_->listen_fd_;
            pfd.events = POLLIN;

            while (impl_->running_.load()) {
                int ret = poll(&pfd, 1, 500);
                if (ret < 0) break;
                if (ret == 0) continue;

                int client_fd = accept(impl_->listen_fd_, nullptr, nullptr);
                if (client_fd < 0) continue;

                // 读取注册/注销消息
                // 格式: [flag(1)] [name_size(4)] [name] [socket_path_size(4)] [socket_path]
                // flag: 0=注册, 1=注销
                uint8_t flag = 0;
                if (::read(client_fd, &flag, 1) != 1) {
                    close(client_fd);
                    continue;
                }

                uint32_t name_size = 0;
                if (::read(client_fd, &name_size, 4) != 4) {
                    close(client_fd);
                    continue;
                }
                std::string name(name_size, '\0');
                if (name_size > 0)
                    ::read(client_fd, name.data(), name_size);

                uint32_t path_size = 0;
                if (::read(client_fd, &path_size, 4) != 4) {
                    close(client_fd);
                    continue;
                }
                std::string path(path_size, '\0');
                if (path_size > 0)
                    ::read(client_fd, path.data(), path_size);

                close(client_fd);

                std::lock_guard<std::mutex> lock(impl_->mutex_);

                if (flag == 0) {
                    // 注册
                    ProcessInfo info;
                    info.name = name;
                    info.socket_path = path;
                    info.alive = true;
                    impl_->processes_[name] = info;
                } else {
                    // 注销
                    impl_->processes_.erase(name);
                }

                if (impl_->status_cb_) {
                    auto it = impl_->processes_.find(name);
                    if (it != impl_->processes_.end())
                        impl_->status_cb_(it->second);
                }
            }
        });
    }

    return true;
}

void ServiceDiscovery::stop()
{
    impl_->running_.store(false);

    if (impl_->listener_thread_ && impl_->listener_thread_->joinable()) {
        // 唤醒监听线程
        int wake_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (wake_fd >= 0) {
            if (is_launcher_) {
                struct sockaddr_un addr;
                if(unixSocketAddress(addr,impl_->discovery_socket_path_)) connect(wake_fd, (struct sockaddr*)&addr, sizeof(addr));
            }
            close(wake_fd);
        }
        impl_->listener_thread_->join();
        impl_->listener_thread_.reset();
    }

    if (impl_->listen_fd_ >= 0) {
        close(impl_->listen_fd_);
        impl_->listen_fd_ = -1;
    }

    if (is_launcher_ && !impl_->discovery_socket_path_.empty())
        unlink(impl_->discovery_socket_path_.c_str());

    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->processes_.clear();
}

bool ServiceDiscovery::registerSelf()
{
    // 向 Launcher 的服务发现套接字发送注册消息
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    if(!unixSocketAddress(addr,impl_->discovery_socket_path_)) { close(fd); return false; }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    // 格式: [flag=0(注册)] [name_size(4)] [name] [socket_path_size(4)] [socket_path]
    uint8_t flag = 0;
    ::write(fd, &flag, 1);

    uint32_t name_size = static_cast<uint32_t>(self_.name.size());
    ::write(fd, &name_size, 4);
    ::write(fd, self_.name.data(), self_.name.size());

    uint32_t path_size = static_cast<uint32_t>(self_.socket_path.size());
    ::write(fd, &path_size, 4);
    ::write(fd, self_.socket_path.data(), self_.socket_path.size());

    close(fd);
    self_.alive = true;
    return true;
}

void ServiceDiscovery::unregisterSelf()
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct sockaddr_un addr;
    if(!unixSocketAddress(addr,impl_->discovery_socket_path_)) { close(fd); return; }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return;
    }

    // 格式: [flag=1(注销)] [name_size(4)] [name] [socket_path_size(4)] [socket_path]
    uint8_t flag = 1;
    ::write(fd, &flag, 1);

    uint32_t name_size = static_cast<uint32_t>(self_.name.size());
    ::write(fd, &name_size, 4);
    ::write(fd, self_.name.data(), self_.name.size());

    uint32_t path_size = static_cast<uint32_t>(self_.socket_path.size());
    ::write(fd, &path_size, 4);
    ::write(fd, self_.socket_path.data(), self_.socket_path.size());

    close(fd);
    self_.alive = false;
}

std::vector<ProcessInfo> ServiceDiscovery::knownProcesses() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<ProcessInfo> result;
    for (const auto& [_, info] : impl_->processes_)
        result.push_back(info);
    return result;
}

ProcessInfo ServiceDiscovery::findProcess(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->processes_.find(name);
    if (it != impl_->processes_.end())
        return it->second;
    return {};
}

void ServiceDiscovery::setStatusCallback(ProcessStatusCallback cb)
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->status_cb_ = std::move(cb);
}

} // namespace vsgraysim::ipc
