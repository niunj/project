#pragma once
/// @file FeatureRegistry.h
/// @brief 特性静态注册表 (编译期插件机制)。
///
/// 用法 (新特性源文件末尾):
///     VSGVP_REGISTER_FEATURE(MyFeature)   // MyFeature 需定义 static kId
#ifndef VSGRAYSIM_VIEWPORT3D_FEATUREREGISTRY_H
#define VSGRAYSIM_VIEWPORT3D_FEATUREREGISTRY_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vsgraysim { namespace viewport3d {

class IViewportFeature;

class FeatureRegistry {
public:
    using Factory = std::function<std::unique_ptr<IViewportFeature>()>;

    static FeatureRegistry& instance();

    void registerFeature(const std::string& id, Factory fn);
    bool contains(const std::string& id) const;
    std::unique_ptr<IViewportFeature> create(const std::string& id) const;
    std::vector<std::string> ids() const;

private:
    std::map<std::string, Factory> factories_;
};

/// 静态注册器: 全局对象构造时写入注册表。
struct FeatureRegistrar {
    FeatureRegistrar(const std::string& id, FeatureRegistry::Factory fn) {
        FeatureRegistry::instance().registerFeature(id, std::move(fn));
    }
};

/// 特性注册宏: 要求特性类定义 `static constexpr const char* kId`。
#define VSGVP_REGISTER_FEATURE(ClassName)                                    \
    static const ::vsgraysim::viewport3d::FeatureRegistrar                   \
        s_vsgvp_registrar_##ClassName(ClassName::kId, []() {                 \
            return std::unique_ptr<::vsgraysim::viewport3d::IViewportFeature>(\
                new ClassName());                                            \
        })

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_FEATUREREGISTRY_H
