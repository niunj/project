/// @file AlgoConfig.cpp
/// @brief 算法配置实现。

#include "AlgoConfig.h"

#include <nlohmann/json.hpp>
#include <sstream>

namespace vsgraysim::algo {

void AlgoConfig::setParam(const std::string& key, const std::string& value)
{
    params[key] = value;
}

bool AlgoConfig::fromJson(const std::string& json_str)
{
    try {
        auto j = nlohmann::json::parse(json_str);
        if (j.contains("category"))
            category = j["category"].get<std::string>();
        if (j.contains("algorithm"))
            algorithm_name = j["algorithm"].get<std::string>();
        if (j.contains("params")) {
            for (auto& [key, val] : j["params"].items())
                params[key] = val.is_string() ? val.get<std::string>() : val.dump();
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string AlgoConfig::toJson() const
{
    nlohmann::json j;
    j["category"] = category;
    j["algorithm"] = algorithm_name;
    j["params"] = nlohmann::json::object();
    for (auto& [k, v] : params)
        j["params"][k] = v;
    return j.dump(2);
}

// 模板特化
template <>
int AlgoConfig::getParam<int>(const std::string& key, const int& default_val) const
{
    auto it = params.find(key);
    if (it == params.end()) return default_val;
    try { return std::stoi(it->second); } catch (...) { return default_val; }
}

template <>
float AlgoConfig::getParam<float>(const std::string& key, const float& default_val) const
{
    auto it = params.find(key);
    if (it == params.end()) return default_val;
    try { return std::stof(it->second); } catch (...) { return default_val; }
}

template <>
double AlgoConfig::getParam<double>(const std::string& key, const double& default_val) const
{
    auto it = params.find(key);
    if (it == params.end()) return default_val;
    try { return std::stod(it->second); } catch (...) { return default_val; }
}

template <>
std::string AlgoConfig::getParam<std::string>(const std::string& key, const std::string& default_val) const
{
    auto it = params.find(key);
    if (it == params.end()) return default_val;
    return it->second;
}

template <>
bool AlgoConfig::getParam<bool>(const std::string& key, const bool& default_val) const
{
    auto it = params.find(key);
    if (it == params.end()) return default_val;
    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    if (v == "true" || v == "1" || v == "yes") return true;
    if (v == "false" || v == "0" || v == "no") return false;
    return default_val;
}

} // namespace vsgraysim::algo