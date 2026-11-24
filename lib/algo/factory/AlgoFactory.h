#pragma once
/// @file AlgoFactory.h
/// @brief 算法工厂 — 按类型和名称创建算法实例。

#include "../IAlgoBase.h"
#include "AlgoConfig.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsgraysim::algo {

/// 算法工厂
///
/// 用法:
///   // 注册算法
///   AlgoFactory::registerCreator<CfarProcessorImpl>("radar", "ca_cfar");
///
///   // 创建算法
///   auto cfar = AlgoFactory::create("radar", "ca_cfar", config);
///
///   // 列出算法
///   auto list = AlgoFactory::listAlgorithms("radar");  // ["ca_cfar", "so_cfar", ...]
class AlgoFactory {
public:
    using CreatorFunc = std::function<std::unique_ptr<IAlgoBase>()>;

    /// 注册算法创建器
    static void registerCreator(const std::string& category,
                                const std::string& name,
                                CreatorFunc creator);

    /// 创建算法实例
    /// @param category 算法类别 (radar/sar/infrared/simulation)
    /// @param name     算法名称
    /// @param cfg      算法配置 (可选)
    static std::unique_ptr<IAlgoBase> create(
        const std::string& category,
        const std::string& name,
        const AlgoConfig& cfg = {});

    /// 列出某类别下所有已注册算法
    static std::vector<std::string> listAlgorithms(const std::string& category);

    /// 清除所有注册
    static void clear();

private:
    static std::unordered_map<std::string,
                std::unordered_map<std::string, CreatorFunc>>& registry();
};

/// 便利模板: 注册算法
template <class T>
struct AutoRegister {
    AutoRegister(const std::string& category, const std::string& name) {
        AlgoFactory::registerCreator(category, name,
            []() { return std::make_unique<T>(); });
    }
};

} // namespace vsgraysim::algo