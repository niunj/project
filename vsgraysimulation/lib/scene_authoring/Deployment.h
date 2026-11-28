#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace scene_deployment {
using Json = nlohmann::json;
inline constexpr auto format = "scene-deployment-v1";
inline constexpr auto maxResponseBytes = 64 * 1024;

// Portable boundary. Hosts build a context from public scene data, and
// translate validated actions through their own editor transaction API.
Json validate(const Json& plan);
Json parse(std::string_view response);
std::optional<Json> simplePlan(std::string_view instruction, const std::string& selected);
Json modelRequest(std::string_view model,std::string_view instruction,const Json& context);
Json modelPlan(std::string_view completion);
}
