/// @file ShmTransport.cpp
/// @brief 共享内存进程间传输实现。
///
/// 通信机制:
///   1. Unix Domain Socket 交换控制消息 (订阅注册、通知、小数据)
///   2. 当前所有载荷走带长度边界的 UDS 字节流，完整读写后才交付
///   3. POSIX Shared Memory / 跨进程 GPU 零拷贝仍为后续能力

#include "ShmTransport.h"
#include "IpcPaths.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cerrno>
#include <limits>
#include <fstream>

#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>


namespace vsgraysim::ipc {

namespace {

/// 最大载荷大小 (16 MB)
constexpr size_t kMaxPayloadSize = 16 * 1024 * 1024;

/// 最大同时连接数
constexpr int kMaxConnections = 16;

/// 确保目录存在
bool ensureDir(const char* path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode);
    return mkdir(path, 0755) == 0;
}

constexpr size_t kMaxTopicSize=4096;
constexpr int kIoTimeoutMs=1500;
using IoClock=std::chrono::steady_clock;
using IoDeadline=IoClock::time_point;
struct Socket {
    int fd=-1;
    explicit Socket(int value):fd(value) {}
    ~Socket(){if(fd>=0) ::close(fd);}
    Socket(const Socket&)=delete;
    Socket& operator=(const Socket&)=delete;
};
bool waitSocket(int fd,short events,IoDeadline deadline) {
    for(;;) {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-IoClock::now()).count();
        if(remaining<=0) return false;
        pollfd descriptor{fd,events,0};
        const int status=::poll(&descriptor,1,int(std::min<int64_t>(remaining,std::numeric_limits<int>::max())));
        if(status<0 && errno==EINTR) continue;
        return status>0 && (descriptor.revents&events)!=0;
    }
}
bool readExact(int fd,void* buffer,size_t bytes,IoDeadline deadline) {
    auto* output=static_cast<uint8_t*>(buffer);
    while(bytes) {
        if(IoClock::now()>=deadline) return false;
        const auto count=::recv(fd,output,bytes,MSG_DONTWAIT);
        if(count>0) {output+=count;bytes-=size_t(count);continue;}
        if(count==0) return false;
        if(errno==EINTR) continue;
        if((errno==EAGAIN || errno==EWOULDBLOCK) && waitSocket(fd,POLLIN,deadline)) continue;
        return false;
    }
    return true;
}
bool writeExact(int fd,const void* buffer,size_t bytes,IoDeadline deadline) {
    const auto* input=static_cast<const uint8_t*>(buffer);
    while(bytes) {
        if(IoClock::now()>=deadline) return false;
        const auto count=::send(fd,input,bytes,MSG_DONTWAIT|MSG_NOSIGNAL);
        if(count>0) {input+=count;bytes-=size_t(count);continue;}
        if(count==0) return false;
        if(errno==EINTR) continue;
        if((errno==EAGAIN || errno==EWOULDBLOCK) && waitSocket(fd,POLLOUT,deadline)) continue;
        return false;
    }
    return true;
}
void socketTimeouts(int fd) {
    timeval timeout{kIoTimeoutMs/1000,(kIoTimeoutMs%1000)*1000};
    ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    ::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
}
int newSocket() {
    const int fd=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(fd>=0) socketTimeouts(fd);
    return fd;
}
bool connectSocket(int fd,const std::string& path,IoDeadline deadline) {
    sockaddr_un address{};
    if(!unixSocketAddress(address,path)) return false;
    for(;;) {
        if(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0) return true;
        if(errno==EINTR) {if(IoClock::now()<deadline) continue; return false;}
        if(errno==EISCONN) return true;
        if(errno!=EINPROGRESS && errno!=EAGAIN && errno!=EWOULDBLOCK) return false;
        if(!waitSocket(fd,POLLOUT,deadline)) return false;
        int error=0; socklen_t size=sizeof(error);
        return ::getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0 && error==0;
    }
}

