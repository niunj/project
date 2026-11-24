#pragma once
/// @file AlgoConfig.h
/// @brief 算法配置数据结构。

#include <string>
#include <unordered_map>
#include <vector>

namespace vsgraysim::algo {

/// 算法配置
struct AlgoConfig {
    /// 算法类别 (radar/sar/infrared/simulation)
    std::string category;

    /// 算法名称 (如 cfar/mti/rd_imaging/ir_processor)
    std::string algorithm_name;

    /// 算法参数 (键值对)
    std::unordered_map<std::string, std::string> params;

    /// 获取参数 (带默认值)
    template <class T>
    T getParam(const std::string& key, const T& default_val) const;

    /// 设置参数
    void setParam(const std::string& key, const std::string& value);

    /// 从 JSON 字符串加载
    bool fromJson(const std::string& json_str);

    /// 导出为 JSON 字符串
    std::string toJson() const;
};

// 模板特化声明
template <>
int AlgoConfig::getParam<int>(const std::string& key, const int& default_val) const;
template <>
float AlgoConfig::getParam<float>(const std::string& key, const float& default_val) const;
template <>
double AlgoConfig::getParam<double>(const std::string& key, const double& default_val) const;
template <>
std::string AlgoConfig::getParam<std::string>(const std::string& key, const std::string& default_val) const;
template <>
bool AlgoConfig::getParam<bool>(const std::string& key, const bool& default_val) const;

} // namespace vsgraysim::algo