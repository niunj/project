#pragma once
/// @file WidgetFactory.h
/// @brief 控件工厂：统一创建常用 Qt 控件，消除各产品软件中重复的控件创建代码。
///
/// 确保所有产品窗口的控件外观一致，控件创建参数集中维护。

#include <QString>
#include <Qt>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QWidget;

namespace vsgraysim { namespace ui {

/// 控件工厂静态方法集合。
struct WidgetFactory {

    // ---- QDoubleSpinBox ----
    static QDoubleSpinBox* createDoubleSpinBox(
        QWidget* parent,
        double lo, double hi, double val,
        int decimals = 2, double step = 1.0,
        const QString& suffix = {});

    // ---- QSpinBox ----
    static QSpinBox* createSpinBox(
        QWidget* parent,
        int lo, int hi, int val,
        const QString& suffix = {});

    // ---- QComboBox ----
    static QComboBox* createComboBox(
        QWidget* parent,
        const std::vector<QString>& items,
        int default_index = 0);

    // ---- QSlider ----
    static QSlider* createSlider(
        QWidget* parent,
        int lo, int hi, int val,
        Qt::Orientation orient = Qt::Horizontal);

    // ---- QPushButton ----
    static QPushButton* createButton(
        QWidget* parent,
        const QString& text,
        bool prominent = false);

    // ---- QRadioButton ----
    static QRadioButton* createRadio(
        QWidget* parent,
        const QString& text);

    // ---- QLabel (monospace style) ----
    static QLabel* createMonospaceLabel(
        QWidget* parent,
        const QString& text = {},
        const QString& color = "#7fd0ff");

    // ---- QCheckBox ----
    static QCheckBox* createCheckBox(
        QWidget* parent,
        const QString& text,
        bool checked = false);
};

}} // namespace vsgraysim::ui
