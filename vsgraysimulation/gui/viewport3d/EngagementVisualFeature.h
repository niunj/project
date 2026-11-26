#pragma once
/// @file EngagementVisualFeature.h
/// @brief 交战过程视觉特性: 制导连线 + 拦截弹道 + 发射/命中特效 + 尾焰 + 摧毁残留。
///
/// 由仿真门面按固定周期全量喂入当前"在飞"交战 (制导连线 + 弹道累积 +
/// 尾焰羽流), 发射瞬间调用 addLaunch 产生起推闪光/烟柱, 命中瞬间调用
/// addImpact 产生随时间扩大的爆炸特效, 脱靶调用 addMiss 打脱靶标记,
/// 击毁时 addWreck 在靶位常驻灰色残骸 + 持续燃烧烟柱。
/// 各时间性特效寿命由 onFrame 推进, 到期自动移除。
#ifndef VSGRAYSIM_VIEWPORT3D_ENGAGEMENTVISUALFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_ENGAGEMENTVISUALFEATURE_H

#include "IViewportFeature.h"
#include "ViewportKit.h"
#include "AviationParticles.h"
#include "SceneEffectsAdapter.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vsg { class Group; }

namespace vsgraysim { namespace viewport3d {

class EngagementVisualFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "engagement_visual";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json&) override;
    /// 整体显隐开关 (从世界场景根增删本特性节点)。
    void setEnabled(bool on) override;

    struct Engagement {       ///< 在飞交战
        std::string interceptor_id;
        std::string target_id;
        vsg::vec3   interceptor_pos;
        vsg::vec3   target_pos;
        int         faction = 2;   ///< 拦截弹阵营 0=中立,1=蓝,2=红 (与关系图一致)
    };
    struct Impact {           ///< 命中/爆炸 (随时间扩大, 到期移除)
        vsg::vec3 pos;
        int       faction = 2;
        float     age_sec = 0.0f;   ///< 由 onFrame 推进
        // 爆炸星形闪光 + 冲击波环/盘 (单位几何 + 平移/缩放矩阵):
        // onFrame 逐帧只改矩阵/开关, 不重建几何、不触发编译。
        vsg::ref_ptr<vsg::MatrixTransform> node;       ///< 星形闪光 (前期)
        vsg::ref_ptr<vsg::Switch> flash_switch;        ///< 闪光期显隐开关
        vsg::ref_ptr<vsg::MatrixTransform> ring_node;  ///< 冲击波环 (全程)
        vsg::ref_ptr<vsg::MatrixTransform> disc_node;  ///< 冲击波地盘 (半透明)
    };
    struct Wreck {            ///< 摧毁残留 (常驻, 含持续燃烧/烟柱动画节点)
        std::string id;
        vsg::vec3   pos;
        int         faction = 2;
        // 持续毁伤: 火光盘 (闪烁) + 烟团柱 (循环上升), 矩阵驱动无重建。
        vsg::ref_ptr<vsg::MatrixTransform> fire_node;
        vsg::ref_ptr<vsg::MatrixTransform> smoke_node;
        float smoke_phase = 0.0f;
    };
    struct Launch {           ///< 发射瞬间 (起推闪光 + 扬尘环 + 上升烟柱, 到期移除)
        vsg::vec3 pos;
        int       faction = 2;
        float     age_sec = 0.0f;   ///< 由 onFrame 推进
        vsg::ref_ptr<vsg::MatrixTransform> flash_node;   ///< 起推闪光 (前段)
        vsg::ref_ptr<vsg::Switch> flash_switch;
        vsg::ref_ptr<vsg::MatrixTransform> dust_node;    ///< 扬尘环 (地面扩散)
        vsg::ref_ptr<vsg::MatrixTransform> smoke_node;   ///< 上升烟团柱 (后段)
    };
    struct Plume {            ///< 在飞导弹发动机尾焰 (全量刷新, 随弹体位置)
        std::string id;
        vsg::vec3   pos;
        vsg::vec3   dir{0.0f, 0.0f, 1.0f};  ///< 飞行方向 (尾焰向后)
        int         faction = 2;
        bool jet = false;
        float strength = 1.f;
        float radius = .45f;
        std::optional<AviationParticles::Style> visualStyle;
        visual_effects::ExhaustAppearance exhaust{};
    };
    struct MissMark {         ///< 脱靶标注 (X 交叉 + 小环, 到期移除)
        vsg::vec3 pos;
        int       faction = 2;
        float     age_sec = 0.0f;
        vsg::ref_ptr<vsg::MatrixTransform> node;
    };
    struct DamageSmoke {      ///< 损伤冒烟 (受损未摧毁平台, 全量刷新)
        std::string id;
        vsg::vec3 pos;
        float severity = 1.0f;   ///< 损伤程度 0..1 (决定烟柱大小)
    };

