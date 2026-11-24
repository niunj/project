#pragma once
/// @file ThemeManager.h
/// @brief 统一暗色主题管理，消除三个产品软件中重复的 QSS 定义。
///
/// 用法: 在 main() 或窗口构造函数开头调用:
///   ThemeManager::applyDarkTheme(qApp);
///
/// 所有产品窗口共享同一套暗色主题，如需定制可在此处集中修改。

#include <QString>

class QApplication;

namespace vsgraysim { namespace ui {

class ThemeManager {
public:
    /// 向 qApp 应用全局暗色 Fusion 主题 + 自定义 QSS。
    static void applyDarkTheme(QApplication* app);

    /// 返回暗色主题 QSS 字符串 (供需要单独设置 QSS 的场景使用)。
    static const QString& darkStyleSheet();
    /// Shared across product executables via the ProductUi settings namespace.
    static bool compactDensity();
    static void setCompactDensity(QApplication* app, bool compact);

private:
    static QString buildStyleSheet();
    static QString styleSheet_;
};

}} // namespace vsgraysim::ui
