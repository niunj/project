#pragma once

#include <string>

namespace vsgraysim {
struct SceneProduct;
bool writeSceneUsd(const SceneProduct& scene, const std::string& path);
}
