/// @file EngagementVisualFeature.cpp
/// @brief 见 EngagementVisualFeature.h。
#include "EngagementVisualFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"

#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace vsgraysim { namespace viewport3d {

namespace {

const gui::ViewportKit::LineStyle kLinkStyle{2.0f, true};   ///< 脉冲环线框
const gui::ViewportKit::LineStyle kFxStyle{1.8f, true};     ///< 爆炸/残骸
constexpr float kImpactMaxR = 9.0f;                         ///< 爆炸最大半径 (m)

/// 交战条目是否一致 (快照周期重复喂入时免重建+免编译)。
bool sameVec(const vsg::vec3& a, const vsg::vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool sameEngagements(const std::vector<EngagementVisualFeature::Engagement>& a,
                     const std::vector<EngagementVisualFeature::Engagement>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.interceptor_id != y.interceptor_id || x.target_id != y.target_id ||
            x.faction != y.faction ||
            !sameVec(x.interceptor_pos, y.interceptor_pos) ||
            !sameVec(x.target_pos, y.target_pos)) return false;
    }
    return true;
}

/// 阵营 → 配色 (红 #ff5252 / 蓝 #448aff / 中立 #9e9e9e)。
QColor engFactionColor(int faction)
{
    switch (faction) {
        case 2: return QColor(0xff, 0x52, 0x52);
        case 1: return QColor(0x44, 0x8a, 0xff);
        default: break;
    }
    return QColor(0x9e, 0x9e, 0x9e);
}

/// 制导/交战连线橙色 (与关系图 fire_control 同族, 醒目区分)。
QColor linkColor() { return QColor(0xff, 0xa7, 0x26); }

/// 爆炸星形线段: 中心向 8 方向放射, 半径随老化线性增大。
void pushBurst(std::vector<vsg::vec3>& pts, const vsg::vec3& c, float r)
{
    const float q = r;   // XZ 平面放射
    pts.emplace_back(c.x + q, c.y, c.z);
    pts.emplace_back(c.x - q, c.y, c.z);
    pts.emplace_back(c.x, c.y, c.z + q);
    pts.emplace_back(c.x, c.y, c.z - q);
    const float d = q * 0.7071f;
    pts.emplace_back(c.x + d, c.y, c.z + d);
    pts.emplace_back(c.x - d, c.y, c.z - d);
    pts.emplace_back(c.x - d, c.y, c.z + d);
    pts.emplace_back(c.x + d, c.y, c.z - d);
}

/// XZ 平面单位八边形环线段 (原点, 半径 1), 供矩阵驱动的脉冲环/冲击波。
std::vector<vsg::vec3> unitRing(int segs = 24)
{
    std::vector<vsg::vec3> pts;
    pts.reserve(size_t(segs) * 2);
    for (int i = 0; i < segs; ++i) {
        const double a0 = 2.0 * M_PI * double(i) / double(segs);
        const double a1 = 2.0 * M_PI * double(i + 1) / double(segs);
        pts.emplace_back(float(std::cos(a0)), 0.0f, float(std::sin(a0)));
        pts.emplace_back(float(std::cos(a1)), 0.0f, float(std::sin(a1)));
    }
    return pts;
}

/// 单位竖直烟团柱 (底部在原点): 自底向上 3 片烟盘 (半径递增),
/// 供发射/持续毁伤烟柱的矩阵驱动上升动画。
vsg::ref_ptr<vsg::Group> makeSmokePuffs(const QColor& col)
{
    auto g = vsg::Group::create();
    for (int i = 0; i < 3; ++i) {
        const float y = 1.2f + 1.7f * float(i);
        const double r = 1.3 + 0.55 * double(i);
        if (auto d = gui::ViewportKit::makeFilledDisc(
                {0.0f, y, 0.0f}, r, col,
                0.24f - 0.05f * float(i), 0.02f, 20))
            g->addChild(d);
    }
    return g;
}

