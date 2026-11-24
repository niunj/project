#pragma once
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
namespace vsgraysim {
// One bounded GPU slot per running world on a single GPU. Equipment instances
// are jobs, not separate copies of the physics world or one GPU context each.
class VoxelEquipmentProcess {
public:
    VoxelEquipmentProcess(earth::VoxelScene&,earth::VoxelEquipmentConfig,nlohmann::json,
                          std::string executable,std::string directory);
    ~VoxelEquipmentProcess();
    void ensureStarted(const std::string& archive);
    void advanceMeasurements(const std::string& archive);
    void idle();
    nlohmann::json diagnostics() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
int runVoxelMeasurementWorker(int argc,char** argv);
}
