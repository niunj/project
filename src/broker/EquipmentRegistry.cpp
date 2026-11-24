/// @file EquipmentRegistry.cpp
/// @brief 装备注册表实现。

#include "vsgraysim/broker/EquipmentInfo.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <set>

namespace vsgraysim {

namespace {
uint64_t nowMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
}
} // anonymous namespace

EquipmentRegistry& EquipmentRegistry::instance()
{
    static EquipmentRegistry reg;
    return reg;
}

void EquipmentRegistry::registerEquipment(const EquipmentInfo& info)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (registry_.count(info.equipment_id)) {
        spdlog::warn("EquipmentRegistry: equipment {} already registered, overwriting",
                     info.equipment_id);
    }
    registry_[info.equipment_id] = info;
    spdlog::info("EquipmentRegistry: registered '{}' (id={}, type={}, platform='{}')",
                 info.name, info.equipment_id,
                 sensorTypeName(info.type), info.platform_name);
}

void EquipmentRegistry::unregisterEquipment(uint32_t equipment_id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = registry_.find(equipment_id);
    if (it != registry_.end()) {
        spdlog::info("EquipmentRegistry: unregistered '{}' (id={})",
                     it->second.name, equipment_id);
        registry_.erase(it);
    }
}

void EquipmentRegistry::clear()
{
    std::lock_guard<std::mutex> lock(mtx_);
    registry_.clear();
    spdlog::info("EquipmentRegistry: cleared all registrations");
}

std::vector<EquipmentInfo> EquipmentRegistry::listOnline() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<EquipmentInfo> result;
    result.reserve(registry_.size());
    for (const auto& [id, info] : registry_) {
        result.push_back(info);
    }
    return result;
}

std::vector<EquipmentInfo> EquipmentRegistry::listByType(SensorType t) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<EquipmentInfo> result;
    for (const auto& [id, info] : registry_) {
        if (info.type == t) {
            result.push_back(info);
        }
    }
    return result;
}

std::optional<EquipmentInfo> EquipmentRegistry::findById(uint32_t id) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = registry_.find(id);
    if (it != registry_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<EquipmentInfo> EquipmentRegistry::findByName(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& [id, info] : registry_) {
        if (info.name == name) {
            return info;
        }
    }
    return std::nullopt;
}

void EquipmentRegistry::setPowered(uint32_t equipment_id, bool powered)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = registry_.find(equipment_id);
    if (it != registry_.end()) {
        it->second.powered = powered;
    }
}

void EquipmentRegistry::setActive(uint32_t equipment_id, bool active)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = registry_.find(equipment_id);
    if (it != registry_.end()) {
        it->second.active = active;
    }
}

void EquipmentRegistry::setSubscribed(uint32_t equipment_id, bool subscribed)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = registry_.find(equipment_id);
    if (it != registry_.end()) {
        it->second.subscribed = subscribed;
    }
}

void EquipmentRegistry::mergeRemote(const std::vector<EquipmentInfo>& list,
                                    const std::string& host)
{
    std::lock_guard<std::mutex> lock(mtx_);

    // 收集快照中的装备 ID, 用于移除已下线的旧镜像条目
    std::set<uint32_t> snapshot_ids;
    for (EquipmentInfo info : list) {
        snapshot_ids.insert(info.equipment_id);
        info.remote        = true;
        info.host          = host;
        info.last_seen_ms  = nowMs();
        // 本地已有条目则保留本地订阅状态 (镜像以远端为主)
        auto it = registry_.find(info.equipment_id);
        if (it != registry_.end() && it->second.remote)
            info.subscribed = it->second.subscribed;
        registry_[info.equipment_id] = std::move(info);
    }

    // 移除同 host 下不在快照中的旧条目
    for (auto it = registry_.begin(); it != registry_.end();) {
        if (it->second.remote && it->second.host == host &&
            !snapshot_ids.count(it->first))
            it = registry_.erase(it);
        else
            ++it;
    }
}

void EquipmentRegistry::clearRemote(const std::string& host)
{
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = registry_.begin(); it != registry_.end();) {
        if (it->second.remote && it->second.host == host)
            it = registry_.erase(it);
        else
            ++it;
    }
}

size_t EquipmentRegistry::sweepStale(uint64_t timeout_ms)
{
    const uint64_t now = nowMs();
    std::lock_guard<std::mutex> lock(mtx_);
    size_t removed = 0;
    for (auto it = registry_.begin(); it != registry_.end();) {
        if (it->second.remote && now > it->second.last_seen_ms &&
            now - it->second.last_seen_ms > timeout_ms) {
            it = registry_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

size_t EquipmentRegistry::count() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return registry_.size();
}

size_t EquipmentRegistry::countByType(SensorType t) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    size_t n = 0;
    for (const auto& [id, info] : registry_) {
        if (info.type == t) ++n;
    }
    return n;
}

} // namespace vsgraysim
