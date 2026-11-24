#ifdef VSGRAYSIM_FORMAL_VALIDATION
#include "testing/FormalChainValidation.h"
#endif
/**
 * @file sim_control/main.cpp
 * @brief 仿真控制软件入口。
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

#include "SimulationControlWindow.h"
#include "SimulationEngineWorker.h"
#include <vsgraysim/broker/VoxelEquipmentProcess.h>
#include "SimStartDialog.h"
#include <telemetry/TelemetryManager.h>

#ifndef QT6_PLUGIN_PATH
#  define QT6_PLUGIN_PATH ""
#endif

static void preferNvidiaVulkanIcd() {
    // 标准 loader 环境变量为 VK_ICD_FILENAMES (限制仅加载 nvidia ICD,
    // 避免多 ICD 环境下设备选择不确定导致的 swapchain 创建失败)。
    if (const char* existing = std::getenv("VK_ICD_FILENAMES")) {
        if (std::string(existing).find("nvidia") != std::string::npos) return;
    }
    const char* candidates[] = {
        "/usr/share/vulkan/icd.d/nvidia_icd.json",
        "/etc/vulkan/icd.d/nvidia_icd.json",
        nullptr
    };
    for (auto* p = candidates; *p; ++p) {
        if (access(*p, R_OK) == 0) {
            setenv("VK_ICD_FILENAMES", *p, 0);
            break;
        }
    }
}

int main(int argc, char** argv) {
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
    if(argc>1&&std::string(argv[1])=="--measurement-worker")
        return vsgraysim::runVoxelMeasurementWorker(argc,argv);

    if(argc>1&&std::string(argv[1])=="--engine-worker")
        return vsgraysim::gui::runSimulationEngineWorker(argc,argv);

    if (QT6_PLUGIN_PATH[0] != 0)
        qputenv("QT_PLUGIN_PATH", QT6_PLUGIN_PATH);
    qputenv("QT_QPA_PLATFORM", "xcb");

    QApplication app(argc, argv);
    app.setApplicationName("vsgraysim_sim_control");
    app.setOrganizationName("vsgraysim");
    app.setApplicationDisplayName("VSG Simulation Control");

    {
        // 异步日志: 热路径 (每帧 info) 不再阻塞仿真线程, 后台线程落盘。
        spdlog::init_thread_pool(8192, 1);
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::async_logger>(
            "sim_control", sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger->set_level(spdlog::level::info);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
    }

    spdlog::info("=== Simulation Control ===");

    // 遥测初始化
    auto telemetry = std::make_shared<vsgraysim::telemetry::TelemetryManager>();
    telemetry->init("sim_control");

    // 初始化 CPU 基准
    vsgraysim::telemetry::TelemetryManager::readCpuPercent(true);

    // 定时健康上报 (CPU/内存)
    QTimer* health_timer = new QTimer();
    QObject::connect(health_timer, &QTimer::timeout, [telemetry]() {
        telemetry->setCpuPercent(
            vsgraysim::telemetry::TelemetryManager::readCpuPercent());
        telemetry->setMemBytes(
            vsgraysim::telemetry::TelemetryManager::readMemoryBytes());
    });
    health_timer->start(2000);

    // 支持命令行传入场景文件路径
    QString scenario_path;
    if (argc > 1)
        scenario_path = QString::fromUtf8(argv[1]);

    // 无场景参数时, 先弹出"选择模板和工程"启动页
    if (scenario_path.isEmpty()) {
        vsgraysim::gui::SimStartDialog dlg;
        if (dlg.exec() == QDialog::Accepted)
            scenario_path = dlg.scenarioPath();
    }

    int rc;
    {
        vsgraysim::gui::SimulationControlWindow window;
        window.setTelemetryManager(telemetry.get());
        window.show();

        if (!scenario_path.isEmpty())
            window.loadScenarioFile(scenario_path);

#ifdef VSGRAYSIM_FORMAL_VALIDATION
        vsgraysim::gui::installFormalValidation(window);
#endif

        // 冒烟验证钩子: VSGRAYSIM_SMOKE_MS 设置后, 装载完成自动播放指定毫秒,
        // 随后暂停并正常退出 (供无人值守回归验证装载/推演链路)。
        if (const char* env_smoke = std::getenv("VSGRAYSIM_SMOKE_MS")) {
            const int smoke_ms = std::atoi(env_smoke);
            spdlog::info("SMOKE: auto-play {} ms then quit", smoke_ms);
            QTimer::singleShot(1000, [&window]() {
                QMetaObject::invokeMethod(&window, "onPlayPause");
            });
            QTimer::singleShot(1000 + smoke_ms, [&app, &window]() {
                QMetaObject::invokeMethod(&window, "onPlayPause");   // 暂停
                app.quit();
            });
        }

        spdlog::info("Simulation Control entering event loop");
        rc = app.exec();

        health_timer->stop();
        telemetry->shutdown();
        spdlog::info("Simulation Control exiting");
    }   // 窗口先析构 (析构链会打日志), 之后才能关异步日志线程池。
    spdlog::shutdown();
    return rc;
}
