#pragma once
/// @file RelationshipGraph.h
/// @brief 场景实体"绑定关系图" — 显式建模平台/装备/导弹/目标之间的关系。
///
/// 把原先隐式的绑定 (PlatformInstance.parent_platform_id / equipment 数组) 升级为
/// 带类型的可查询关系图, 使运行时能回答:
///   - 制导雷达能指导哪些导弹 (GUIDES)
///   - 导弹当前导引链: 中制导源 + 自身导引头锁定的目标
///   - 某平台携带哪些装备, 挂在哪 (EQUIPPED_ON)
///
/// 关系可序列化为 JSON, 由场景设计软件写入场景文件, 仿真控制软件装载后
/// 重建图谱, 供制导闭环 (见 SimDataBroker EngagementRequest) 与多装备协同使用。
#ifndef VSGRAYSIM_PLATFORM_RELATIONSHIP_GRAPH_H
#define VSGRAYSIM_PLATFORM_RELATIONSHIP_GRAPH_H

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vsgraysim::platform {

/// 关系主体类型 (平台 或 装备)
enum class EntityKind : uint8_t {
    PLATFORM  = 0,
    EQUIPMENT = 1,
};

/// 关系类型 (物理语义)
enum class RelationType : uint8_t {
    EQUIPPED_ON,   ///< 装备挂在平台 (含挂点 slot / 安装角)
    LAUNCHER_OF,   ///< 平台是某导弹的发射载体 (导弹从挂点发射)
    GUIDES,        ///< 某装备/平台为中制导源, 指导某导弹 (为导弹提供信息)
    SEEKER_LOCKS,  ///< 导弹导引头当前锁定目标
    TARGET_OF,     ///< 通用"目标"关系 (锁定/交战)
    FIRE_CONTROL_OF, ///< 火控通道指配: 火控装备控制某武器通道
    FUSION_PEER,   ///< 多装备协同: 参与融合/互引的同伴
};

inline const char* relationTypeName(RelationType t) {
    switch (t) {
        case RelationType::EQUIPPED_ON:     return "equipped_on";
        case RelationType::LAUNCHER_OF:     return "launcher_of";
        case RelationType::GUIDES:          return "guides";
        case RelationType::SEEKER_LOCKS:    return "seeker_locks";
        case RelationType::TARGET_OF:       return "target_of";
        case RelationType::FIRE_CONTROL_OF: return "fire_control_of";
        case RelationType::FUSION_PEER:     return "fusion_peer";
    }
    return "unknown";
}

/// 一条有向边: from --relation--> to
struct GraphEdge {
    uint32_t     id = 0;         ///< 边 id (图内唯一)
    EntityKind   from_kind = EntityKind::PLATFORM;
    uint32_t     from_id = 0;    ///< 平台 id 或装备 id
    EntityKind   to_kind = EntityKind::PLATFORM;
    uint32_t     to_id = 0;
    RelationType rel = RelationType::GUIDES;
    std::string  slot;           ///< 挂点/通道名 (如 "under_wing_1" / "fire_channel_A")
    bool         enabled = true; ///< 是否生效 (可停用某条导引/协同)
    std::string  note;           ///< 说明
};

/// 绑定关系图 (场景级; 无引擎/UI 依赖, 便于跨层使用)
class RelationshipGraph {
public:
    RelationshipGraph() = default;

    /// 新增一条边; 返回边 id (0 表示失败/重复)
    uint32_t addEdge(EntityKind from_kind, uint32_t from_id,
                     EntityKind to_kind, uint32_t to_id,
                     RelationType rel,
                     const std::string& slot = {},
                     const std::string& note = {});

    /// 删除某条边
    bool removeEdge(uint32_t edge_id);

    /// 清空
    void clear();

    // ---- 查询 ----

    /// 所有边
    const std::vector<GraphEdge>& edges() const { return edges_; }

    /// 查询: 所有满足 from 的边 (过滤 kind/id)
    std::vector<GraphEdge> edgesFrom(EntityKind from_kind, uint32_t from_id,
                                     RelationType rel) const;

    /// 查询: 所有满足 to 的边
    std::vector<GraphEdge> edgesTo(EntityKind to_kind, uint32_t to_id,
                                   RelationType rel) const;

    /// 便捷: 某制导装备/平台能指导的导弹平台 id 列表
    std::vector<uint32_t> guidedMissiles(uint32_t guidance_source_id) const;

    /// 便捷: 某平台携带的指定类型装备 id 列表
    std::vector<uint32_t> equipmentOf(uint32_t platform_id) const;

    /// 便捷: 某导弹的导引链 (按 GUIDES 指向该导弹的源 id)
    std::vector<uint32_t> guidanceSourcesOf(uint32_t missile_id) const;

    // ---- 序列化 (场景 JSON) ----
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    uint32_t next_edge_id_ = 1;
    std::vector<GraphEdge> edges_;
};

} // namespace vsgraysim::platform

#endif // VSGRAYSIM_PLATFORM_RELATIONSHIP_GRAPH_H