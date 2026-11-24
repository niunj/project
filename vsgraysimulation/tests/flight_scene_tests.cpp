#include "base/FlightConfiguration.h"
#include "SceneValidator.h"
#include "base/ProductWorkflow.h"
#include "platform/PlatformMotionFactory.h"
#include <QApplication>
#include <QDir>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    using namespace vsgraysim::gui;
    try {
        SceneObject aircraft;
        aircraft.id = "flight_configuration_fixture";
        aircraft.platform_cat = 1;
        require(SceneValidator::validate({aircraft}, nullptr).errorCount() == 0,
            "default aircraft cannot be handed to simulation");
        aircraft.motion_params = {{"min_speed", 20}, {"cruise_speed", 42}, {"max_speed", 80},
            {"min_altitude", -100}, {"max_altitude", 3000}, {"flight_control_enabled", true}};
        require(SceneValidator::validate({aircraft}, nullptr).errorCount() == 0,
            "valid UAV envelope rejected");
        const auto valid = aircraft.motion_params;
        for (const auto& invalid : std::vector<nlohmann::json>{
            {{"max_speed", 30}}, {{"min_speed", 100}}, {{"min_altitude", 5000}},
            {{"max_roll_angle", 90}}, {{"max_pitch_rate", "20"}}, {{"flight_control_enabled", "true"}}}) {
            aircraft.motion_params = valid;
            aircraft.motion_params.update(invalid);
            require(SceneValidator::validate({aircraft}, nullptr).errorCount() > 0,
                "invalid flight configuration passed product handoff");
        }
        aircraft.motion_params = valid;
        aircraft.motion_authority = "kinematic";
        const auto kinematic = SceneValidator::validate({aircraft}, nullptr);
        require(kinematic.errorCount() == 0 && kinematic.warningCount() > 0,
            "prescribed aircraft did not explain control unavailability");
        require(!validateFlightConfiguration(nlohmann::json::array()).isEmpty(),
            "invalid parameter container silently accepted");
        const auto scenarioPath = QDir::current().absoluteFilePath("workspace/scenes/flight_control_demo.scenario.json");
        std::ifstream input(scenarioPath.toStdString()); nlohmann::json scenario; input >> scenario;
        vsgraysim::platform::PlatformManager manager; manager.initialize({}); uint32_t equipmentId = 1;
        for (const auto& object : scenario.at("objects")) {
            require(!vsgraysim::ui::ProductWorkflow::resolveSceneAsset(
                QString::fromStdString(object.at("mesh_path").get<std::string>()), scenarioPath, QDir::currentPath()).isEmpty(),
                "flight control sample refers to a missing model");
            require(vsgraysim::platform::PlatformMotionFactory::createFromObjectJSON(object, manager, equipmentId) > 0,
                "sample aircraft did not reach platform execution factory");
        }
        require(manager.platformCount() == 2, "flight sample lost one controllable airframe");
        manager.shutdown();
        std::cout << "PASS: scene handoff rejects invalid flight envelopes and explains prescribed control authority\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