/// 单位尾焰 (顶点在原点, 沿 -Y 喷出): 放射短线 + 喷口环 + 辉光盘。
/// 外部用矩阵把本地 -Y 对齐到世界飞行反方向 (尾焰向后)。
vsg::ref_ptr<vsg::Group> makeFlameCone(const QColor& col)
{
    std::vector<vsg::vec3> pts;
    constexpr int rays = 6;
    for (int i = 0; i < rays; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(rays);
        pts.emplace_back(0.0f, 0.0f, 0.0f);
        pts.emplace_back(float(0.38 * std::cos(a)), -1.0f, float(0.38 * std::sin(a)));
    }
    for (int i = 0; i < rays; ++i) {
        const double a0 = 2.0 * M_PI * double(i) / double(rays);
        const double a1 = 2.0 * M_PI * double(i + 1) / double(rays);
        pts.emplace_back(float(0.38 * std::cos(a0)), -1.0f, float(0.38 * std::sin(a0)));
        pts.emplace_back(float(0.38 * std::cos(a1)), -1.0f, float(0.38 * std::sin(a1)));
    }
    auto g = vsg::Group::create();
    if (auto n = gui::ViewportKit::makeLineSegments(pts, col, kFxStyle))
        g->addChild(n);
    if (auto d = gui::ViewportKit::makeFilledDisc(
            {0.0f, -0.35f, 0.0f}, 0.45, col.lighter(130), 0.35f, 0.05f, 16))
        g->addChild(d);
    return g;
}

/// 把本地 -Y 轴对齐到世界方向 b 的旋转矩阵 (尾焰定向用)。
vsg::dmat4 alignNegY(const vsg::dvec3& b)
{
    const vsg::dvec3 u(0.0, -1.0, 0.0);
    const double d = vsg::dot(u, b);
    if (d > 0.99999) return vsg::dmat4(1.0);
    if (d < -0.99999) return vsg::rotate(M_PI, vsg::dvec3(1.0, 0.0, 0.0));
    const vsg::dvec3 ax = vsg::normalize(vsg::cross(u, b));
    return vsg::rotate(std::acos(std::clamp(d, -1.0, 1.0)), ax);
}

/// 尾焰列表是否一致 (快照周期重复喂入时免重建)。
bool samePlumes(const std::vector<EngagementVisualFeature::Plume>& a,
                const std::vector<EngagementVisualFeature::Plume>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.id != y.id || x.faction != y.faction ||
            !sameVec(x.pos, y.pos) || !sameVec(x.dir, y.dir)) return false;
    }
    return true;
}

/// 损伤冒烟列表是否一致 (快照周期重复喂入时免重建)。
bool sameDamageSmokes(const std::vector<EngagementVisualFeature::DamageSmoke>& a,
                      const std::vector<EngagementVisualFeature::DamageSmoke>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.id != y.id || x.severity != y.severity ||
            !sameVec(x.pos, y.pos)) return false;
    }
    return true;
}

} // namespace

void EngagementVisualFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    root_ = vsg::Group::create();
    if (enabled_) setInWorld(true);
}

void EngagementVisualFeature::onDetach(ViewportContext& ctx)
{
    setInWorld(false);
    ctx_ = nullptr;
    root_ = {};
}

void EngagementVisualFeature::setEnabled(bool on)
{
    enabled_ = on;
    if (ctx_) { if (on) setInWorld(true); else setInWorld(false); }
}

void EngagementVisualFeature::setInWorld(bool add)
{
    if (!root_ || !ctx_) return;
    const bool now = in_world_;
    if (add && !now) {
        ctx_->contentRoot()->addChild(root_);
        in_world_ = true;
    } else if (!add && now) {
        ctx_->removeFromWorld(root_);
        in_world_ = false;
    }
    ctx_->requestCompile();
}

void EngagementVisualFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    pulse_elapsed_ = tick.elapsed_sec;
    bool changed = false;
    for (auto it = impacts_.begin(); it != impacts_.end();) {
        it->age_sec += static_cast<float>(tick.dt_sec);
        if (it->age_sec >= impact_life_) { it = impacts_.erase(it); changed = true; }
        else ++it;
    }
    for (auto it = launches_.begin(); it != launches_.end();) {
        it->age_sec += static_cast<float>(tick.dt_sec);
        if (it->age_sec >= launch_life_) { it = launches_.erase(it); changed = true; }
        else ++it;
    }
    for (auto it = miss_marks_.begin(); it != miss_marks_.end();) {
        it->age_sec += static_cast<float>(tick.dt_sec);
        if (it->age_sec >= miss_life_) { it = miss_marks_.erase(it); changed = true; }
        else ++it;
    }
    if (changed) { rebuild(); return; }
    // 存活特效逐帧仅更新平移/缩放矩阵 (push-constant 动态, 无需编译)。
    for (auto& im : impacts_) {
        const float t = std::clamp(im.age_sec / std::max(0.001f, impact_life_),
                                   0.0f, 1.0f);
        if (im.node) {
            // 星形闪光: 前 35% 快速扩张, 之后隐藏 (Switch 开关)。
            const float ts = std::clamp(t / 0.35f, 0.0f, 1.0f);
            const float r  = 1.0f + kImpactMaxR * 0.55f * ts;
            im.node->matrix = vsg::translate(vsg::dvec3(im.pos)) *
                              vsg::scale(double(r));
            if (im.flash_switch)
                im.flash_switch->setAllChildren(t < 0.35f);
        }
        if (im.ring_node) {
            // 冲击波环: 全程扩张 (缓出), 前段线性后收尾。
            const float tr = std::clamp((im.age_sec - 0.05f) /
                                        std::max(0.001f, impact_life_ * 0.9f),
                                        0.0f, 1.0f);
            const float rr = 2.0f + kImpactMaxR * 1.35f * (1.0f - (1.0f - tr) * (1.0f - tr));
            im.ring_node->matrix = vsg::translate(
                vsg::dvec3(im.pos.x, im.pos.y + 0.4, im.pos.z)) *
                vsg::scale(double(rr));
            if (im.disc_node)
                im.disc_node->matrix = vsg::translate(
                    vsg::dvec3(im.pos.x, im.pos.y + 0.25, im.pos.z)) *
                    vsg::scale(double(rr) * 0.82);
        }
    }
    // 拦截弹脉冲环: 位置随拦截弹, 半径呼吸脉冲 (活跃交战指示)。
    for (auto& [id, mt] : pulse_nodes_) {
        if (!mt) continue;
        const auto it = std::find_if(engs_.begin(), engs_.end(),
            [&](const Engagement& e) { return e.interceptor_id == id; });
        if (it == engs_.end()) continue;
        const float pulse =
            4.0f + 1.6f * float(std::sin(pulse_elapsed_ * 5.0));
        mt->matrix = vsg::translate(vsg::dvec3(
            it->interceptor_pos.x, it->interceptor_pos.y + 0.6,
            it->interceptor_pos.z)) * vsg::scale(double(pulse));
    }
    // 发射特效: 前段起推闪光 → 扬尘环地面扩散 → 烟团柱持续上升。
    for (auto& ln : launches_) {
        const float t = std::clamp(ln.age_sec / std::max(0.001f, launch_life_),
                                   0.0f, 1.0f);
        if (ln.flash_node) {
            const float ts = std::clamp(t / 0.15f, 0.0f, 1.0f);
            const float r = 0.8f + 5.5f * ts;
            ln.flash_node->matrix = vsg::translate(vsg::dvec3(
                ln.pos.x, ln.pos.y + 1.2, ln.pos.z)) * vsg::scale(double(r));
            if (ln.flash_switch)
                ln.flash_switch->setAllChildren(t < 0.15f);
        }
        if (ln.dust_node) {
            const float tr = std::clamp(t / 0.45f, 0.0f, 1.0f);
            const float rr = 2.0f + 9.0f * (1.0f - (1.0f - tr) * (1.0f - tr));
            ln.dust_node->matrix = vsg::translate(vsg::dvec3(
                ln.pos.x, ln.pos.y + 0.3, ln.pos.z)) * vsg::scale(double(rr));
        }
        if (ln.smoke_node) {
            const float ts = std::clamp((t - 0.08f) / 0.92f, 0.0f, 1.0f);
            const float s = 0.5f + 1.3f * ts;
            ln.smoke_node->matrix = vsg::translate(vsg::dvec3(ln.pos)) *
                                    vsg::scale(double(s));
        }
    }
    // 尾焰: 位置随弹体, 长度闪烁 (发动机推力抖动)。
    for (const auto& p : plumes_) {
        auto it = plume_nodes_.find(p.id);
        if (it == plume_nodes_.end() || !it->second) continue;
        const float flick = 0.8f + 0.3f * float(std::sin(pulse_elapsed_ * 27.0 +
                              double(p.id.size()) * 1.7));
        const vsg::vec3 nd = vsg::length(p.dir) > 1e-4f
                           ? vsg::normalize(p.dir) : vsg::vec3(0.0f, 0.0f, 1.0f);
        it->second->matrix =
            vsg::translate(vsg::dvec3(p.pos)) *
            alignNegY(vsg::dvec3(-nd.x, -nd.y, -nd.z)) *
            vsg::scale(double(4.5f * flick));
    }
    // 持续毁伤: 残骸火光盘闪烁 + 烟团柱循环上升 (常驻)。
    for (auto& w : wrecks_) {
        if (w.fire_node) {
            const double seed = double(std::hash<std::string>{}(w.id) % 97) * 0.13;
            const float fl = 0.85f + 0.35f * float(std::sin(pulse_elapsed_ * 9.0 + seed));
            w.fire_node->matrix = vsg::translate(vsg::dvec3(
                w.pos.x, w.pos.y + 0.3, w.pos.z)) * vsg::scale(double(fl));
        }
        if (w.smoke_node) {
            w.smoke_phase += static_cast<float>(tick.dt_sec);
            const float ph = std::fmod(w.smoke_phase, 7.0f) / 7.0f;
            const float s = 0.7f + 0.9f * ph;
            w.smoke_node->matrix = vsg::translate(vsg::dvec3(w.pos)) *
                                   vsg::scale(double(s));
        }
    }
    // 损伤冒烟: 烟团柱循环上升 (大小随损伤程度, 常驻至平台修复/摧毁)。
    for (const auto& ds : damage_smokes_) {
        auto it = dmg_nodes_.find(ds.id);
        if (it == dmg_nodes_.end() || !it->second.node) continue;
        it->second.phase += static_cast<float>(tick.dt_sec);
        const float ph = std::fmod(it->second.phase, 6.0f) / 6.0f;
        const float s = std::clamp(ds.severity, 0.1f, 1.0f) * (0.5f + 0.7f * ph);
        it->second.node->matrix = vsg::translate(vsg::dvec3(ds.pos)) *
                                  vsg::scale(double(s));
    }
    // 刷新制导管道/弹道条带 shader 的相机矩阵 + 数据流时间。
    // param1 = 1.0 表示链路连通 (制导中), 数据流从拦截弹流向目标。
    if (!shader_handles_.empty()) {
        vsg::dmat4 vp; vsg::dvec3 cp;
        gui::ViewportKit::cameraMatrices(ctx.camera(), vp, cp);
        for (auto& h : shader_handles_)
            gui::ViewportKit::updateShaderNode(h, vp, cp,
                                               float(pulse_elapsed_), 1.0f, 0.0f,
                                               ctx.contentModel());
    }
}

