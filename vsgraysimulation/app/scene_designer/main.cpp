#ifdef VSGRAYSIM_FORMAL_VALIDATION
#include "testing/FormalChainValidation.h"
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#endif
/**
 * @file scene_designer/main.cpp
 * @brief 场景设计软件入口。
 */

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSurfaceFormat>
#include <QTimer>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <filesystem>
#include <unistd.h>

#include "SceneDesignerWindow.h"
#include "SceneStartDialog.h"
#include "voxel/ModelLibraryDialog.h"
#include "voxel/SharedSceneWorkspace.h"
#include <telemetry/TelemetryManager.h>

#ifndef QT6_PLUGIN_PATH
#  define QT6_PLUGIN_PATH ""
#endif

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
    app.setApplicationName("vsgraysim_scene_designer");
    app.setOrganizationName("vsgraysim");
    app.setApplicationDisplayName("VSG Scene Designer");
    if (app.arguments().contains("--model-library")) {
        vsgraysim::gui::ModelLibraryDialog library;
        library.show();
#ifdef VSGRAYSIM_FORMAL_VALIDATION
        if (const auto screenshot=qEnvironmentVariable("VSGRAYSIM_MODEL_LIBRARY_PROBE"); !screenshot.isEmpty()) {
            QTimer::singleShot(25000,&app,[&app]{app.exit(3);});
            auto* probe=new QTimer(&library);
            const auto temperature=qEnvironmentVariable("VSGRAYSIM_MODEL_LIBRARY_PROBE_TEMPERATURE");
            QObject::connect(probe,&QTimer::timeout,&library,[&library,&app,probe,phase=0,screenshot,temperature]() mutable {
                auto* preview=library.findChild<QLabel*>("libraryModelImage");
                if (!preview || preview->pixmap().isNull()) return;
                if (phase==0) {
                    library.findChild<QDoubleSpinBox*>("libraryYaw")->setValue(12.);
                    if (!temperature.isEmpty()) library.findChild<QLineEdit*>("temperature_k")->setText(temperature);
                    library.findChild<QPushButton*>("librarySave")->click();
                    phase=1;
                    return;
                }
                const auto yaw=library.findChild<QDoubleSpinBox*>("libraryYaw")->value();
                if (yaw==12. && library.findChild<QPushButton*>("librarySave")->isEnabled() &&
                    (temperature.isEmpty() || library.findChild<QLineEdit*>("temperature_k")->text()==temperature)) {
                    probe->stop();
                    app.exit(library.grab().save(screenshot) ? 0 : 4);
                }
            });
            probe->start(200);
        }
#endif
        return app.exec();
    }
    if (argc == 1 || app.arguments().contains("--collaborate")) {
        const auto args = app.arguments();
        const auto index = args.indexOf("--collaborate");
        const auto endpoint = index >= 0 && index + 1 < args.size() ? args.at(index + 1)
            : qEnvironmentVariable("VSGRAYSIM_SERVICE_URL","http://127.0.0.1:8765");
        qputenv("VSGRAYSIM_SERVICE_URL",endpoint.toUtf8());
        return vsgraysim::gui::runSharedSceneWorkspace(app,endpoint);
    }

    {
        // 异步日志: 渲染/加载热路径不被日志 I/O 阻塞, 后台线程落盘。
        spdlog::init_thread_pool(8192, 1);
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::async_logger>(
            "scene_designer", sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger->set_level(spdlog::level::info);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
    }

    spdlog::info("=== Scene Designer ===");

    // 遥测初始化
    auto telemetry = std::make_shared<vsgraysim::telemetry::TelemetryManager>();
    telemetry->init("scene_designer");

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

    // 支持命令行参数: 地形模板路径 场景文件路径
    QString scene_topic;
    QString terrain_template;
    QString open_scenario;
    if(argc==3&&QString::fromUtf8(argv[1])=="--scene-topic")scene_topic=QFileInfo(QString::fromUtf8(argv[2])).absoluteFilePath();
    if (scene_topic.isEmpty() && argc > 1 && QString::fromUtf8(argv[1])!="--local") terrain_template = QString::fromUtf8(argv[1]);
    if (scene_topic.isEmpty() && argc > 2) open_scenario    = QString::fromUtf8(argv[2]);

    // 无任何工程参数时, 先弹出"选择模板和工程"启动页
    if (scene_topic.isEmpty() && terrain_template.isEmpty() && open_scenario.isEmpty()) {
        vsgraysim::gui::SceneStartDialog dlg;
        if (dlg.exec() == QDialog::Accepted) {
            if (dlg.choice() == vsgraysim::gui::SceneStartDialog::Choice::NewFromTerrain)
                terrain_template = dlg.terrainProject();
            else if (dlg.choice() == vsgraysim::gui::SceneStartDialog::Choice::OpenScenario)
                open_scenario = dlg.scenarioPath();
        }
    }

    int rc=1;
    try {
        vsgraysim::gui::SceneDesignerWindow window(terrain_template, open_scenario, nullptr,scene_topic);
        window.setTelemetryManager(telemetry.get());
        window.show();
#ifdef VSGRAYSIM_FORMAL_VALIDATION
        vsgraysim::gui::installFormalValidation(window);
#endif

        spdlog::info("Scene Designer entering event loop");
        rc = app.exec();

        health_timer->stop();
        telemetry->shutdown();
        spdlog::info("Scene Designer exiting");
    }catch(const std::exception& error){health_timer->stop();telemetry->shutdown();spdlog::error("Scene startup failed: {}",error.what());QMessageBox::critical(nullptr,QStringLiteral("场景打开失败"),QString::fromUtf8(error.what()));}
    // 窗口先析构 (析构链会打日志), 之后才能关异步日志线程池。
    spdlog::shutdown();
    return rc;
}
