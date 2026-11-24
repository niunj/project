/**
 * @file launcher/main.cpp
 * @brief 进程管理器入口 — 启动/监控/管理所有子进程。
 *
 * 替代原来的 main_gui.cpp, 不再直接创建窗口实例,
 * 而是通过 QProcess 启动各个独立进程。
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QProcess>
#include <QSurfaceFormat>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "LauncherWindow.h"

#ifndef QT6_PLUGIN_PATH
#  define QT6_PLUGIN_PATH ""
#endif

// 与 main_gui.cpp 相同的 NVIDIA ICD 优先逻辑
static void preferNvidiaVulkanIcd() {
    if (const char* existing = std::getenv("VK_ICD_FILE_LIST")) {
        if (std::string(existing).find("nvidia") != std::string::npos) return;
    }
    const char* candidates[] = {
        "/usr/share/vulkan/icd.d/nvidia_icd.json",
        "/etc/vulkan/icd.d/nvidia_icd.json",
        nullptr
    };
    for (auto* p = candidates; *p; ++p) {
        if (access(*p, R_OK) == 0) {
            setenv("VK_ICD_FILE_LIST", *p, 0);
            break;
        }
    }
}

/// 获取可执行文件所在目录 (用于查找子进程可执行文件)
static std::string executableDir() {
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return exe.parent_path().string();
    return ".";
}

int main(int argc, char** argv) {
    // 修正工作目录
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            fs::path dir = exe.parent_path();
            for (int i = 0; i < 8 && !dir.empty(); ++i) {
                if (fs::exists(dir / "config", ec) && fs::exists(dir / "models", ec)) {
                    fs::current_path(dir);
                    break;
                }
                dir = dir.parent_path();
            }
        }
    }

    preferNvidiaVulkanIcd();

    if (QT6_PLUGIN_PATH[0] != 0)
        qputenv("QT_PLUGIN_PATH", QT6_PLUGIN_PATH);
    qputenv("QT_QPA_PLATFORM", "xcb");

    QApplication app(argc, argv);
    app.setApplicationName("vsgraysim_launcher");
    app.setOrganizationName("vsgraysim");
    app.setApplicationDisplayName("VSG Multi-spectral Simulation - Launcher");

    QCommandLineParser parser;
    parser.setApplicationDescription("VSG Multi-spectral Simulation - Process Manager");
    parser.addHelpOption();
    parser.process(app);

    // 日志
    {
        // 异步日志: 进程管理/心跳监控路径不被日志 I/O 阻塞。
        spdlog::init_thread_pool(8192, 1);
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::async_logger>(
            "launcher", sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger->set_level(spdlog::level::info);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
    }

    spdlog::info("=== vsgraysim Launcher ===");

    QApplication::setQuitOnLastWindowClosed(false);

    // 子进程可执行文件目录
    std::string exe_dir = executableDir();

    // Launcher 窗口
    auto* launcher = new vsgraysim::gui::LauncherWindow();
    launcher->setProcessDir(QString::fromStdString(exe_dir));
    launcher->show();

    spdlog::info("Launcher shown - entering Qt event loop");
    int rc = app.exec();

    // 确保所有子进程退出
    launcher->terminateAllProcesses();

    spdlog::info("Launcher exiting");
    spdlog::shutdown();   // 刷新异步日志队列后再退出
    return rc;
}