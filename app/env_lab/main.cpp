/**
 * @file env_lab/main.cpp
 * @brief rocky_env 环境模块验证程序入口 (sky / cloud / ocean)。
 *
 * 独立的 VSG+Qt 测试台: 可分别或组合加载三个环境模块, 通过控制面板驱动
 * 其全部 Control/Query 接口, 验证功能与显示效果。不依赖工程文件与 rocky
 * 地理数据 (平面模式启动), 便于快速迭代环境模块。
 */

#include <QApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QTimer>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <filesystem>
#include <unistd.h>

#include "EnvLabWindow.h"

#ifndef QT6_PLUGIN_PATH
#  define QT6_PLUGIN_PATH ""
#endif

/// 优先使用 NVIDIA 独显 Vulkan ICD (与其它 vsgraysim 程序一致)。
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

int main(int argc, char** argv) {
    // 工作目录定位: 向上寻找含 config/ + models/ 的工程根 (资产/shaders 相对解析)。
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
    app.setApplicationName("vsgraysim_env_lab");
    app.setOrganizationName("vsgraysim");
    app.setApplicationDisplayName("VSG Environment Lab");

    {
        spdlog::init_thread_pool(8192, 1);
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::async_logger>(
            "env_lab", sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger->set_level(spdlog::level::info);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
    }

    spdlog::info("=== rocky_env Environment Lab ===");

    int rc;
    {
        vsgraysim::viewport3d::EnvLabWindow window;
        window.show();
        spdlog::info("Env Lab entering event loop");
        rc = app.exec();
        spdlog::info("Env Lab exiting");
    }   // 窗口先析构, 之后才能关异步日志线程池。

    spdlog::shutdown();
    return rc;
}
