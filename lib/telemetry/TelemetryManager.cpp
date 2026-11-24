/// @file TelemetryManager.cpp
/// @brief 进程遥测管理器实现。

#include "TelemetryManager.h"

#include "ServiceDiscovery.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace vsgraysim::telemetry {

// ===========================================================================
// 内部: ServiceDiscovery 客户端包装
// ===========================================================================
class TelemetryManager::ServiceDiscoveryClient {
public:
    ServiceDiscoveryClient() = default;
    ~ServiceDiscoveryClient() { stop(); }

    bool init(const std::string& name, const std::string& sock_path,
              const std::string& discovery_path)
    {
        discovery_path_ = discovery_path;
        name_ = name;
        socket_path_ = sock_path;
        return true;
    }

    bool start()
    {
        // 向 Launcher 的 discovery socket 注册
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, discovery_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return false;
        }

        // 注册消息: flag=0 | name_size | name | socket_path_size | socket_path
        uint8_t flag = 0;
        uint32_t name_sz = uint32_t(name_.size());
        uint32_t path_sz = uint32_t(socket_path_.size());

        struct iovec iov[5];
        iov[0] = {&flag, 1};
        iov[1] = {&name_sz, 4};
        iov[2] = {const_cast<char*>(name_.data()), name_sz};
        iov[3] = {&path_sz, 4};
        iov[4] = {const_cast<char*>(socket_path_.data()), path_sz};

        struct msghdr msg = {};
        msg.msg_iov = iov;
        msg.msg_iovlen = 5;

        ssize_t sent = sendmsg(fd, &msg, 0);
        close(fd);
        return sent > 0;
    }

    void stop()
    {
        if (socket_path_.empty()) return;
        // 发送注销消息
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, discovery_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            uint8_t flag = 1;  // 注销
            uint32_t name_sz = uint32_t(name_.size());
            uint32_t path_sz = uint32_t(socket_path_.size());

            struct iovec iov[5];
            iov[0] = {&flag, 1};
            iov[1] = {&name_sz, 4};
            iov[2] = {const_cast<char*>(name_.data()), name_sz};
            iov[3] = {&path_sz, 4};
            iov[4] = {const_cast<char*>(socket_path_.data()), path_sz};

            struct msghdr msg = {};
            msg.msg_iov = iov;
            msg.msg_iovlen = 5;
            sendmsg(fd, &msg, 0);
        }
        close(fd);
    }

private:
    std::string name_;
    std::string socket_path_;
    std::string discovery_path_;
};

// ===========================================================================
// TelemetryManager
// ===========================================================================
TelemetryManager::TelemetryManager()
    : start_time_(std::chrono::steady_clock::now())
{
}

TelemetryManager::~TelemetryManager()
{
    shutdown();
}

bool TelemetryManager::init(const std::string& process_name,
                            const std::string& discovery_path)
{
    if (initialized_.exchange(true)) return false;

    process_name_ = process_name;
    pid_ = uint32_t(::getpid());
    discovery_path_ = discovery_path;

    // 构造遥测 UDS 路径
    telemetry_socket_path_ = "/tmp/vsgraysim/telemetry_" + process_name
                             + "_" + std::to_string(pid_) + ".sock";

    // 确保目录存在
    struct stat st;
    if (stat("/tmp/vsgraysim", &st) < 0)
        mkdir("/tmp/vsgraysim", 0755);

    // 创建 UDS 服务器
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        initialized_ = false;
        return false;
    }

    // 删除旧套接字
    unlink(telemetry_socket_path_.c_str());

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, telemetry_socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        initialized_ = false;
        return false;
    }

    // 设置非阻塞
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    if (listen(server_fd_, 4) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        initialized_ = false;
        return false;
    }

    // 注册到 ServiceDiscovery
    discovery_ = std::make_unique<ServiceDiscoveryClient>();
    if (!discovery_->init(process_name_, telemetry_socket_path_, discovery_path_)) {
        close(server_fd_);
        server_fd_ = -1;
        initialized_ = false;
        return false;
    }

    if (!discovery_->start()) {
        // 注册失败不算致命错误 (Launcher 可能还没启动)
        // 继续运行, 健康上报会通过 UDS 服务器进行
    }

    // 启动服务器线程
    running_ = true;
    server_thread_ = std::thread(&TelemetryManager::serverThread, this);

    return true;
}

