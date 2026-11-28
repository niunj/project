#pragma once
namespace vsgraysim::service {
// Headless world entry shared by the service and the legacy executable.
int runSimulationEngineWorker(int argc,char** argv);
}