// Atomic local subscription manifests let publishers avoid sending full image
// products to assembly windows and unrelated equipment. Missing manifests keep
// compatibility with older peers; malformed or oversized manifests fail closed.
bool acceptsTopic(const std::string& socket,const std::string& topic) {
    std::ifstream input(socket+".topics",std::ios::binary|std::ios::ate);
    if(!input)return true;
    const auto size=input.tellg();if(size<4||size>1024*1024)return false;input.seekg(0);
    uint32_t count=0;input.read(reinterpret_cast<char*>(&count),4);if(count>4096)return false;
    for(uint32_t n=0;n<count;++n){uint32_t length=0;input.read(reinterpret_cast<char*>(&length),4);if(!input||!length||length>kMaxTopicSize)return false;std::string name(length,'\0');input.read(name.data(),length);if(!input)return false;if(name==topic)return true;}
    return false;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
ShmTransport::ShmTransport() = default;

ShmTransport::~ShmTransport()
{
    stop();
}

// ---------------------------------------------------------------------------
// ITransport
// ---------------------------------------------------------------------------
bool ShmTransport::init(const std::string& process_name)
{
    process_name_ = process_name;
    socket_directory_=ipcDirectory();
    socket_path_ = ipcSocketPath(socket_directory_,process_name);
    return !socket_path_.empty();
}

bool ShmTransport::start()
{
    if (running_.load())
        return true;

    // 确保目录存在
    if (socket_path_.empty() || !ensureDir(socket_directory_.c_str()))
        return false;
    {std::lock_guard lock(mutex_);if(!saveSubscriptionsLocked())return false;}

    // 清理可能残留的旧套接字
    unlink(socket_path_.c_str());

    // 创建 UDS 监听套接字
    listen_fd_ = newSocket();
    if (listen_fd_ < 0)
        return false;

    struct sockaddr_un addr;
    if(!unixSocketAddress(addr,socket_path_)) { close(listen_fd_); listen_fd_=-1; return false; }

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, kMaxConnections) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_.store(true);

    // 启动监听线程
    listener_thread_ = std::make_unique<std::thread>([this]() {
        listenerThread();
    });

    return true;
}

void ShmTransport::stop()
{
    running_.store(false);

    if (listener_thread_ && listener_thread_->joinable()) {
        // 连接自身套接字以唤醒 poll()
        int wake_fd = newSocket();
        if (wake_fd >= 0) {
            struct sockaddr_un addr;
            connectSocket(wake_fd,socket_path_,IoClock::now()+std::chrono::milliseconds(100));
            close(wake_fd);
        }
        listener_thread_->join();
        listener_thread_.reset();
    }

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }

    // 清理套接字文件
    if(!socket_path_.empty()) unlink(socket_path_.c_str());
    if(!socket_path_.empty()) unlink((socket_path_+".topics").c_str());

    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.clear();
    received_bytes_.clear();
    services_.clear();
    published_topics_.clear();
}

std::string ShmTransport::processName() const
{
    return process_name_;
}

// ---------------------------------------------------------------------------
// Publish / Subscribe
// ---------------------------------------------------------------------------
bool ShmTransport::publish(const std::string& topic,const void* data,size_t size) {
    if(!running_.load() || topic.empty() || topic.size()>kMaxTopicSize || size>kMaxPayloadSize || (size && !data)) return false;
    const int64_t now=std::chrono::duration_cast<std::chrono::milliseconds>(IoClock::now().time_since_epoch()).count();
    bool refresh=false;
    {std::lock_guard<std::mutex> lock(mutex_);refresh=known_processes_.empty() || now-last_scan_ms_>=200;}
    if(refresh) refreshKnownProcesses();
    std::map<std::string,std::string> targets;
    {std::lock_guard<std::mutex> lock(mutex_);targets=known_processes_;}
    std::vector<uint8_t> message; message.reserve(9+topic.size()+size);
    message.push_back(0);
    auto append=[&](const void* bytes,size_t count){if(count){auto* start=static_cast<const uint8_t*>(bytes);message.insert(message.end(),start,start+count);}};
    const uint32_t topicSize=uint32_t(topic.size()),payloadSize=uint32_t(size);
    append(&topicSize,4);append(topic.data(),topic.size());append(&payloadSize,4);append(data,size);
    bool complete=true;
    for(const auto& [name,path]:targets) {
        if(name==process_name_) continue;
        if(!acceptsTopic(path,topic))continue;
        Socket peer(newSocket());
        const auto deadline=IoClock::now()+std::chrono::milliseconds(kIoTimeoutMs);
        if(peer.fd<0 || !connectSocket(peer.fd,path,deadline) || !writeExact(peer.fd,message.data(),message.size(),deadline)) complete=false;
    }
    return complete;
}