void TelemetryManager::shutdown()
{
    if (!initialized_.exchange(false)) return;

    running_ = false;

    // 关闭服务器套接字 (唤醒监听线程)
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    // 等待服务器线程退出
    if (server_thread_.joinable())
        server_thread_.join();

    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int fd : client_fds_) {
            if (fd >= 0) close(fd);
        }
        client_fds_.clear();
    }

    // 注销服务发现
    if (discovery_) {
        discovery_->stop();
        discovery_.reset();
    }

    // 删除套接字文件
    if (!telemetry_socket_path_.empty())
        unlink(telemetry_socket_path_.c_str());

    // 更新自定义状态
    {
        std::lock_guard<std::mutex> lock(health_mutex_);
        custom_status_ = "stopped";
    }
}

// ---- 日志 API ----
void TelemetryManager::log(LogLevel level, const std::string& module,
                           const std::string& message)
{
    if (!initialized_) return;
    sendLog(level, module, message);
}

void TelemetryManager::logInfo(const std::string& module, const std::string& msg)
{
    log(LogLevel::Info, module, msg);
}

void TelemetryManager::logWarn(const std::string& module, const std::string& msg)
{
    log(LogLevel::Warning, module, msg);
}

void TelemetryManager::logError(const std::string& module, const std::string& msg)
{
    log(LogLevel::Error, module, msg);
}

void TelemetryManager::logFatal(const std::string& module, const std::string& msg)
{
    log(LogLevel::Fatal, module, msg);
}

// ---- 故障 API ----
void TelemetryManager::reportFault(const std::string& module, int code,
                                   const std::string& description)
{
    if (!initialized_) return;
    std::ostringstream oss;
    oss << "{\"module\":\"" << module
        << "\",\"code\":" << code
        << ",\"description\":\"" << description << "\"}";
    sendTelemetry(TelemetryMsgType::Fault, oss.str());
}

void TelemetryManager::clearFault(const std::string& module, int code)
{
    if (!initialized_) return;
    std::ostringstream oss;
    oss << "{\"module\":\"" << module
        << "\",\"code\":" << code
        << ",\"cleared\":true}";
    sendTelemetry(TelemetryMsgType::Fault, oss.str());
}

// ---- 性能指标 API ----
void TelemetryManager::reportMetric(const std::string& name, double value,
                                    const std::string& unit)
{
    if (!initialized_) return;
    std::ostringstream oss;
    oss << "{\"name\":\"" << name
        << "\",\"value\":" << value
        << ",\"unit\":\"" << unit << "\"}";
    sendTelemetry(TelemetryMsgType::Metric, oss.str());
}

// ---- 健康状态更新 ----
void TelemetryManager::setFps(double fps)
{
    std::lock_guard<std::mutex> lock(health_mutex_);
    fps_ = fps;
}

void TelemetryManager::setCpuPercent(double pct)
{
    std::lock_guard<std::mutex> lock(health_mutex_);
    cpu_pct_ = pct;
}

void TelemetryManager::setMemBytes(size_t bytes)
{
    std::lock_guard<std::mutex> lock(health_mutex_);
    mem_bytes_ = bytes;
}

void TelemetryManager::setCustomStatus(const std::string& status)
{
    std::lock_guard<std::mutex> lock(health_mutex_);
    custom_status_ = status;
}

void TelemetryManager::setConnectCallback(TelemetryConnectCallback cb)
{
    connect_cb_ = std::move(cb);
}

// ===========================================================================
// 内部实现
// ===========================================================================

