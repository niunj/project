/// @file AlgoFactory.cpp
/// @brief 算法工厂实现。

#include "AlgoFactory.h"

namespace vsgraysim::algo {

void AlgoFactory::registerCreator(const std::string& category,
                                   const std::string& name,
                                   CreatorFunc creator)
{
    registry()[category][name] = std::move(creator);
}

std::unique_ptr<IAlgoBase> AlgoFactory::create(
    const std::string& category,
    const std::string& name,
    const AlgoConfig& cfg)
{
    auto& reg = registry();
    auto cat_it = reg.find(category);
    if (cat_it == reg.end())
        return nullptr;

    auto algo_it = cat_it->second.find(name);
    if (algo_it == cat_it->second.end())
        return nullptr;

    auto instance = algo_it->second();
    if (instance && !cfg.params.empty()) {
        // 使用配置初始化 (具体算法子类自行处理配置)
    }
    return instance;
}

std::vector<std::string> AlgoFactory::listAlgorithms(const std::string& category)
{
    auto& reg = registry();
    auto cat_it = reg.find(category);
    if (cat_it == reg.end())
        return {};

    std::vector<std::string> result;
    for (const auto& [name, _] : cat_it->second)
        result.push_back(name);
    return result;
}

void AlgoFactory::clear()
{
    registry().clear();
}

std::unordered_map<std::string,
    std::unordered_map<std::string, AlgoFactory::CreatorFunc>>&
AlgoFactory::registry()
{
    static std::unordered_map<std::string,
        std::unordered_map<std::string, CreatorFunc>> reg;
    return reg;
}

} // namespace vsgraysim::algo