bool ShmTransport::subscribe(const std::string& topic, SubscribeCallback cb)
{
    if(topic.empty() || topic.size()>kMaxTopicSize || !cb) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[topic].push_back(std::move(cb));
    if(running_&&!saveSubscriptionsLocked()){auto& callbacks=subscribers_[topic];callbacks.pop_back();if(callbacks.empty())subscribers_.erase(topic);return false;}
    return true;
}

void ShmTransport::unsubscribe(const std::string& topic)
{
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(topic);
    if(running_&&!saveSubscriptionsLocked())std::fprintf(stderr,"ShmTransport: subscription manifest update failed\n");
}

bool ShmTransport::saveSubscriptionsLocked(){
    size_t size=4;for(const auto& [topic,callbacks]:subscribers_)size+=4+topic.size();if(subscribers_.size()>4096||size>1024*1024)return false;
    const auto path=socket_path_+".topics",pending=path+".pending";std::ofstream output(pending,std::ios::binary|std::ios::trunc);const uint32_t count=uint32_t(subscribers_.size());output.write(reinterpret_cast<const char*>(&count),4);
    for(const auto& [topic,callbacks]:subscribers_){const uint32_t length=uint32_t(topic.size());output.write(reinterpret_cast<const char*>(&length),4);output.write(topic.data(),topic.size());}output.close();if(!output){::unlink(pending.c_str());return false;}if(::rename(pending.c_str(),path.c_str())!=0){::unlink(pending.c_str());return false;}return true;
}
uint64_t ShmTransport::receivedBytes(const std::string& topic) const {std::lock_guard lock(mutex_);auto found=received_bytes_.find(topic);return found==received_bytes_.end()?0:found->second;}

// ---------------------------------------------------------------------------
// Request / Reply
// ---------------------------------------------------------------------------
bool ShmTransport::serve(const std::string& service, RequestHandler handler)
{
    if(service.empty() || service.size()>kMaxTopicSize || !handler) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    services_[service] = std::move(handler);
    return true;
}

