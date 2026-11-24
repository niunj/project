#pragma once
/// @file RelationshipVisualFeature.h
/// @brief VSG 3D 平台标注特性: 阵营立杆/光柱 + 中文公告牌 + 关系链路拱线。
///
/// 拓扑数据与 Qt 关系图 (RelationshipGraphView, gui/display) 同源;
/// 3D 视口内显示平台标注与绑定/导引关系链路 (仰角拱线 + 方向箭头,
/// 按关系类型配色: 制导橙 / 通信青 / 其他灰, 非简单直线)。
/// 通过 setEnabled 切换整体显隐 (工具栏开关调用); 数据经 setGraph/setLinks 喂入。
#ifndef VSGRAYSIM_VIEWPORT3D_RELATIONSHIPVISUALFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_RELATIONSHIPVISUALFEATURE_H

#include "IViewportFeature.h"
#include "ViewportKit.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Switch.h>

#include <string>
#include <vector>

namespace vsg { class Group; class Font; }

namespace vsgraysim { namespace viewport3d {

class ViewportContext;

class RelationshipVisualFeature : public IViewportFeature {
public:
    static constexpr char kId[] = "relationship_visual";
    struct Marker {
        std::string  id;
        vsg::vec3    pos;
        int          faction = 0;   ///< 0=中立,1=蓝,2=红 (与 display 一致)
        std::string  label;         ///< 平台标注文字 (如 "红方导弹"), 空则不显示公告牌
    };
    /// 关系链路 (通信/制导等): 世界坐标端点 + 关系类型 (配色)。
    struct Link {
        std::string relation;       ///< "guides"/"communicates"/... (关系词表)
        vsg::vec3   from, to;       ///< 世界坐标 (平台顶部)
        bool active = true;          ///< 通信是否活跃 (影响数据流动画速度)
        bool connected = true;       ///< 通断状态 (断开时管道变暗/闪烁)
        double data_rate = 1.0;      ///< 数据流速度因子
    };

    const char* id() const override { return kId; }

    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    /// 整体显隐开关 (从世界场景根增删本特性节点)。
    void setEnabled(bool on) override;

    /// 全量刷新平台标注。
    void setGraph(const std::vector<Marker>& markers);
    /// 全量刷新关系链路 (与标注同容器渲染, 随本特性整体显隐)。
    void setLinks(const std::vector<Link>& links);
    void clear();

private:
    void rebuild();
    void setInWorld(bool add);
    /// 每帧: 标注距离 LOD 显隐 + push-constant 时间更新。
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;

    ViewportContext* ctx_ = nullptr;
    vsg::ref_ptr<vsg::Group> root_;
    vsg::ref_ptr<vsg::Font> font_;     ///< 平台标注中文字体 (情性加载)
    std::vector<Marker> markers_;
    std::vector<Link>   links_;
    // 标注 LOD: 公告牌 Switch 包裹 (超距 setAllChildren(false) 摘除)。
    struct LabelRef {
        vsg::ref_ptr<vsg::Switch> node;
        vsg::vec3 pos;
    };
    std::vector<LabelRef> labels_;
    bool in_world_ = false;
    double elapsed_sec_ = 0.0;  ///< 全局时间 (数据流动画)
    /// 自定义 shader 节点 (管道/光晕) 的逐帧更新句柄 + 动画参数。
    struct ShaderRef {
        gui::ViewportKit::ShaderHandle h;
        float param1 = 0.0f;      ///< 通断(管道) / 阵营(光晕)
        float time_scale = 1.0f;  ///< 数据流速度因子 (data_rate)
    };
    std::vector<ShaderRef> shader_refs_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_RELATIONSHIPVISUALFEATURE_H