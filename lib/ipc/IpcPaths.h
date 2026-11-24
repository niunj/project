#pragma once
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>

namespace vsgraysim::ipc {
/// Optional deployment/test namespace. Invalid overrides fail closed rather
/// than silently connecting to the default application's sockets.
inline std::string ipcDirectory() {
    const char* configured=std::getenv("VSGRAYSIM_IPC_DIRECTORY");
    if(!configured) return "/tmp/vsgraysim";
    const std::filesystem::path path(configured);
    if(path.empty() || !path.is_absolute()) return {};
    return path.lexically_normal().string();
}
inline bool unixSocketAddress(sockaddr_un& address,const std::string& path) {
    if(path.empty() || path.front()!='/' || path.size()>=sizeof(address.sun_path) || path.find('\0')!=std::string::npos) return false;
    address={}; address.sun_family=AF_UNIX;
    std::memcpy(address.sun_path,path.c_str(),path.size()+1); return true;
}
inline std::string ipcSocketPath(const std::string& directory,const std::string& name) {
    if(directory.empty() || name.empty() || name=="." || name==".." || name.find('/')!=std::string::npos) return {};
    const auto path=(std::filesystem::path(directory)/(name+".sock")).string();
    sockaddr_un address{}; return unixSocketAddress(address,path) ? path : std::string{};
}
}