void EngagementVisualFeature::setEngagements(const std::vector<Engagement>& engs)
{
    if (sameEngagements(engs_, engs)) return;   // 快照无变化: 跳过重建

    engs_ = engs;

    // 更新弹道历史: 对仍在飞拦截弹累积位置; 移除不再在飞的交战历史。
    std::set<std::string> alive;
    for (const auto& e : engs_) {
        alive.insert(e.interceptor_id);
        auto& h = ball_hist_[e.interceptor_id];
        if (h.empty() || vsg::length(h.back() - e.interceptor_pos) > 0.05f)
            h.push_back(e.interceptor_pos);
        while (static_cast<int>(h.size()) > ball_max_) h.pop_front();
    }
    for (auto it = ball_hist_.begin(); it != ball_hist_.end();) {
        if (!alive.count(it->first)) it = ball_hist_.erase(it);
        else ++it;
    }
    // 同步移除已结束交战的脉冲环。
    for (auto it = pulse_nodes_.begin(); it != pulse_nodes_.end();) {
        if (!alive.count(it->first)) it = pulse_nodes_.erase(it);
        else ++it;
    }
    rebuild();
}

void EngagementVisualFeature::setPlumes(const std::vector<Plume>& plumes)
{
    if (samePlumes(plumes_, plumes)) return;   // 快照无变化: 跳过重建
    plumes_ = plumes;
    // 移除不再在飞导弹的尾焰节点。
    std::set<std::string> alive;
    for (const auto& p : plumes_) alive.insert(p.id);
    for (auto it = plume_nodes_.begin(); it != plume_nodes_.end();)
        if (!alive.count(it->first)) it = plume_nodes_.erase(it);
        else ++it;
    rebuild();
}

void EngagementVisualFeature::addLaunch(const vsg::vec3& pos, int faction)
{
    launches_.push_back({pos, faction, 0.0f});
    rebuild();
}

void EngagementVisualFeature::addMiss(const vsg::vec3& pos, int faction)
{
    miss_marks_.push_back({pos, faction, 0.0f});
    rebuild();
}

