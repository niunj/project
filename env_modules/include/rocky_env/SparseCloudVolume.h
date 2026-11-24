#pragma once

#include <vsg/maths/vec3.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rockyenv {

// Local high resolution cloud state. Procedural noise is deliberately absent:
// callers inject humidity/thermal observations and the volume evolves them.
struct CloudVoxelState {
    float liquidWaterKgM3=0.f;       // liquid water content, kg/m^3
    float vapourMixingRatio=0.f;     // water vapour / dry air, kg/kg
    float temperatureOffsetK=0.f;    // kelvin relative to ambient profile
    vsg::vec3 velocity{};        // metres per second in local ENU
};

struct CloudBrickCoord {
    int x=0,y=0,z=0;
    bool operator==(const CloudBrickCoord& rhs) const {
        return x==rhs.x && y==rhs.y && z==rhs.z;
    }
};

struct SparseCloudConfig {
    float voxelSizeMetres=40.f;
    uint32_t brickSide=8;
    uint32_t maxBricks=4096;
    float minimumActiveWaterKgM3=1e-7f;
    float referenceLiquidWaterKgM3=.003f; // atlas value 1.0
};

struct CloudAirState {
    float temperatureK;
    float densityKgM3;
    float saturationMixingRatio;
};

struct CloudDynamics {
    vsg::vec3 ambientWind{};
    float ambientVapourMixingRatio=.010f;
    float seaLevelTemperatureK=288.15f;
    float seaLevelPressurePa=101325.f;
    float lapseRateKPerMetre=.0065f;
    float gravity=9.80665f;
    float condensationRate=1.8f;
    float evaporationRate=.35f;
    float buoyancy=.16f;
    float waterLoading=.07f;
    float vorticity=.22f;
    float velocityDamping=.025f;

    CloudAirState airAt(float altitudeMetres,float temperatureOffsetK) const;
};

struct CloudBrickUpload {
    CloudBrickCoord coord;
    uint64_t revision=0;
    std::vector<uint16_t> density;
};

class SparseCloudVolume {
public:
    explicit SparseCloudVolume(SparseCloudConfig config={});

    void clear();
    void setVoxel(int x,int y,int z,const CloudVoxelState& state);
    void setBrick(const CloudBrickCoord&,const std::vector<CloudVoxelState>& states);
    CloudVoxelState voxel(int x,int y,int z) const;
    CloudVoxelState sample(const vsg::vec3& localMetres) const;
    float density(const vsg::vec3& localMetres) const;
    bool resident(const vsg::vec3& localMetres) const;

    // Adds observed moist, warm air. This is a state injection, not a noise
    // function; weather ingestion can call the same API with measured fields.
    void injectThermal(const vsg::vec3& centreMetres,float radiusMetres,
                       float vapour,float temperatureOffset,
                       const vsg::vec3& velocity={},float initialLiquidWater=0.f);
    void step(float dt,const CloudDynamics& dynamics);

    std::vector<CloudBrickUpload> consumeDirtyBricks();
    std::vector<CloudBrickUpload> brickSnapshot() const;
    size_t activeBrickCount() const;
    size_t activeVoxelCount() const;
    uint64_t revision() const;
    const SparseCloudConfig& config() const { return config_; }

private:
    struct Brick {
        std::vector<CloudVoxelState> voxels;
        uint64_t revision=0;
    };
    struct CoordHash {
        size_t operator()(const CloudBrickCoord& c) const;
    };
    using Bricks=std::unordered_map<CloudBrickCoord,Brick,CoordHash>;

    static int floorDiv(int value,int divisor);
    static int floorMod(int value,int divisor);
    size_t index(int x,int y,int z) const;
    CloudVoxelState voxelUnlocked(const Bricks&,int x,int y,int z) const;
    CloudVoxelState sampleUnlocked(const Bricks&,const vsg::vec3&) const;
    Brick& ensureBrick(Bricks&,const CloudBrickCoord&);

    SparseCloudConfig config_;
    mutable std::mutex mutex_;
    Bricks bricks_;
    std::unordered_map<CloudBrickCoord,uint64_t,CoordHash> dirty_;
    uint64_t revision_=0;
};

} // namespace rockyenv