void TelemetryManager::serverThread()
{
    auto last_health = std::chrono::steady_clock::now();

    while (running_) {
        // 接受新连接 (非阻塞)
        struct sockaddr_un client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd_,
                               (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd >= 0) {
            // 新客户端连接 (Launcher)
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                client_fds_.push_back(client_fd);
            }
            // 立即发送当前健康状态
            sendHealthReport();
            if (connect_cb_)
                connect_cb_();
        }

        // 健康定时上报
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_health).count();
        if (elapsed >= health_interval_sec_) {
            sendHealthReport();
            last_health = now;
        }

        // 短暂休眠
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void TelemetryManager::sendHealthReport()
{
    if (!initialized_) return;

    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time_).count();

    double fps, cpu;
    size_t mem;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(health_mutex_);
        fps = fps_;
        cpu = cpu_pct_;
        mem = mem_bytes_;
        status = custom_status_.empty() ? "running" : custom_status_;
    }

    std::ostringstream oss;
    oss << "{"
        << "\"name\":\"" << process_name_ << "\","
        << "\"pid\":" << pid_ << ","
        << "\"status\":\"" << status << "\","
        << "\"fps\":" << fps << ","
        << "\"cpu\":" << cpu << ","
        << "\"mem\":" << mem << ","
        << "\"uptime\":" << uptime << ","
        << "\"ts\":" << now_ms
        << "}";

    sendTelemetry(TelemetryMsgType::Health, oss.str());
}

void TelemetryManager::sendLog(LogLevel level, const std::string& module,
                               const std::string& message)
{
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    static const char* kLevelNames[] = {
        "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    const char* lv = (int(level) >= 0 && int(level) < 5)
                     ? kLevelNames[int(level)] : "UNKNOWN";

    std::ostringstream oss;
    oss << "{"
        << "\"name\":\"" << process_name_ << "\","
        << "\"level\":\"" << lv << "\","
        << "\"module\":\"" << module << "\","
        << "\"msg\":\"" << message << "\","
        << "\"ts\":" << now_ms
        << "}";

    sendTelemetry(TelemetryMsgType::Log, oss.str());
}

void TelemetryManager::sendTelemetry(TelemetryMsgType type,
                                     const std::string& json_payload)
{
    TelemetryPacket pkt{type, json_payload};
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = client_fds_.begin(); it != client_fds_.end(); ) {
        int fd = *it;
        sendPacket(fd, pkt);
        ++it;
    }
}

void TelemetryManager::sendPacket(int fd, const TelemetryPacket& pkt)
{
    uint8_t type_byte = static_cast<uint8_t>(pkt.type);
    uint32_t payload_size = uint32_t(pkt.payload.size());

    struct iovec iov[3];
    iov[0] = {&type_byte, 1};
    iov[1] = {&payload_size, 4};
    iov[2] = {const_cast<char*>(pkt.payload.data()), payload_size};

    struct msghdr msg = {};
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    ssize_t sent = sendmsg(fd, &msg, MSG_NOSIGNAL);
    if (sent < 0) {
        // 连接断开, 标记移除
        close(fd);
        fd = -1;
    }
}

// ---- 系统工具实现 ----
size_t TelemetryManager::readMemoryBytes()
{
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            size_t kb = 0;
            std::istringstream(line.substr(6)) >> kb;
            return kb * 1024;
        }
    }
    return 0;
}

double TelemetryManager::readCpuPercent(bool reset)
{
    static long prev_utime = 0, prev_stime = 0;
    if (reset) { prev_utime = 0; prev_stime = 0; return 0.0; }

    std::ifstream f("/proc/self/stat");
    std::string line;
    std::getline(f, line);
    f.close();

    std::istringstream iss(line);
    std::string tok;
    for (int i = 0; i < 13; ++i) iss >> tok;
    long utime = 0, stime = 0;
    iss >> utime >> stime;

    if (prev_utime == 0) {
        prev_utime = utime; prev_stime = stime;
        return 0.0;
    }
    long dtotal = (utime + stime) - (prev_utime + prev_stime);
    prev_utime = utime; prev_stime = stime;
    // 假设 sysconf(_SC_CLK_TCK) = 100, 并且采样间隔 ~2s
    // 粗略估算: dtotal / 100 ticks/sec / 2 sec = dtotal / 200
    double cpu = double(dtotal) / 200.0 * 100.0;
    return std::min(cpu, 100.0);
}

} // namespace vsgraysim::telemetry