void EngagementVisualFeature::setDamageSmokes(const std::vector<DamageSmoke>& smokes)
{
    if (sameDamageSmokes(damage_smokes_, smokes)) return;   // 快照无变化: 跳过重建
    damage_smokes_ = smokes;
    // 移除不再冒烟平台的节点。
    std::set<std::string> alive;
    for (const auto& ds : damage_smokes_) alive.insert(ds.id);
    for (auto it = dmg_nodes_.begin(); it != dmg_nodes_.end();)
        if (!alive.count(it->first)) it = dmg_nodes_.erase(it);
        else ++it;
    rebuild();
}

void EngagementVisualFeature::addImpact(const vsg::vec3& pos, int faction)
{
    impacts_.push_back({pos, faction, 0.0f});
    rebuild();
}

void EngagementVisualFeature::addWreck(const std::string& id, const vsg::vec3& pos,
                                       int faction)
{
    for (const auto& w : wrecks_) if (w.id == id) return;   // 幂等
    wrecks_.push_back({id, pos, faction});
    rebuild();
}

void EngagementVisualFeature::removeWreck(const std::string& id)
{
    auto it = std::remove_if(wrecks_.begin(), wrecks_.end(),
                             [&](const Wreck& w) { return w.id == id; });
    if (it != wrecks_.end()) { wrecks_.erase(it); rebuild(); }
}

void EngagementVisualFeature::clear()
{
    engs_.clear();
    ball_hist_.clear();
    impacts_.clear();
    wrecks_.clear();
    launches_.clear();
    plumes_.clear();
    miss_marks_.clear();
    damage_smokes_.clear();
    pulse_nodes_.clear();
    plume_nodes_.clear();
    dmg_nodes_.clear();
    rebuild();
}

