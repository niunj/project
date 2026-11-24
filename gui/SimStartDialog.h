#pragma once
/// @file SimStartDialog.h
/// @brief 仿真控制启动页: 选择已创建好的场景文件装载。
///
/// 仅列出工作区 workspace/scenes/ 下由场景设计器创建保存的场景文件,
/// 选中场景后点击"装载"即用 Simulation::initFromScenario 初始化。
#ifndef VSGRAYSIM_GUI_SIM_START_DIALOG_H
#define VSGRAYSIM_GUI_SIM_START_DIALOG_H

#include <QDialog>
#include <QString>

#include "vsgraysim/workspace/Workspace.h"

class QLabel;

namespace vsgraysim { namespace gui {

class AssetBrowserWidget;

class SimStartDialog : public QDialog {
    Q_OBJECT
public:
    explicit SimStartDialog(QWidget* parent = nullptr);

    /// 选中的场景文件完整路径 (取消则为空)。
    QString scenarioPath() const { return scenario_path_; }

private slots:
    void onActivated(const vsgraysim::WorkspaceEntry& e);
    void onAccept();

private:
    void updateDetails(const vsgraysim::WorkspaceEntry& e);

    AssetBrowserWidget* scenes_ = nullptr;     ///< 已创建的场景文件列表
    QLabel* details_ = nullptr;      ///< 右侧场景详情面板
    QString scenario_path_;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_SIM_START_DIALOG_H