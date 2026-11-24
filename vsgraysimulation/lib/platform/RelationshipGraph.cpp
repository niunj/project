#include "RelationshipGraph.h"

namespace vsgraysim::platform {

uint32_t RelationshipGraph::addEdge(EntityKind from_kind, uint32_t from_id,
                                    EntityKind to_kind, uint32_t to_id,
                                    RelationType rel,
                                    const std::string& slot,
                                    const std::string& note)
{
    if (from_id == 0 || to_id == 0) return 0;

    // 查重: 完全相同的 (from,to,rel,slot) 不重复添加
    for (const auto& e : edges_) {
        if (e.from_kind == from_kind && e.from_id == from_id &&
            e.to_kind == to_kind && e.to_id == to_id &&
            e.rel == rel && e.slot == slot)
            return 0;
    }

    GraphEdge e;
    e.id = next_edge_id_++;
    e.from_kind = from_kind;
    e.from_id = from_id;
    e.to_kind = to_kind;
    e.to_id = to_id;
    e.rel = rel;
    e.slot = slot;
    e.note = note;
    e.enabled = true;
    edges_.push_back(e);
    return e.id;
}

bool RelationshipGraph::removeEdge(uint32_t edge_id)
{
    for (auto it = edges_.begin(); it != edges_.end(); ++it) {
        if (it->id == edge_id) {
            edges_.erase(it);
            return true;
        }
    }
    return false;
}

void RelationshipGraph::clear() { edges_.clear(); }

std::vector<GraphEdge> RelationshipGraph::edgesFrom(EntityKind from_kind,
                                                    uint32_t from_id,
                                                    RelationType rel) const
{
    std::vector<GraphEdge> out;
    for (const auto& e : edges_) {
        if (e.from_kind == from_kind && e.from_id == from_id &&
            e.rel == rel && e.enabled)
            out.push_back(e);
    }
    return out;
}

std::vector<GraphEdge> RelationshipGraph::edgesTo(EntityKind to_kind,
                                                  uint32_t to_id,
                                                  RelationType rel) const
{
    std::vector<GraphEdge> out;
    for (const auto& e : edges_) {
        if (e.to_kind == to_kind && e.to_id == to_id &&
            e.rel == rel && e.enabled)
            out.push_back(e);
    }
    return out;
}

std::vector<uint32_t> RelationshipGraph::guidedMissiles(uint32_t guidance_source_id) const
{
    std::vector<uint32_t> out;
    for (const auto& e : edgesFrom(EntityKind::PLATFORM, guidance_source_id, RelationType::GUIDES))
        if (e.to_kind == EntityKind::PLATFORM) out.push_back(e.to_id);
    // 制导源也可能是装备 (如某搜索/制导雷达装备), 一并考虑
    for (const auto& e : edgesFrom(EntityKind::EQUIPMENT, guidance_source_id, RelationType::GUIDES))
        if (e.to_kind == EntityKind::PLATFORM) out.push_back(e.to_id);
    return out;
}

std::vector<uint32_t> RelationshipGraph::equipmentOf(uint32_t platform_id) const
{
    std::vector<uint32_t> out;
    for (const auto& e : edgesFrom(EntityKind::PLATFORM, platform_id, RelationType::EQUIPPED_ON))
        if (e.to_kind == EntityKind::EQUIPMENT) out.push_back(e.to_id);
    return out;
}

std::vector<uint32_t> RelationshipGraph::guidanceSourcesOf(uint32_t missile_id) const
{
    std::vector<uint32_t> out;
    for (const auto& e : edgesTo(EntityKind::PLATFORM, missile_id, RelationType::GUIDES)) {
        // 源可以是平台或装备, 都按 id 返回 (调用方结合上下文判定种类)
        out.push_back(e.from_id);
    }
    return out;
}

nlohmann::json RelationshipGraph::toJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : edges_) {
        arr.push_back({
            {"id",        e.id},
            {"from_kind", int(e.from_kind)},
            {"from_id",   e.from_id},
            {"to_kind",   int(e.to_kind)},
            {"to_id",     e.to_id},
            {"rel",       relationTypeName(e.rel)},
            {"slot",      e.slot},
            {"enabled",   e.enabled},
            {"note",      e.note},
        });
    }
    return arr;
}

void RelationshipGraph::fromJson(const nlohmann::json& j)
{
    clear();
    if (!j.is_array()) return;
    for (const auto& it : j) {
        if (!it.is_object()) continue;
        auto fk = static_cast<EntityKind>(it.value("from_kind", 0));
        auto tk = static_cast<EntityKind>(it.value("to_kind", 0));
        const auto fid = it.value("from_id", 0);
        const auto tid = it.value("to_id", 0);
        const std::string rels = it.value("rel", std::string("guides"));
        RelationType rel = RelationType::GUIDES;
        if (rels == "equipped_on")     rel = RelationType::EQUIPPED_ON;
        if (rels == "launcher_of")     rel = RelationType::LAUNCHER_OF;
        if (rels == "seeker_locks")    rel = RelationType::SEEKER_LOCKS;
        if (rels == "target_of")       rel = RelationType::TARGET_OF;
        if (rels == "fire_control_of") rel = RelationType::FIRE_CONTROL_OF;
        if (rels == "fusion_peer")     rel = RelationType::FUSION_PEER;

        const auto eid = addEdge(fk, uint32_t(fid), tk, uint32_t(tid), rel,
                                 it.value("slot", std::string()),
                                 it.value("note", std::string()));
        if (eid != 0)
            edges_.back().enabled = it.value("enabled", true);
    }
}

} // namespace vsgraysim::platform