#include "ThemeManager.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QSettings>

namespace vsgraysim { namespace ui {

QString ThemeManager::styleSheet_;

void ThemeManager::applyDarkTheme(QApplication* app)
{
    if (!app) return;
    if(app->property("vsgraysimProductTheme").toBool()) return;
    app->setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window,QColor("#151b23"));
    palette.setColor(QPalette::WindowText,QColor("#dfe7f3"));
    palette.setColor(QPalette::Base,QColor("#10151d"));
    palette.setColor(QPalette::AlternateBase,QColor("#18202b"));
    palette.setColor(QPalette::Text,QColor("#dfe7f3"));
    palette.setColor(QPalette::Button,QColor("#243044"));
    palette.setColor(QPalette::ButtonText,QColor("#dfe7f3"));
    palette.setColor(QPalette::Highlight,QColor("#235584"));
    palette.setColor(QPalette::HighlightedText,Qt::white);
    palette.setColor(QPalette::ToolTipBase,QColor("#253146"));
    palette.setColor(QPalette::ToolTipText,QColor("#eaf2fc"));
    palette.setColor(QPalette::PlaceholderText,QColor("#8a95a8"));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor("#788598"));
    palette.setColor(QPalette::Disabled,QPalette::WindowText,QColor("#788598"));
    palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor("#788598"));
    app->setPalette(palette);
    app->setProperty("vsgraysimProductTheme",true);
    setCompactDensity(app,compactDensity());
}

bool ThemeManager::compactDensity()
{
    return QSettings(QStringLiteral("vsgraysim"),QStringLiteral("ProductUi"))
        .value(QStringLiteral("appearance/compact"),false).toBool();
}

void ThemeManager::setCompactDensity(QApplication* app,bool compact)
{
    if(!app) return;
    QSettings settings(QStringLiteral("vsgraysim"),QStringLiteral("ProductUi"));
    settings.setValue(QStringLiteral("appearance/compact"),compact);
    QString qss=darkStyleSheet();
    if(compact) qss+=QStringLiteral(
        "QPushButton { padding:3px 9px; min-height:18px; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { padding:2px 5px; min-height:18px; }"
        "QTreeView::item, QTableView::item { min-height:20px; padding:2px 4px; }"
        "QToolButton { padding:3px 7px; } QTabBar::tab { padding:5px 10px; }");
    app->setStyleSheet(qss);
}

const QString& ThemeManager::darkStyleSheet()
{
    if (styleSheet_.isEmpty())
        styleSheet_ = buildStyleSheet();
    return styleSheet_;
}

