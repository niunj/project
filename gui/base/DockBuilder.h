#pragma once
/// @file DockBuilder.h
/// @brief Dock 构建辅助：标准化 "QDockWidget + QScrollArea + QVBoxLayout" 模式。
///
/// 所有产品软件通过 DockBuilder 创建分类停靠面板，确保统一的 Dock 外观和行为。

#include <Qt>
#include <QString>

class QDockWidget;
class QVBoxLayout;
class QWidget;

namespace vsgraysim { namespace ui {

/// Dock 构建辅助静态方法集合。
struct DockBuilder {

    /// 创建一个标准分类 Dock，含滚动区域。
    /// @param parent    QMainWindow 或 QWidget 父对象
    /// @param title     Dock 标题
    /// @param area      停靠区域 (默认左侧)
    /// @param min_width 最小宽度 (0=不限制)
    /// @return          Dock 的内部 QVBoxLayout* (向其中添加控件)
    static QVBoxLayout* createDock(
        QWidget* parent,
        const QString& title,
        Qt::DockWidgetArea area = Qt::LeftDockWidgetArea,
        int min_width = 0,
        const QString& persistent_id = {});

    /// 在 layout 中添加一个 QGroupBox 分区。
    /// @param layout  目标布局
    /// @param title   分区的标题
    /// @return        分区内的 QVBoxLayout* (向其中添加控件和子布局)
    static QVBoxLayout* addSection(QVBoxLayout* layout, const QString& title);
};

}} // namespace vsgraysim::ui
