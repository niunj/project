/// @file FeatureRegistry.cpp
#include "FeatureRegistry.h"

#include "IViewportFeature.h"

#include <spdlog/spdlog.h>

namespace vsgraysim { namespace viewport3d {

FeatureRegistry& FeatureRegistry::instance()
{
    static FeatureRegistry reg;
    return reg;
}

void FeatureRegistry::registerFeature(const std::string& id, Factory fn)
{
    factories_[id] = std::move(fn);
}

bool FeatureRegistry::contains(const std::string& id) const
{
    return factories_.count(id) > 0;
}

std::unique_ptr<IViewportFeature> FeatureRegistry::create(const std::string& id) const
{
    const auto it = factories_.find(id);
    if (it == factories_.end()) {
        spdlog::warn("FeatureRegistry: unknown feature id '{}'", id);
        return {};
    }
    return it->second();
}

std::vector<std::string> FeatureRegistry::ids() const
{
    std::vector<std::string> out;
    out.reserve(factories_.size());
    for (const auto& kv : factories_) out.push_back(kv.first);
    return out;
}

}} // namespace vsgraysim::viewport3d