QString ThemeManager::buildStyleSheet()
{
    return QStringLiteral(R"QSS(
QMainWindow, QDialog { background: #1a1d24; color: #d7dbe2; }
QMenuBar { background: #14161c; color: #d7dbe2; border-bottom: 1px solid #262b36; }
QMenuBar::item:selected { background: #2d6fb8; }
QMenu { background: #1c2028; color: #d7dbe2; border: 1px solid #2a3040; }
QMenu::item:selected { background: #2d6fb8; }
QStatusBar { background: #14161c; color: #9aa5b8; border-top: 1px solid #262b36; }
QToolBar { background: #14161c; border: none; padding: 2px; spacing: 4px; }
QToolButton { color: #d7dbe2; padding: 4px 10px; border-radius: 4px; }
QToolButton:hover { background: #262b36; }
QToolButton:checked { background: #2d6fb8; }
QGroupBox { border: 1px solid #2a3040; border-radius: 6px; margin-top: 10px;
            padding-top: 8px; font-weight: bold; color: #7fd0ff; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
QLabel { color: #c6ccd8; }
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit, QListWidget, QTabWidget::pane {
  background: #10131a; color: #d7dbe2; border: 1px solid #2a3040;
  border-radius: 4px; selection-background-color: #2d6fb8; }
QComboBox QAbstractItemView { background: #1c2028; color: #d7dbe2;
  selection-background-color: #2d6fb8; }
QPushButton { background: #243044; color: #dbe4f0; border: 1px solid #34425c;
  border-radius: 4px; padding: 5px 12px; }
QPushButton:hover { background: #2e3d57; }
QPushButton:pressed { background: #3a4c6e; }
QPushButton:disabled { color: #5d6675; background: #1d222c; }
QCheckBox, QRadioButton { color: #c6ccd8; spacing: 5px; }
QCheckBox::indicator, QRadioButton::indicator { width: 15px; height: 15px; }
QSlider::groove:horizontal { height: 5px; background: #2a3040; border-radius: 2px; }
QSlider::handle:horizontal { width: 13px; margin: -5px 0; border-radius: 6px;
  background: #7fd0ff; }
QTabBar::tab { background: #1c2028; color: #8a93a3; padding: 7px 14px;
  border-top-left-radius: 5px; border-top-right-radius: 5px; margin-right: 2px; }
QTabBar::tab:selected { background: #2d6fb8; color: white; }
QTabWidget::pane { border: 1px solid #2a3040; }
QListWidget::item { padding: 4px; }
QListWidget::item:selected { background: #2d6fb8; color: white; }
QListView { background: #10131a; color: #d7dbe2; border: 1px solid #2a3040;
  border-radius: 4px; outline: 0; }
QListView::item { border-radius: 4px; }
QListView::item:hover { background: #262b36; }
QListView::item:selected { background: #2d6fb8; color: white; }
QTreeWidget { background: #10131a; color: #d7dbe2; border: 1px solid #2a3040;
  border-radius: 4px; }
QTreeWidget::item { padding: 3px; }
QTreeWidget::item:selected { background: #2d6fb8; color: white; }
QHeaderView::section { background: #14161c; color: #9aa5b8;
  border: 1px solid #2a3040; padding: 3px 6px; }
QDockWidget { color: #e8ecf3; font-size: 12.5px; }
QDockWidget::title { background: #202a3a; color: #e8ecf3;
  border: 1px solid #2a3650; border-radius: 5px; padding: 6px 10px;
  text-align: left; }
QDockWidget::title:hover { background: #253146; }
QDockWidget::close-button, QDockWidget::float-button {
  background: transparent; border: none; padding: 2px; }
QDockWidget::close-button:hover, QDockWidget::float-button:hover {
  background: #2d6fb8; border-radius: 3px; }
QDockWidget > QWidget { background: #1a1d24; }
QScrollBar { background: #14161c; }
/* Shared product controls: tables, keyboard focus and readable empty states. */
QWidget { selection-background-color:#235584; selection-color:#ffffff; }
QMainWindow::separator { width:5px; height:5px; background:#1a2230; }
QMainWindow::separator:hover, QSplitter::handle:hover { background:#4389b8; }
QSplitter::handle { background:#232d3b; }
QToolTip { background:#253146; color:#eaf2fc; border:1px solid #526782; padding:6px 9px; }
QStatusBar::item { border:none; }
QToolBar { spacing:6px; padding:5px 8px; }
QToolBar::separator { background:#344052; width:1px; margin:4px 5px; }
QPushButton { min-height:24px; padding:5px 12px; }
QPushButton[prominent="true"] { background:#205b88; border-color:#438ebc; font-weight:600; }
QPushButton[prominent="true"]:hover { background:#286e9f; }
QPushButton[intent="danger"] { color:#ffc7c7; border-color:#884949; background:#442b33; }
QPushButton:focus, QToolButton:focus, QComboBox:focus, QLineEdit:focus,
QSpinBox:focus, QDoubleSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus,
QTableView:focus, QTreeView:focus, QListView:focus { border:1px solid #79c7ee; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height:24px; padding:4px 7px; }
QLineEdit[readOnly="true"] { background:#1a2230; color:#abbacf; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
  background:#1b212c; color:#788598; border-color:#2a3342; }
QComboBox::drop-down { width:24px; border-left:1px solid #2a3547; }
QComboBox QAbstractItemView { outline:0; padding:4px; border:1px solid #41546b; }
QTextEdit, QPlainTextEdit, QAbstractItemView { background:#10151d; color:#dfe7f3;
  alternate-background-color:#18202b; border:1px solid #2a3547; border-radius:4px; }
QTreeView::item, QTableView::item { min-height:25px; padding:3px 5px; }
QTreeView::item:hover, QTableView::item:hover { background:#223047; }
QTreeView::item:selected, QTableView::item:selected { background:#235584; color:#ffffff; }
QHeaderView::section { background:#1a2230; color:#b8c7da; border:none;
  border-right:1px solid #2a3547; border-bottom:1px solid #2a3547; padding:6px 8px; }
QTableView { gridline-color:#263243; }
QTableCornerButton::section { background:#1a2230; border:1px solid #2a3547; }
QGroupBox { background:#18202b; font-weight:600; color:#b9d7ee; }
QDockWidget::title { background:#202c3d; border-radius:0; border:1px solid #2c3b50; padding:7px 10px; }
QDockWidget > QWidget { background:#151b23; }
QWidget#dockHost { background:#151b23; }
QScrollArea { border:none; background:transparent; }
QTabBar::tab { background:#1a2330; color:#aab8ca; padding:8px 14px;
  border-radius:0; border-bottom:2px solid transparent; margin-right:1px; }
QTabBar::tab:hover { background:#253347; }
QTabBar::tab:selected { background:#263e57; color:#f0f8ff; border-bottom-color:#7bc8ec; }
QLabel[uiRole="title"] { color:#eef7ff; font-size:18px; font-weight:600; }
QLabel[uiRole="muted"] { color:#9aadc3; }
QLabel[status="online"] { color:#8dd9b3; }
QLabel[status="offline"] { color:#9aadc3; }
QLabel[status="warning"] { color:#f1ca8b; }
QProgressBar { background:#10151d; color:#dfe7f3; border:1px solid #2a3547; border-radius:3px; text-align:center; }
QProgressBar::chunk { background:#367da9; }
QScrollBar:vertical { width:11px; margin:1px; background:transparent; }
QScrollBar:horizontal { height:11px; margin:1px; background:transparent; }
QScrollBar::handle:vertical { min-height:28px; background:#35465d; border-radius:4px; }
QScrollBar::handle:horizontal { min-width:28px; background:#35465d; border-radius:4px; }
QScrollBar::handle:hover { background:#4a6382; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QScrollBar::add-page, QScrollBar::sub-page { background:transparent; }
)QSS");
}

}} // namespace vsgraysim::ui
