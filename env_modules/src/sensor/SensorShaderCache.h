#pragma once
#include <vsg/utils/ShaderCompiler.h>
#include <list>
#include <mutex>
#include <tuple>

namespace rockyenv::sensor {
// VSG resolves includes on read. Key the entire linked group by expanded
// source (including sensor defines), stage and entry point. Keep SPIR-V only;
// Vulkan modules, descriptors and command graphs remain owned by each device.
inline bool compileSensorStages(vsg::ShaderStages& stages) {
    using Key=std::vector<std::tuple<VkShaderStageFlagBits,std::string,std::string>>;
    using Code=std::vector<vsg::ShaderModule::SPIRV>;
    static std::list<std::pair<Key,Code>> cache;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    Key key;
    for(const auto& stage:stages) {
        if(!stage || !stage->module) return false;
        if(stage->module->hints || stage->module->source.empty())
            return vsg::ShaderCompiler::create()->compile(stages);
        key.emplace_back(stage->stage,stage->entryPointName,stage->module->source);
    }
    for(auto it=cache.begin();it!=cache.end();++it) if(it->first==key) {
        for(size_t i=0;i<stages.size();++i) stages[i]->module->code=it->second[i];
        cache.splice(cache.begin(),cache,it);
        return true;
    }
    auto compiler=vsg::ShaderCompiler::create();
    if(!compiler->supported() || !compiler->compile(stages)) return false;
    Code code;
    for(const auto& stage:stages) code.push_back(stage->module->code);
    cache.emplace_front(std::move(key),std::move(code));
    if(cache.size()>64) cache.pop_back();
    return true;
}
}
