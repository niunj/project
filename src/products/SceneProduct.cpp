#include "vsgraysim/products/SceneProduct.h"

#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

#include "vsgraysim/project/ProjectSerializer.h"

namespace vsgraysim {

// ---- Transform ----

vsg::dmat4 Transform::matrix() const {
    vsg::dmat4 m = vsg::translate(translation);
    m = m * vsg::rotate(rotation);
    m = m * vsg::scale(scale);
    return m;
}

Transform Transform::fromMatrix(const vsg::dmat4& m) {
    Transform t;
    vsg::dvec3 translation;
    vsg::dquat rotation;
    vsg::dvec3 scale;
    vsg::decompose(m, translation, rotation, scale);
    t.translation = translation;
    t.rotation = rotation;
    t.scale = scale;
    return t;
}

// ---- Query helpers ----

const Entity* SceneProduct::findEntity(const std::string& entity_id) const {
    for (const auto& e : entities) {
        if (e.entity_id == entity_id) return &e;
    }
    return nullptr;
}

std::vector<const Entity*> SceneProduct::findEntitiesByTag(const std::string& tag) const {
    std::vector<const Entity*> result;
    for (const auto& e : entities) {
        auto it = std::find(e.tags.begin(), e.tags.end(), tag);
        if (it != e.tags.end()) result.push_back(&e);
    }
    return result;
}

std::vector<const Entity*> SceneProduct::findEntitiesByLayer(const std::string& layer) const {
    std::vector<const Entity*> result;
    for (const auto& e : entities) {
        auto it = std::find(e.layers.begin(), e.layers.end(), layer);
        if (it != e.layers.end()) result.push_back(&e);
    }
    return result;
}

const Prefab* SceneProduct::getPrefab(const std::string& prefab_ref) const {
    auto it = prefab_library.find(prefab_ref);
    if (it != prefab_library.end()) return &it->second;
    return nullptr;
}

// ---- Export stubs ----

bool SceneProduct::exportUSD(const std::string&) const {
    // TODO(M2): 通过 USD 桥接导出
    return false;
}

bool SceneProduct::exportGLTF(const std::string&) const {
    // TODO(M2): 通过 glTF 导出器实现
    return false;
}

// ---- Factory ----

std::shared_ptr<SceneProduct> SceneProduct::create(const std::string& name) {
    auto product = std::make_shared<SceneProduct>();
    product->name = name;
    product->prefab_library.clear();
    return product;
}

std::shared_ptr<SceneProduct> SceneProduct::load(const std::string& path) {
    ProjectSerializer serializer;
    return serializer.loadScene(path);
}

bool SceneProduct::save(const std::string& path) const {
    ProjectSerializer serializer;
    SerializeOptions opts;
    auto self = const_cast<SceneProduct*>(this);
    return serializer.saveScene(std::shared_ptr<SceneProduct>(self, [](SceneProduct*) {}), path, opts);
}

} // namespace vsgraysim