bool ShmTransport::request(const std::string& service,const void* requestData,size_t requestSize,
                           std::vector<uint8_t>& response,int timeoutMs) {
    response.clear();
    if(!running_.load() || service.empty() || service.size()>kMaxTopicSize || requestSize>kMaxPayloadSize || (requestSize && !requestData) || timeoutMs<=0) return false;
    RequestHandler local;
    {std::lock_guard<std::mutex> lock(mutex_);auto found=services_.find(service);if(found!=services_.end()) local=found->second;}
    if(local) {
        try {return local(requestData,requestSize,response) && response.size()<=kMaxPayloadSize;}
        catch(...) {response.clear();return false;}
    }
    refreshKnownProcesses();
    std::map<std::string,std::string> targets;
    {std::lock_guard<std::mutex> lock(mutex_);targets=known_processes_;}
    const auto deadline=IoClock::now()+std::chrono::milliseconds(timeoutMs);
    const uint8_t flag=1; const uint32_t serviceSize=uint32_t(service.size()),payloadSize=uint32_t(requestSize);
    for(const auto& [name,path]:targets) {
        if(name==process_name_) continue;
        if(IoClock::now()>=deadline) break;
        Socket peer(newSocket());
        if(peer.fd<0 || !connectSocket(peer.fd,path,deadline)) continue;
        if(!writeExact(peer.fd,&flag,1,deadline) || !writeExact(peer.fd,&serviceSize,4,deadline) ||
           !writeExact(peer.fd,service.data(),service.size(),deadline) || !writeExact(peer.fd,&payloadSize,4,deadline) ||
           !writeExact(peer.fd,requestData,requestSize,deadline)) continue;
        uint32_t responseSize=0;
        if(!readExact(peer.fd,&responseSize,4,deadline) || responseSize>kMaxPayloadSize) continue;
        std::vector<uint8_t> received(responseSize);
        if(!readExact(peer.fd,received.data(),received.size(),deadline)) continue;
        response=std::move(received);return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 服务发现 (套接字目录扫描)
// ---------------------------------------------------------------------------
void ShmTransport::setKnownProcess(const std::string& name,
                                   const std::string& socket_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sockaddr_un address{};
    if(unixSocketAddress(address,socket_path)) known_processes_[name] = socket_path;
}

size_t ShmTransport::refreshKnownProcesses()
{
    std::lock_guard<std::mutex> lock(mutex_);

    last_scan_ms_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();

    DIR* dir = opendir(socket_directory_.c_str());
    if (!dir) return known_processes_.size();

    while (auto* ent = readdir(dir)) {
        std::string fn = ent->d_name;
        const std::string suffix = ".sock";
        if (fn.size() <= suffix.size() ||
            fn.compare(fn.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        std::string name = fn.substr(0, fn.size() - suffix.size());
        if (name == process_name_)
            continue;   // 跳过自身
        const auto path=ipcSocketPath(socket_directory_,name);
        if(!path.empty()) known_processes_[name] = path;
    }
    closedir(dir);
    return known_processes_.size();
}

// ---------------------------------------------------------------------------
// 内部: 监听线程
// ---------------------------------------------------------------------------
void ShmTransport::listenerThread()
{
    struct pollfd pfd;
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;

    while (running_.load()) {
        int ret = poll(&pfd, 1, 500); // 500ms 超时, 定期检查 running_
        if (ret < 0) {if(errno==EINTR) continue; break;}
        if (ret == 0) continue; // 超时

        int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) continue;

        ::fcntl(client_fd,F_SETFD,FD_CLOEXEC); socketTimeouts(client_fd);
        try {handleConnection(client_fd);}
        catch(...) {std::fprintf(stderr,"ShmTransport: incoming frame processing failed\n");}
    }
}

void ShmTransport::handleConnection(int clientFd) {
    Socket connection(clientFd);
    const auto deadline=IoClock::now()+std::chrono::milliseconds(kIoTimeoutMs);
    uint8_t flag=0; uint32_t nameSize=0,payloadSize=0;
    if(!readExact(clientFd,&flag,1,deadline) || flag>1 || !readExact(clientFd,&nameSize,4,deadline) || !nameSize || nameSize>kMaxTopicSize) return;
    std::string name(nameSize,'\0');
    if(!readExact(clientFd,name.data(),name.size(),deadline) || !readExact(clientFd,&payloadSize,4,deadline) || payloadSize>kMaxPayloadSize) return;
    std::vector<uint8_t> payload(payloadSize);
    if(!readExact(clientFd,payload.data(),payload.size(),deadline)) return;
    if(flag==0) {
        std::vector<SubscribeCallback> callbacks;
        {std::lock_guard<std::mutex> lock(mutex_);if(received_bytes_.size()<4096||received_bytes_.count(name))received_bytes_[name]+=payloadSize;auto found=subscribers_.find(name);if(found!=subscribers_.end()) callbacks=found->second;}
        for(const auto& callback:callbacks) if(callback) {
            try {callback(payload.data(),payload.size());}
            catch(...) {std::fprintf(stderr,"ShmTransport: subscriber callback failed\n");}
        }
    } else {
        RequestHandler handler;
        {std::lock_guard<std::mutex> lock(mutex_);auto found=services_.find(name);if(found!=services_.end()) handler=found->second;}
        if(!handler) return;
        std::vector<uint8_t> response;
        try {handler(payload.data(),payload.size(),response);}
        catch(...) {std::fprintf(stderr,"ShmTransport: service callback failed\n");return;}
        if(response.size()>kMaxPayloadSize) return;
        const uint32_t responseSize=uint32_t(response.size());
        // Give callback processing its own bounded response-write interval.
        const auto responseDeadline=IoClock::now()+std::chrono::milliseconds(kIoTimeoutMs);
        if(!writeExact(clientFd,&responseSize,4,responseDeadline)) return;
        writeExact(clientFd,response.data(),response.size(),responseDeadline);
    }
}

} // namespace vsgraysim::ipc
