#include "EnvCommon.h"
#include <cstdio>
#include <filesystem>
#include <limits.h>
#include <unistd.h>

namespace rockyenv {
std::string resolveEnvShader(const std::string& name)
{
    namespace fs = std::filesystem;
    // 可执行文件目录
    char exe[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    fs::path base;
    if (n > 0) { exe[n] = '\0'; base = fs::path(exe).parent_path(); }
    const std::string rel = std::string("shaders/env/") + name;
    // Build presets may nest the executable below build/<preset>/gui.
    // Only a repository marker identifies the source shader tree.
    std::vector<fs::path> candidates;
    for(auto dir=base; !dir.empty() && dir!=dir.root_path(); dir=dir.parent_path()) {
        if(fs::exists(dir/"env_modules/CMakeLists.txt") && fs::exists(dir/"shaders/env")) {
            candidates.push_back(dir/rel);
            break;
        }
    }
    candidates.insert(candidates.end(),{base/rel,base.parent_path()/rel,
        base.parent_path()/"share"/rel,fs::path(rel)});
    for (auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) {
            // 诊断: shader 源文件在源码树/构建树存在多副本, 加载错位曾导致
            // "改了没生效"的调试假象 —— 打印最终解析路径以永远可查。
            std::fprintf(stderr, "[rocky_env] shader %s -> %s\n",
                         name.c_str(), c.string().c_str());
            return c.string();
        }
    }
    return rel;  // 兜底 (交给 ShaderStage::read 报错)
}

}