    /// 全量刷新当前在飞交战: 制导连线按当前双方位置绘制, 弹道按拦截弹
    /// 位置逐帧累积 (每个拦截弹独立历史); 不在新列表中的交战历史自动清除。
    void setEngagements(const std::vector<Engagement>& engs);
    /// 全量刷新在飞导弹尾焰 (位置 + 飞行方向; 尾焰朝 -dir 喷出)。
    void setPlumes(const std::vector<Plume>& plumes);
    void setPlatformPlumes(const std::string& owner, std::vector<Plume>);
    /// 发射瞬间: 起推闪光 + 扬尘环 + 上升烟柱 (内部按时间淡出)。
    void addLaunch(const vsg::vec3& pos, int faction);
    void addPreviewFlash(const vsg::vec3& pos,const vsg::vec3& direction,float scale);
    /// 命中/爆炸: 世界位置 + 阵营 (内部按时间淡出)。
    void addImpact(const vsg::vec3& pos, int faction);
    size_t burningDebrisCount() const {return burningDebrisCount_;}
    size_t smokeHistoryCount() const {return particles_.historyCount();}
    unsigned visibleParticles() const {return particles_.visibleCount();}
    unsigned visibleJetFlames() const {return particles_.visibleJetFlames();}
    bool particleMode() const {return !legacy_fx_&&particles_.node()!=nullptr;}
    /// 脱靶标注: 命中但未摧毁时的落点标记 (内部按时间淡出)。
    void addMiss(const vsg::vec3& pos, int faction);
    /// 全量刷新受损平台冒烟 (健康度低的存活平台)。
    void setDamageSmokes(const std::vector<DamageSmoke>& smokes);
    /// 摧毁残留: 常驻灰色残骸 + 持续燃烧烟柱 (同 id 重复调用幂等)。
    void addWreck(const std::string& id, const vsg::vec3& pos, int faction);
    void removeWreck(const std::string& id);
    void clear();

    void setImpactDuration(float sec) { impact_life_ = sec; }
    void setLaunchDuration(float sec) { launch_life_ = sec; }
    void setMissDuration(float sec) { miss_life_ = sec; }
    void setBalisticMaxPoints(int n) { ball_max_ = n < 2 ? 2 : n; }

private:
    void rebuild();
    void refreshLegacyPlumes();
    template<class Visitor> void visitPlumes(Visitor&& visit) const {
        for(const auto& plume:plumes_)visit(plume);
        for(const auto& entry:platformPlumes_)for(const auto& plume:entry.second)visit(plume);
    }
    SceneEffectsAdapter sceneEffects_;
    size_t burningDebrisCount_=0;
    void updateParticles(ViewportContext&,const FrameTick&);
    void setInWorld(bool add);

    AviationParticles particles_;
    bool legacy_fx_=false, annotations_=false;
    ViewportContext* ctx_ = nullptr;
    vsg::ref_ptr<vsg::Group> root_, attachedRoot_;
    std::vector<Engagement> engs_;
    std::vector<Impact> impacts_;
    std::vector<Wreck> wrecks_;
    std::vector<Launch> launches_;
    std::vector<Plume> plumes_;
    std::map<std::string,std::vector<Plume>> platformPlumes_;
    std::vector<MissMark> miss_marks_;
    std::vector<DamageSmoke> damage_smokes_;
    // 损伤冒烟节点 (unit 烟团柱 + 矩阵循环上升, 按平台 id 索引)。
    struct DmgNode { vsg::ref_ptr<vsg::MatrixTransform> node; float phase = 0.0f; };
    std::map<std::string, DmgNode> dmg_nodes_;
    std::map<std::string, std::deque<vsg::vec3>> ball_hist_; ///< 拦截弹 → 历史点
    // 拦截弹脉冲环 (unit 环 + 矩阵脉冲, 按拦截弹 id 索引, 交战结束移除)。
    std::map<std::string, vsg::ref_ptr<vsg::MatrixTransform>> pulse_nodes_;
    // 尾焰节点 (unit 火焰锥 + 矩阵对齐/闪烁, 按导弹 id 索引, 不在飞移除)。
    std::map<std::string, vsg::ref_ptr<vsg::MatrixTransform>> plume_nodes_;
    double pulse_elapsed_ = 0.0;
    int   ball_max_    = 80;
    float impact_life_ = 2.5f;
    float launch_life_ = 4.0f;   ///< 发射特效寿命 (烟柱持续较久)
    float miss_life_   = 8.0f;   ///< 脱靶标记驻留时长
    bool  in_world_    = false;
    /// 自定义 shader 管道/条带节点的逐帧相机更新句柄。
    std::vector<gui::ViewportKit::ShaderHandle> shader_handles_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_ENGAGEMENTVISUALFEATURE_H
