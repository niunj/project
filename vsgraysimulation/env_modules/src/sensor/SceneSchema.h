#pragma once
/// @file SceneSchema.h —— 场景/传感器 JSON 轻量 schema 校验 (P1 工程化)。
///
/// 不使用完整 JSON-Schema 库 (零依赖, 随 nlohmann 单头); 在解析点 (SceneSource.cpp)
/// 对顶层结构 + 关键字段做"存在性/类型/范围"检查, 发现问题即 spdlog 警告
/// (不 error, 保持向后兼容: 旧场景文件缺字段时只提醒, 仍走默认值回退)。
///
/// 校验维度:
///   - 顶层必需键 (scenario_name 缺失时警告)
///   - 传感器条目字段类型 (sensor_id/name/band/fov 等, 与 SensorDesc 对齐)
///   - 数值范围 (fov_deg/netd_mk/integration_ms/rate_hz 的物理合理区间)
///
/// 新增场景字段时同步扩展 validateScenario()。
#ifndef VSGRAYSIM_ENV_SCENE_SCHEMA_H
#define VSGRAYSIM_ENV_SCENE_SCHEMA_H

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace rockyenv {
namespace scene_schema {

/// 单字段校验结果。
struct Issue {
    std::string path;    ///< JSON 路径 (如 "sensors.sensor_list[2].fov_deg")
    std::string message; ///< 描述
};

/// 收集器: 收集所有 issue (一次解析报全部问题, 而非遇首个即停)。
class Report {
public:
    void warn(const std::string& path, const std::string& msg) {
        issues_.push_back({path, msg});
    }
    int count() const { return int(issues_.size()); }
    /// 打印全部 issue (stderr, [schema] 前缀便于 grep; 不依赖 spdlog,
    /// 与 SceneSource.cpp 既有 std::printf 风格一致)。
    void flush(const char* context) const {
        for (const auto& i : issues_)
            std::fprintf(stderr, "[schema] %s: %s — %s\n",
                         context, i.path.c_str(), i.message.c_str());
        if (!issues_.empty())
            std::fprintf(stderr, "[schema] %s: 共 %d 处字段问题 (已回退默认值, 建议修正)\n",
                         context, int(issues_.size()));
    }

private:
    std::vector<Issue> issues_;
};

/// 数值字段存在性 + 类型 + 范围检查。
inline void checkNumber(const nlohmann::json& j, const char* key,
                        double lo, double hi, Report& rep, const std::string& path)
{
    if (!j.contains(key)) return;                 // 缺失 → 走默认值, 不报警
    const auto& v = j[key];
    if (!v.is_number()) {
        rep.warn(path + "." + key, "类型应为数值, 实际 " + std::string(v.type_name()));
        return;
    }
    const double d = v.get<double>();
    if (d < lo || d > hi || !std::isfinite(d))
        rep.warn(path + "." + key,
                 "值 " + std::to_string(d) + " 超出物理合理范围 [" +
                 std::to_string(lo) + ", " + std::to_string(hi) + "]");
}

/// 字符串字段存在性 + 类型检查。
inline void checkString(const nlohmann::json& j, const char* key,
                        Report& rep, const std::string& path)
{
    if (!j.contains(key)) return;
    if (!j[key].is_string())
        rep.warn(path + "." + key, "类型应为字符串, 实际 " + std::string(j[key].type_name()));
}

/// 校验场景 JSON 顶层结构 + 传感器列表 (与 SceneData/SensorDesc 对齐)。
/// 调用点: SceneSource.cpp 解析后、填充 SceneData 前。返回 issue 数 (0 = 通过)。
inline int validateScenario(const nlohmann::json& j, const char* context = "scenario")
{
    Report rep;

    // ---- 顶层 ----
    if (!j.contains("scenario_name"))
        rep.warn("(top)", "缺顶层键 scenario_name (场景名将取空)");

    // ---- geo_anchor ----
    if (j.contains("geo_anchor")) {
        const auto& g = j["geo_anchor"];
        if (g.is_object()) {
            checkNumber(g, "lat", -90.0, 90.0, rep, "geo_anchor");
            checkNumber(g, "lon", -180.0, 180.0, rep, "geo_anchor");
            checkNumber(g, "alt", -500.0, 100000.0, rep, "geo_anchor");
        } else {
            rep.warn("geo_anchor", "类型应为对象, 实际 " + std::string(g.type_name()));
        }
    }

    // ---- sensors.sensor_list[] ----
    if (j.contains("sensors") && j["sensors"].is_object()
        && j["sensors"].contains("sensor_list")) {
        const auto& list = j["sensors"]["sensor_list"];
        if (list.is_array()) {
            int idx = 0;
            for (const auto& s : list) {
                const std::string p = "sensors.sensor_list[" + std::to_string(idx) + "]";
                if (!s.is_object()) {
                    rep.warn(p, "条目应为对象, 实际 " + std::string(s.type_name()));
                    ++idx; continue;
                }
                checkString(s, "name", rep, p);
                // 物理字段范围 (与 SensorProfile 默认值对齐)
                checkNumber(s, "fov_deg",        0.1,  180.0,   rep, p);
                checkNumber(s, "netd_mk",        0.0,  1000.0,  rep, p);
                checkNumber(s, "integration_ms", 0.0,  1000.0,  rep, p);
                checkNumber(s, "rate_hz",        0.01, 10000.0, rep, p);
                checkNumber(s, "cpi_pulses",     1.0,  4096.0,  rep, p);
                checkNumber(s, "range_m",        1.0,  1e7,     rep, p);
                // band 字段 (若给出) 应为已识别的字符串
                if (s.contains("band") && s["band"].is_string()) {
                    const std::string b = s["band"].get<std::string>();
                    static const char* kKnown[] = {"TV","VIS","SWIR","MWIR","LWIR",
                                                   "tv","swir","mwir","lwir"};
                    bool known = false;
                    for (auto* k : kKnown) if (b == k) { known = true; break; }
                    if (!known)
                        rep.warn(p + ".band", "未知波段 '" + b + "' (应为 tv|swir|mwir|lwir)");
                }
                ++idx;
            }
        } else {
            rep.warn("sensors.sensor_list", "类型应为数组, 实际 " + std::string(list.type_name()));
        }
    }

    // ---- atmosphere (若存在; 与 SceneDesignerWindow 导出格式对齐) ----
    if (j.contains("atmosphere") && j["atmosphere"].is_object()) {
        const auto& a = j["atmosphere"];
        checkNumber(a, "cloud_cover",  0.0, 1.0,     rep, "atmosphere");
        checkNumber(a, "cloud_base",   0.0, 20000.0, rep, "atmosphere");
        checkNumber(a, "rain",         0.0, 1.0,     rep, "atmosphere");
        checkNumber(a, "snow",         0.0, 1.0,     rep, "atmosphere");
        checkNumber(a, "visibility",   100.0, 80000.0, rep, "atmosphere");
        checkNumber(a, "wind_speed",   0.0, 80.0,    rep, "atmosphere");
        checkNumber(a, "wind_az",      0.0, 360.0,   rep, "atmosphere");
        checkNumber(a, "sun_el",       -90.0, 90.0,  rep, "atmosphere");
        checkNumber(a, "sun_az",       0.0, 360.0,   rep, "atmosphere");
        checkNumber(a, "wave_height",  0.0, 30.0,    rep, "atmosphere");
    }

    rep.flush(context);
    return rep.count();
}

}} // namespace rockyenv::scene_schema

#endif // VSGRAYSIM_ENV_SCENE_SCHEMA_H
