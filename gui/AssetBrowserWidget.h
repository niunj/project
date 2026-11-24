#pragma once
/// @file AssetBrowserWidget.h
/// @brief 工作区资产浏览器: 卡片/列表双视图 + 预览缩略图。
///
/// 供 场景设计启动页 / 仿真控制启动页 复用:
///   - 卡片模式 (QListView::IconMode): 大缩略图 + 标题 + 副标题
///   - 列表模式 (QListView::ListMode): 紧凑行
///   - 双击条目发 activated(entry) 信号
#ifndef VSGRAYSIM_GUI_ASSETBROWSERWIDGET_H
#define VSGRAYSIM_GUI_ASSETBROWSERWIDGET_H

#include <QWidget>
#include <QString>
#include <vector>

#include "vsgraysim/workspace/Workspace.h"

class QListView;
class QStandardItemModel;
class QToolButton;

namespace vsgraysim { namespace gui {

/// 解析 GUI 运行时应使用的工作区根目录:
/// 1) <可执行目录>/../workspace  2) cwd/workspace (存在才用)
/// 3) 回退 <可执行目录>/workspace。
QString resolveWorkspaceRoot();

class AssetBrowserWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Cards, List };

    explicit AssetBrowserWidget(QWidget* parent = nullptr);

    void setEntries(const std::vector<vsgraysim::WorkspaceEntry>& entries);
    void setMode(Mode m);
    Mode mode() const { return mode_; }

    /// 当前选中条目 (无则 path 为空)。
    vsgraysim::WorkspaceEntry selected() const;

    /// 选择条目并滚动可见 (供外部初始化)。
    void selectFirst();

signals:
    /// 双击条目。
    void activated(const vsgraysim::WorkspaceEntry& entry);
    /// 当前选中条目变化 (单击/键盘)。
    void selectionChanged(const vsgraysim::WorkspaceEntry& entry);

private:
    void buildUi();
    void refreshModel();
    QIcon iconFor(const vsgraysim::WorkspaceEntry& e) const;
    QString subtitleFor(const vsgraysim::WorkspaceEntry& e) const;

    QListView*           view_ = nullptr;
    QStandardItemModel*  model_ = nullptr;
    QToolButton*         btn_cards_ = nullptr;
    QToolButton*         btn_list_ = nullptr;
    Mode                 mode_ = Mode::Cards;
    std::vector<vsgraysim::WorkspaceEntry> entries_;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_ASSETBROWSERWIDGET_H