void EngagementVisualFeature::rebuild()
{
    if (!root_) return;
    root_->children.clear();
    shader_handles_.clear();

    // 1) 制导连线 (拦截弹 → 目标): 半透明管道表现 "制导指令链",
    //    管道内数据流方向从拦截弹流向目标 (uv.x 递增方向)。
    //    管道半径与分段数距离自适应, 复用 comm_tube shader (橙色)。
    for (const auto& e : engs_) {
        const vsg::vec3 d = e.target_pos - e.interceptor_pos;
        const float len = vsg::length(d);
        if (len < 1e-3f) continue;
        const float radius = std::clamp(len * 0.008f, 1.5f, 4.0f);
        const int segs = std::clamp(int(len / 10.0f) + 2, 4, 32);
        gui::ViewportKit::ShaderHandle sh;
        if (auto tube = gui::ViewportKit::makeCommTube(
                e.interceptor_pos, e.target_pos,
                radius, segs, 12, linkColor(), 0.28f, &sh)) {
            root_->addChild(tube);
            if (sh.valid()) shader_handles_.push_back(sh);
        }
    }

    // 1b) 拦截弹脉冲环 (活跃交战指示): 半透明盘 + 亮环, 矩阵驱动位置/脉冲。
    for (const auto& e : engs_) {
        auto& mt = pulse_nodes_[e.interceptor_id];
        if (!mt) {
            mt = vsg::MatrixTransform::create();
            if (auto disc = gui::ViewportKit::makeFilledDisc(
                    {0.0f, 0.0f, 0.0f}, 1.0, linkColor(), 0.30f, 0.02f))
                mt->addChild(disc);
            if (auto ring = gui::ViewportKit::makeLineSegments(
                    unitRing(), linkColor().lighter(140), kLinkStyle))
                mt->addChild(ring);
        }
        if (mt) root_->addChild(mt);
    }

    // 2) 拦截弹道条带 (拦击方阵营色历史拖尾): 用水平条带网格替代折线,
    //    复用 trail_ribbon shader 沿长度方向渐隐 (旧段透明→新段不透明)。
    for (const auto& [id, h] : ball_hist_) {
        (void)id;
        if (h.size() < 2) continue;
        QColor col = engFactionColor(2);   // 默认攻击方为红, 下方按交战匹配重设
        for (const auto& e : engs_)
            if (e.interceptor_id == id) { col = engFactionColor(e.faction); break; }
        std::vector<vsg::vec3> pts(h.begin(), h.end());
        gui::ViewportKit::ShaderHandle sh;
        if (auto ribbon = gui::ViewportKit::makeTrailRibbon(pts, 3.0f, col, &sh)) {
            root_->addChild(ribbon);
            if (sh.valid()) shader_handles_.push_back(sh);
        }
    }

    // 3) 命中爆炸: 星形闪光 (前段) + 冲击波环 (全程), 均为单位几何 +
    //    矩阵驱动, onFrame 逐帧只改矩阵 (无重建、无编译)。
    for (auto& im : impacts_) {
        if (!im.node) {
            std::vector<vsg::vec3> unit;
            pushBurst(unit, {0.0f, 0.0f, 0.0f}, 1.0f);
            auto geom = gui::ViewportKit::makeLineSegments(
                unit, engFactionColor(im.faction).lighter(150), kFxStyle);
            im.flash_switch = vsg::Switch::create();
            if (geom) im.flash_switch->addChild(vsg::Mask(0xFFFFFFFFull), geom);
            auto mt = vsg::MatrixTransform::create();
            if (im.flash_switch) mt->addChild(im.flash_switch);
            im.node = mt;
        }
        if (!im.ring_node) {
            auto geom = gui::ViewportKit::makeLineSegments(
                unitRing(), engFactionColor(im.faction), kFxStyle);
            auto mt = vsg::MatrixTransform::create();
            if (geom) mt->addChild(geom);
            im.ring_node = mt;
        }
        if (!im.disc_node) {
            // 半透明冲击波地盘 (径向渐隐)。
            auto geom = gui::ViewportKit::makeFilledDisc(
                {0.0f, 0.0f, 0.0f}, 1.0, engFactionColor(im.faction),
                0.40f, 0.02f);
            auto mt = vsg::MatrixTransform::create();
            if (geom) mt->addChild(geom);
            im.disc_node = mt;
        }
        root_->addChild(im.node);
        root_->addChild(im.ring_node);
        root_->addChild(im.disc_node);
    }

    // 3b) 发射特效: 起推闪光 (前段) + 扬尘环 (地面扩散) + 烟团柱 (上升),
    //     均为单位几何 + 矩阵驱动, onFrame 逐帧只改矩阵。
    for (auto& ln : launches_) {
        const QColor lc = engFactionColor(ln.faction);
        if (!ln.flash_node) {
            std::vector<vsg::vec3> unit;
            pushBurst(unit, {0.0f, 0.0f, 0.0f}, 1.0f);
            auto geom = gui::ViewportKit::makeLineSegments(
                unit, lc.lighter(160), kFxStyle);
            ln.flash_switch = vsg::Switch::create();
            if (geom) ln.flash_switch->addChild(vsg::Mask(0xFFFFFFFFull), geom);
            auto mt = vsg::MatrixTransform::create();
            if (ln.flash_switch) mt->addChild(ln.flash_switch);
            ln.flash_node = mt;
        }
        if (!ln.dust_node) {
            auto mt = vsg::MatrixTransform::create();
            if (auto ring = gui::ViewportKit::makeLineSegments(
                    unitRing(), QColor(0xc9, 0xb2, 0x8a), kFxStyle))
                mt->addChild(ring);
            if (auto disc = gui::ViewportKit::makeFilledDisc(
                    {0.0f, 0.0f, 0.0f}, 1.0, QColor(0xc9, 0xb2, 0x8a),
                    0.30f, 0.02f))
                mt->addChild(disc);
            ln.dust_node = mt;
        }
        if (!ln.smoke_node) {
            auto mt = vsg::MatrixTransform::create();
            mt->addChild(makeSmokePuffs(QColor(0x8a, 0x8a, 0x8a)));
            ln.smoke_node = mt;
        }
        root_->addChild(ln.flash_node);
        root_->addChild(ln.dust_node);
        root_->addChild(ln.smoke_node);
    }

    // 3c) 在飞导弹尾焰: 单位火焰锥 + 矩阵对齐/闪烁 (位置随弹体)。
    for (const auto& p : plumes_) {
        auto& mt = plume_nodes_[p.id];
        if (!mt) {
            mt = vsg::MatrixTransform::create();
            mt->addChild(makeFlameCone(QColor(0xff, 0x8a, 0x3c)));
        }
        root_->addChild(mt);
    }

    // 3e) 损伤冒烟: 受损未摧毁平台的持续烟柱 (矩阵驱动循环上升)。
    for (const auto& ds : damage_smokes_) {
        auto& dn = dmg_nodes_[ds.id];
        if (!dn.node) {
            dn.node = vsg::MatrixTransform::create();
            dn.node->addChild(makeSmokePuffs(QColor(0x55, 0x55, 0x55)));
        }
        root_->addChild(dn.node);
    }

    // 3d) 脱靶标注: X 交叉 + 小环 (落点标记, 到期移除)。
    for (auto& mm : miss_marks_) {
        if (!mm.node) {
            const QColor mc = engFactionColor(mm.faction).lighter(120);
            const float h = 2.2f, y = mm.pos.y + 0.3f;
            std::vector<vsg::vec3> pts = {
                {mm.pos.x - h, y, mm.pos.z - h}, {mm.pos.x + h, y, mm.pos.z + h},
                {mm.pos.x - h, y, mm.pos.z + h}, {mm.pos.x + h, y, mm.pos.z - h},
            };
            auto mt = vsg::MatrixTransform::create();
            if (auto x = gui::ViewportKit::makeLineSegments(pts, mc, kFxStyle))
                mt->addChild(x);
            if (auto ring = gui::ViewportKit::makeLineSegments(
                    unitRing(), mc, kFxStyle)) {
                auto rt = vsg::MatrixTransform::create();
                rt->matrix = vsg::translate(vsg::dvec3(
                    mm.pos.x, y, mm.pos.z)) * vsg::scale(3.0);
                rt->addChild(ring);
                mt->addChild(rt);
            }
            mm.node = mt;
        }
        root_->addChild(mm.node);
    }

    // 4) 摧毁残留: 灰色大叉 + 底座方环 + 半透明暗底盘 (常驻, 记靶位残骸)。
    const QColor wrCol(112, 118, 128);
    for (auto& w : wrecks_) {
        const float h = 4.0f, y = w.pos.y + 0.35f;
        std::vector<vsg::vec3> pts = {
            {w.pos.x - h, y, w.pos.z - h}, {w.pos.x + h, y, w.pos.z + h},
            {w.pos.x - h, y, w.pos.z + h}, {w.pos.x + h, y, w.pos.z - h},
        };
        // 底座方环
        const float q = 5.0f, y0 = w.pos.y + 0.05f;
        pts.push_back({w.pos.x - q, y0, w.pos.z - q}); pts.push_back({w.pos.x + q, y0, w.pos.z - q});
        pts.push_back({w.pos.x + q, y0, w.pos.z - q}); pts.push_back({w.pos.x + q, y0, w.pos.z + q});
        pts.push_back({w.pos.x + q, y0, w.pos.z + q}); pts.push_back({w.pos.x - q, y0, w.pos.z + q});
        pts.push_back({w.pos.x - q, y0, w.pos.z + q}); pts.push_back({w.pos.x - q, y0, w.pos.z - q});
        if (auto n = gui::ViewportKit::makeLineSegments(pts, wrCol, kFxStyle))
            root_->addChild(n);
        // 半透明暗底盘 (残骸区域标识)。
        if (auto d = gui::ViewportKit::makeFilledDisc(
                {w.pos.x, w.pos.y + 0.04f, w.pos.z}, 7.0, wrCol,
                0.26f, 0.03f))
            root_->addChild(d);
        // 持续毁伤: 火光盘 (闪烁) + 烟团柱 (循环上升), 矩阵驱动无重建。
        if (!w.fire_node) {
            auto mt = vsg::MatrixTransform::create();
            if (auto disc = gui::ViewportKit::makeFilledDisc(
                    {0.0f, 0.0f, 0.0f}, 2.6, QColor(0xff, 0x6d, 0x2e),
                    0.42f, 0.03f, 24))
                mt->addChild(disc);
            std::vector<vsg::vec3> fp;
            pushBurst(fp, {0.0f, 0.5f, 0.0f}, 1.6f);
            if (auto fl = gui::ViewportKit::makeLineSegments(
                    fp, QColor(0xff, 0xb7, 0x4d), kFxStyle))
                mt->addChild(fl);
            w.fire_node = mt;
        }
        if (!w.smoke_node) {
            auto mt = vsg::MatrixTransform::create();
            mt->addChild(makeSmokePuffs(QColor(0x6e, 0x6e, 0x6e)));
            w.smoke_node = mt;
        }
        root_->addChild(w.fire_node);
        root_->addChild(w.smoke_node);
    }

    if (ctx_) ctx_->requestCompile();
}

VSGVP_REGISTER_FEATURE(EngagementVisualFeature);

}} // namespace vsgraysim::viewport3d