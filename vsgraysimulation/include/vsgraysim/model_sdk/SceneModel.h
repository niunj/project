#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace vsgraysim::model_sdk {

struct SceneTime {
    uint64_t tick = 0;
    uint64_t stateRevision = 0;
    uint64_t contentRevision = 0;
    double seconds = 0.;
    double stepSeconds = 0.;
};

enum class PlatformFamily { Rigid, Ground, FixedWing, Rotorcraft, Surface, Playback, Submersible };
enum class EquipmentFamily { Optical, Infrared, Radar, Sar, Navigation, Sonar };

enum class InstanceKind { Platform, Equipment };
enum class StatePort { PlatformMotion, EquipmentMount };

struct ModelEndpoint {
    InstanceKind kind;
    uint32_t id;
};

struct StateConnection {
    ModelEndpoint source, destination;
    StatePort port;
};

struct PlatformDescriptor {
    std::string_view id;
    PlatformFamily family;
    std::string_view console;
};

struct EquipmentDescriptor {
    std::string_view id;
    EquipmentFamily family;
    std::string_view console;
    std::string_view band{};
};

using Vec3 = std::array<double,3>;

struct AperturePose {
    Vec3 position, forward, up;
};

// A row-major rotation is kept separate from the translation in the SDK.
struct RigidMount {
    Vec3 position;
    std::array<Vec3,3> rotation;
};

struct PlatformInput {
    SceneTime time;
    Vec3 position, velocity;
    double pitchDegrees, rollDegrees;
};

struct SurfaceMotion {
    double speedMetresPerSecond, heightMetres, pitchDegrees, rollDegrees;
};

struct AirMotion {
    double horizontalSpeedMetresPerSecond, verticalSpeedMetresPerSecond;
    bool nearStationary;
};

struct PlatformSample {
    std::optional<SurfaceMotion> surface;
    std::optional<AirMotion> air;
};

class IPlatformRuntime {
public:
    virtual ~IPlatformRuntime() = default;
    virtual PlatformSample sample(const PlatformInput&) const = 0;
};

struct EquipmentTiming {
    double periodSeconds;
    double integrationSeconds;
};

class IEquipmentRuntime {
public:
    virtual ~IEquipmentRuntime() = default;
    virtual EquipmentTiming timing(double periodSeconds, double exposureSeconds) const = 0;

    static AperturePose resolve(const AperturePose&, const RigidMount&);
};

inline AperturePose IEquipmentRuntime::resolve(const AperturePose& local, const RigidMount& mount)
{
    const auto rotate = [&](const Vec3& v) {
        Vec3 result{};
        for (std::size_t row=0; row<3; ++row)
            for (std::size_t col=0; col<3; ++col)
                result[row] += mount.rotation[row][col]*v[col];
        return result;
    };
    auto position = rotate(local.position);
    for (std::size_t axis=0; axis<3; ++axis)
        position[axis] += mount.position[axis];
    return {position,rotate(local.forward),rotate(local.up)};
}

} // namespace vsgraysim::model_sdk
