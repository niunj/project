#pragma once
/// @file SceneStartDialog.h
/// @brief 场景设计启动页: 打开已有场景编辑 / 从场景模板新建。
///
/// 两个标签页共用 AssetBrowserWidget (卡片/列表 + 预览):
///   - 已有场景: 扫描 workspace/scenes/ 的场景文件, 选中后打开编辑修改;
///   - 场景模板: 扫描 workspace/terrains/ 的地形工程 (地形编辑器导出),
///     选中后以"地形 + 目标场景树"快速创建新场景。
/// 确定按钮文案随当前标签变化 ("打开" / "创建")。
#ifndef VSGRAYSIM_GUI_SCENE_START_DIALOG_H
#define VSGRAYSIM_GUI_SCENE_START_DIALOG_H

#include <QDialog>
#include <QString>

#include "vsgraysim/workspace/Workspace.h"

class QLabel;
class QTabWidget;

namespace vsgraysim { namespace gui {

class AssetBrowserWidget;

class SceneStartDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { None, NewFromTerrain, OpenScenario };

    explicit SceneStartDialog(QWidget* parent = nullptr);

    Choice choice() const { return choice_; }
    /// NewFromTerrain: 地形工程完整路径
    QString terrainProject() const { return terrain_project_; }
    /// OpenScenario: 场景文件完整路径
    QString scenarioPath() const { return scenario_path_; }

private slots:
    void onActivated(const vsgraysim::WorkspaceEntry& e);
    void onAccept();
    void onTabChanged(int idx);

private:
    void updateDetails(const vsgraysim::WorkspaceEntry& e);

    QTabWidget* tabs_ = nullptr;
    AssetBrowserWidget* terrains_ = nullptr;
    AssetBrowserWidget* scenes_ = nullptr;
    QLabel* details_ = nullptr;      ///< 右侧详情预览面板
    QString terrain_project_;
    QString scenario_path_;
    Choice  choice_ = Choice::None;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_SCENE_START_DIALOG_H