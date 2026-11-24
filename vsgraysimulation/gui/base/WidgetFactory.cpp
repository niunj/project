#include "WidgetFactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>
#include <QWheelEvent>
#include <QVariant>

namespace vsgraysim { namespace ui {
namespace {
// Scrolling a settings panel must not change an unfocused parameter.
class FocusDoubleSpinBox : public QDoubleSpinBox {
public: using QDoubleSpinBox::QDoubleSpinBox;
protected: void wheelEvent(QWheelEvent* event) override {
    if(hasFocus()) QDoubleSpinBox::wheelEvent(event); else event->ignore();
}
};
class FocusSpinBox : public QSpinBox {
public: using QSpinBox::QSpinBox;
protected: void wheelEvent(QWheelEvent* event) override {
    if(hasFocus()) QSpinBox::wheelEvent(event); else event->ignore();
}
};
class FocusComboBox : public QComboBox {
public: using QComboBox::QComboBox;
protected: void wheelEvent(QWheelEvent* event) override {
    if(hasFocus()) QComboBox::wheelEvent(event); else event->ignore();
}
};
}

// ---- QDoubleSpinBox ----
QDoubleSpinBox* WidgetFactory::createDoubleSpinBox(
    QWidget* parent, double lo, double hi, double val,
    int decimals, double step, const QString& suffix)
{
    auto* s = new FocusDoubleSpinBox(parent);
    s->setFocusPolicy(Qt::StrongFocus);
    s->setKeyboardTracking(false);
    s->setAccessibleName(suffix.isEmpty() ? QObject::tr("数值参数") : QObject::tr("数值参数 (%1)").arg(suffix.trimmed()));
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setValue(val);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}

// ---- QSpinBox ----
QSpinBox* WidgetFactory::createSpinBox(
    QWidget* parent, int lo, int hi, int val, const QString& suffix)
{
    auto* s = new FocusSpinBox(parent);
    s->setFocusPolicy(Qt::StrongFocus);
    s->setKeyboardTracking(false);
    s->setAccessibleName(suffix.isEmpty() ? QObject::tr("整数参数") : QObject::tr("整数参数 (%1)").arg(suffix.trimmed()));
    s->setRange(lo, hi);
    s->setValue(val);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}

// ---- QComboBox ----
QComboBox* WidgetFactory::createComboBox(
    QWidget* parent, const std::vector<QString>& items, int default_index)
{
    auto* c = new FocusComboBox(parent);
    c->setFocusPolicy(Qt::StrongFocus);
    c->setAccessibleName(QObject::tr("参数选项"));
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(12);
    for (const auto& item : items)
        c->addItem(item);
    if (default_index >= 0 && default_index < int(items.size()))
        c->setCurrentIndex(default_index);
    return c;
}

// ---- QSlider ----
QSlider* WidgetFactory::createSlider(
    QWidget* parent, int lo, int hi, int val, Qt::Orientation orient)
{
    auto* s = new QSlider(orient, parent);
    s->setRange(lo, hi);
    s->setValue(val);
    s->setFocusPolicy(Qt::StrongFocus);
    s->setAccessibleName(QObject::tr("范围参数"));
    return s;
}

// ---- QPushButton ----
QPushButton* WidgetFactory::createButton(
    QWidget* parent, const QString& text, bool prominent)
{
    auto* b = new QPushButton(text, parent);
    b->setAccessibleName(text);
    b->setAutoDefault(false);
    b->setProperty("prominent",prominent);
    return b;
}

// ---- QRadioButton ----
QRadioButton* WidgetFactory::createRadio(
    QWidget* parent, const QString& text)
{
    auto* radio=new QRadioButton(text,parent);
    radio->setAccessibleName(text);
    return radio;
}

// ---- QLabel (monospace) ----
QLabel* WidgetFactory::createMonospaceLabel(
    QWidget* parent, const QString& text, const QString& color)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lbl->setStyleSheet(
        QString("color:%1; font-family:monospace;").arg(color));
    return lbl;
}

// ---- QCheckBox ----
QCheckBox* WidgetFactory::createCheckBox(
    QWidget* parent, const QString& text, bool checked)
{
    auto* c = new QCheckBox(text, parent);
    c->setAccessibleName(text);
    c->setChecked(checked);
    return c;
}

}} // namespace vsgraysim::ui
