#include "SimulationEngineWorker.h"
#include "SimulationService.h"
#include <vsgraysim/broker/VoxelEquipmentProcess.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <QTimer>
#include <csignal>
#include <string_view>

namespace {
volatile std::sig_atomic_t terminateRequested = 0;
void requestTermination(int) { terminateRequested = 1; }
}

int main(int argc, char** argv) {
    using namespace vsgraysim::service;
    if (argc > 1 && std::string_view(argv[1]) == "--measurement-worker")
        return vsgraysim::runVoxelMeasurementWorker(argc, argv);
    if (argc > 1 && std::string_view(argv[1]) == "--engine-worker")
        return runSimulationEngineWorker(argc, argv);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("vsgraysim_sim_service");
    QCommandLineParser options;
    options.setApplicationDescription("VSG authoritative simulation service (loopback)");
    options.addHelpOption();
    options.addOption({QStringLiteral("port"), "HTTP port (0 picks an available port)", "port", "8765"});
    options.addOption({QStringLiteral("project-root"), "Repository/install data root", "directory", QDir::currentPath()});
    options.addOption({QStringLiteral("web-dir"), "Built browser console directory", "directory", "web/sim-console/dist"});
    options.process(app);
    bool ok = false;
    const auto port = options.value("port").toUShort(&ok);
    if (!ok || !QDir(options.value("project-root")).exists("config/service/projects.json"))
        options.showHelp(2);
    SimulationService service(QDir(options.value("project-root")).absolutePath(), options.value("web-dir"));
    if (const auto actual = service.listen(port)) {
        std::signal(SIGTERM, requestTermination);
        std::signal(SIGINT, requestTermination);
        QTimer signals;
        QObject::connect(&signals, &QTimer::timeout, &app, [&app] {
            if (terminateRequested) app.quit();
        });
        signals.start(100);
        QTextStream(stdout) << "LISTENING http://127.0.0.1:" << actual << Qt::endl;
        return app.exec();
    }
    QTextStream(stderr) << "Cannot load project catalog, initialize loopback listener or create a private run directory" << Qt::endl;
    return 1;
}
