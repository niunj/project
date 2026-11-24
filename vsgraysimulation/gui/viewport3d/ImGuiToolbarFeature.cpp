/// @file ImGuiToolbarFeature.cpp
/// @brief 见 ImGuiToolbarFeature.h。
#include "ImGuiToolbarFeature.h"

#include "FeatureRegistry.h"
#include "IViewportFeature.h"
#include "ViewportContext.h"
#include "VsgQtWidget.h"

#include <vsgImGui/imgui.h>

namespace vsgraysim { namespace viewport3d {

namespace {
/// 默认工具项 (未 configure 时兜底: 只控关系可视化)。
const std::vector<ImGuiToolbarFeature::Toggle> kDefaultToggles = {
    {"relationship_visual", "标注/连线"},
};
}

void ImGuiToolbarFeature::configure(const nlohmann::json& params)
{
    toggles_.clear();
    title_ = params.value("title", std::string("显示控制"));
    const auto arr = params.value("toggles", nlohmann::json::array());
    for (const auto& t : arr) {
        if (!t.is_object() || !t.contains("id")) continue;
        Toggle tg;
        tg.id = t.value("id", std::string());
        tg.label = t.value("label", tg.id);
        if (!tg.id.empty()) toggles_.push_back(std::move(tg));
    }
}

void ImGuiToolbarFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    if (toggles_.empty()) toggles_ = kDefaultToggles;
    if (auto* w = ctx.widget()) {
        // 每帧 ImGui 绘制 (渲染线程回调)。捕获存活哨兵副本: 本对象析构
        // (onDetach) 后置 false, 残留回调直接返回 false 跳过绘制。
        auto alive = imgui_alive_;
        w->addImGuiDraw([this, alive]() -> bool {
            if (!alive || !*alive) return false;
            draw();
            return true;
        });
    }
}

void ImGuiToolbarFeature::onDetach(ViewportContext& ctx)
{
    (void)ctx;
    *imgui_alive_ = false;   // 作废残留 ImGui 回调 (见头文件说明)
    ctx_ = nullptr;
}

void ImGuiToolbarFeature::draw()
{
    if (!ctx_) return;

    // ---- 定位/尺寸: 首帧定位到左上角 (只定位一次, 用户可拖动) ----
    if (!placed_) {
        ImGui::SetNextWindowPos(ImVec2(14.0f, 14.0f));
        placed_ = true;
    }
    ImGui::SetNextWindowBgAlpha(0.90f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 4.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

    bool open = true;
    if (ImGui::Begin(title_.c_str(), &open, flags)) {
        // ---- 标题行: 强调色标题 (» 为 Latin-1 项目符号, CJK 字体覆盖) ----
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.72f, 1.00f, 1.0f));
        ImGui::TextUnformatted("\xC2\xBB 三维图层");   // » + 标题
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator,
                              ImVec4(0.35f, 0.43f, 0.55f, 0.55f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        // ---- 开关行: 状态点 + 可点击标签 (on=亮绿点, off=灰点) ----
        for (const auto& tg : toggles_) {
            IViewportFeature* f = ctx_->findFeature(tg.id);
            if (!f) continue;
            const bool on = f->enabled();
            // 状态点颜色 (业务语义: 图层点亮/熄灭)。
            const ImVec4 dot = on ? ImVec4(0.35f, 0.90f, 0.49f, 1.0f)   // 绿
                                  : ImVec4(0.33f, 0.36f, 0.42f, 1.0f);  // 灰
            const ImVec4 txt = on ? ImVec4(0.92f, 0.95f, 1.00f, 1.0f)
                                  : ImVec4(0.55f, 0.60f, 0.68f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, dot);
            ImGui::TextUnformatted("\xC2\xB7");          // · 状态点
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 5.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, txt);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                  ImVec4(0.25f, 0.42f, 0.72f, 0.35f));
            if (ImGui::Selectable(tg.label.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns))
                f->setEnabled(!on);          // 运行时显隐切换
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    open_ = open;
}

VSGVP_REGISTER_FEATURE(ImGuiToolbarFeature);

}} // namespace vsgraysim::viewport3d