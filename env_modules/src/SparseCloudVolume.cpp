#include <rocky_env/SparseCloudVolume.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rockyenv {
namespace {
CloudVoxelState mixState(const CloudVoxelState& a,const CloudVoxelState& b,float t) {
    CloudVoxelState r;
    r.liquidWaterKgM3=a.liquidWaterKgM3+(b.liquidWaterKgM3-a.liquidWaterKgM3)*t;
    r.vapourMixingRatio=a.vapourMixingRatio+(b.vapourMixingRatio-a.vapourMixingRatio)*t;
    r.temperatureOffsetK=a.temperatureOffsetK+(b.temperatureOffsetK-a.temperatureOffsetK)*t;
    r.velocity=a.velocity+(b.velocity-a.velocity)*t;
    return r;
}
bool active(const CloudVoxelState& s,float threshold) {
    return s.liquidWaterKgM3>threshold || s.vapourMixingRatio>1e-6f ||
           std::abs(s.temperatureOffsetK)>.01f || vsg::length(s.velocity)>.02f;
}
float saturationMixingRatio(float temperatureK,float pressurePa) {
    const float celsius=temperatureK-273.15f;
    const float vapourPressure=610.94f*std::exp(17.625f*celsius/(celsius+243.04f));
    return .622f*vapourPressure/std::max(pressurePa-vapourPressure,100.f);
}
}

CloudAirState CloudDynamics::airAt(float altitudeMetres,float temperatureOffsetK) const {
    const float altitude=std::max(altitudeMetres,0.f);
    const float temperatureDrop=lapseRateKPerMetre*altitude;
    const float ambientTemperature=std::max(190.f,seaLevelTemperatureK-temperatureDrop);
    const float pressureBase=std::max(.1f,1.f-temperatureDrop/seaLevelTemperatureK);
    const float pressure=seaLevelPressurePa*std::pow(pressureBase,5.25588f);
    const float temperature=std::clamp(ambientTemperature+temperatureOffsetK,190.f,330.f);
    return {temperature,pressure/(287.05f*temperature),saturationMixingRatio(temperature,pressure)};
}

size_t SparseCloudVolume::CoordHash::operator()(const CloudBrickCoord& c) const {
    uint64_t h=uint32_t(c.x)*0x9e3779b1u;
    h^=uint64_t(uint32_t(c.y))*0x85ebca77u+(h<<6)+(h>>2);
    h^=uint64_t(uint32_t(c.z))*0xc2b2ae3du+(h<<6)+(h>>2);
    return size_t(h);
}

SparseCloudVolume::SparseCloudVolume(SparseCloudConfig config):config_(config) {
    if(!std::isfinite(config_.voxelSizeMetres) || config_.voxelSizeMetres<=0 ||
       config_.brickSide<4 || config_.brickSide>32 || config_.maxBricks==0 ||
       !std::isfinite(config_.referenceLiquidWaterKgM3) || config_.referenceLiquidWaterKgM3<=0)
        throw std::invalid_argument("invalid sparse cloud configuration");
}

int SparseCloudVolume::floorDiv(int value,int divisor) {
    int q=value/divisor,r=value%divisor;
    return q-(r<0);
}
int SparseCloudVolume::floorMod(int value,int divisor) {
    int r=value%divisor; return r<0?r+divisor:r;
}
size_t SparseCloudVolume::index(int x,int y,int z) const {
    const size_t s=config_.brickSide;
    return size_t(x)+s*(size_t(y)+s*size_t(z));
}
SparseCloudVolume::Brick& SparseCloudVolume::ensureBrick(Bricks& bricks,const CloudBrickCoord& coord) {
    auto [it,inserted]=bricks.try_emplace(coord);
    if(inserted)it->second.voxels.resize(size_t(config_.brickSide)*config_.brickSide*config_.brickSide);
    return it->second;
}

void SparseCloudVolume::clear() {
    std::lock_guard lock(mutex_);bricks_.clear();dirty_.clear();++revision_;
}
void SparseCloudVolume::setVoxel(int x,int y,int z,const CloudVoxelState& state) {
    std::lock_guard lock(mutex_);
    const int s=int(config_.brickSide);
    CloudBrickCoord coord{floorDiv(x,s),floorDiv(y,s),floorDiv(z,s)};
    if(!bricks_.count(coord) && bricks_.size()>=config_.maxBricks)
        throw std::runtime_error("sparse cloud brick budget exhausted");
    auto& brick=ensureBrick(bricks_,coord);
    brick.voxels[index(floorMod(x,s),floorMod(y,s),floorMod(z,s))]=state;
    brick.revision=++revision_;dirty_[coord]=revision_;
}
void SparseCloudVolume::setBrick(const CloudBrickCoord& coord,const std::vector<CloudVoxelState>& states) {
    const size_t expected=size_t(config_.brickSide)*config_.brickSide*config_.brickSide;
    if(states.size()!=expected)throw std::invalid_argument("sparse cloud brick has wrong voxel count");
    std::lock_guard lock(mutex_);
    if(!bricks_.count(coord) && bricks_.size()>=config_.maxBricks)
        throw std::runtime_error("sparse cloud brick budget exhausted");
    auto& brick=ensureBrick(bricks_,coord);brick.voxels=states;
    brick.revision=++revision_;dirty_[coord]=revision_;
}
CloudVoxelState SparseCloudVolume::voxelUnlocked(const Bricks& bricks,int x,int y,int z) const {
    const int s=int(config_.brickSide);
    CloudBrickCoord coord{floorDiv(x,s),floorDiv(y,s),floorDiv(z,s)};
    auto it=bricks.find(coord);if(it==bricks.end())return {};
    return it->second.voxels[index(floorMod(x,s),floorMod(y,s),floorMod(z,s))];
}
CloudVoxelState SparseCloudVolume::voxel(int x,int y,int z) const {
    std::lock_guard lock(mutex_);return voxelUnlocked(bricks_,x,y,z);
}
CloudVoxelState SparseCloudVolume::sampleUnlocked(const Bricks& bricks,const vsg::vec3& p) const {
    // Voxel state is stored at each cell centre. Convert metres to the same
    // centre-indexed lattice before trilinear reconstruction.
    const vsg::vec3 g=p/config_.voxelSizeMetres-vsg::vec3(.5f,.5f,.5f);
    const int x=int(std::floor(g.x)),y=int(std::floor(g.y)),z=int(std::floor(g.z));
    const float fx=g.x-x,fy=g.y-y,fz=g.z-z;
    CloudVoxelState yz[2][2];
    for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)
        yz[dz][dy]=mixState(voxelUnlocked(bricks,x,y+dy,z+dz),
                            voxelUnlocked(bricks,x+1,y+dy,z+dz),fx);
    const auto a=mixState(yz[0][0],yz[0][1],fy);
    const auto b=mixState(yz[1][0],yz[1][1],fy);
    return mixState(a,b,fz);
}
CloudVoxelState SparseCloudVolume::sample(const vsg::vec3& p) const {
    std::lock_guard lock(mutex_);return sampleUnlocked(bricks_,p);
}
float SparseCloudVolume::density(const vsg::vec3& p) const {
    return std::clamp(sample(p).liquidWaterKgM3/config_.referenceLiquidWaterKgM3,0.f,1.f);
}
bool SparseCloudVolume::resident(const vsg::vec3& p) const {
    std::lock_guard lock(mutex_);
    const int s=int(config_.brickSide);
    const int x=int(std::floor(p.x/config_.voxelSizeMetres));
    const int y=int(std::floor(p.y/config_.voxelSizeMetres));
    const int z=int(std::floor(p.z/config_.voxelSizeMetres));
    return bricks_.count({floorDiv(x,s),floorDiv(y,s),floorDiv(z,s)})!=0;
}
void SparseCloudVolume::injectThermal(const vsg::vec3& centre,float radius,float vapour,
                                      float temperature,const vsg::vec3& velocity,float initialLiquidWater) {
    if(!std::isfinite(radius)||radius<=0)return;
    std::lock_guard lock(mutex_);
    const float h=config_.voxelSizeMetres;
    const int xmin=int(std::floor((centre.x-radius)/h)),xmax=int(std::ceil((centre.x+radius)/h));
    const int ymin=int(std::floor((centre.y-radius)/h)),ymax=int(std::ceil((centre.y+radius)/h));
    const int zmin=int(std::floor((centre.z-radius)/h)),zmax=int(std::ceil((centre.z+radius)/h));
    const int s=int(config_.brickSide);
    for(int z=zmin;z<=zmax;++z)for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x) {
        const vsg::vec3 p(float(x)*h,float(y)*h,float(z)*h);
        const float falloff=std::max(0.f,1.f-vsg::length(p-centre)/radius);
        if(falloff<=0)continue;
        CloudBrickCoord coord{floorDiv(x,s),floorDiv(y,s),floorDiv(z,s)};
        if(!bricks_.count(coord) && bricks_.size()>=config_.maxBricks)continue;
        auto& brick=ensureBrick(bricks_,coord);
        auto& cell=brick.voxels[index(floorMod(x,s),floorMod(y,s),floorMod(z,s))];
        cell.vapourMixingRatio=std::max(cell.vapourMixingRatio,std::clamp(vapour*falloff,0.f,.05f));
        cell.liquidWaterKgM3=std::max(cell.liquidWaterKgM3,
                                     std::clamp(initialLiquidWater*falloff,0.f,.02f));
        cell.temperatureOffsetK=std::max(cell.temperatureOffsetK,temperature*falloff);
        cell.velocity+=velocity*falloff;
        brick.revision=revision_+1;dirty_[coord]=revision_+1;
    }
    ++revision_;
}

void SparseCloudVolume::step(float dt,const CloudDynamics& dynamics) {
    if(!std::isfinite(dt)||dt<=0)return;
    dt=std::min(dt,.25f);
    std::lock_guard lock(mutex_);
    if(bricks_.empty())return;
    const Bricks previous=bricks_;
    Bricks next;
    std::vector<CloudBrickCoord> candidates;
    candidates.reserve(std::min<size_t>(config_.maxBricks,previous.size()*7));
    // Reserve every committed brick first. Filling the budget with empty
    // neighbours in hash order can discard an existing fringe on the next tick.
    for(const auto& [coord,_]:previous)candidates.push_back(coord);
    for(const auto& [c,_]:previous) {
        if(candidates.size()>=config_.maxBricks)break;
        for(const auto& d:std::array<CloudBrickCoord,6>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}}) {
            CloudBrickCoord q{c.x+d.x,c.y+d.y,c.z+d.z};
            if(candidates.size()<config_.maxBricks &&
               std::find(candidates.begin(),candidates.end(),q)==candidates.end())
                candidates.push_back(q);
        }
    }
    const int s=int(config_.brickSide);const float h=config_.voxelSizeMetres;
    const float condenseFraction=1.f-std::exp(-dynamics.condensationRate*dt);
    const float evaporateFraction=1.f-std::exp(-dynamics.evaporationRate*dt);
    const auto curlAxis=vsg::normalize(vsg::vec3(.13f,1.f,.07f)+dynamics.ambientWind*.03f);
    for(const auto& coord:candidates) {
        Brick candidate;candidate.voxels.resize(size_t(s)*s*s);
        bool keep=false;
        for(int z=0;z<s;++z)for(int y=0;y<s;++y)for(int x=0;x<s;++x) {
            const int gx=coord.x*s+x,gy=coord.y*s+y,gz=coord.z*s+z;
            const vsg::vec3 p((gx+.5f)*h,(gy+.5f)*h,(gz+.5f)*h);
            const auto centre=voxelUnlocked(previous,gx,gy,gz);
            // A previously empty cell participates in the background airflow
            // and can receive liquid from an upwind brick on this step.
            const auto velocity=active(centre,config_.minimumActiveWaterKgM3) ?
                centre.velocity : dynamics.ambientWind;
            auto state=sampleUnlocked(previous,p-velocity*dt);
            const std::array<CloudVoxelState,3> low{
                voxelUnlocked(previous,gx-1,gy,gz),voxelUnlocked(previous,gx,gy-1,gz),
                voxelUnlocked(previous,gx,gy,gz-1)};
            const std::array<CloudVoxelState,3> high{
                voxelUnlocked(previous,gx+1,gy,gz),voxelUnlocked(previous,gx,gy+1,gz),
                voxelUnlocked(previous,gx,gy,gz+1)};
            const auto travel=velocity*(dt/h);
            if(std::max({std::abs(travel.x),std::abs(travel.y),std::abs(travel.z)})<=1.f) {
                // Recover local curvature lost by linear backtracing. Limit
                // the increment to the sampled water range to preserve empty
                // gaps and avoid creating a new peak on the rim of a filament.
                const float water=centre.liquidWaterKgM3;
                float correction=0.f;
                float minimum=std::min(water,state.liquidWaterKgM3);
                float maximum=std::max(water,state.liquidWaterKgM3);
                for(int axis=0;axis<3;++axis) {
                    const float fraction=std::abs(travel[axis]);
                    const float a=low[axis].liquidWaterKgM3,b=high[axis].liquidWaterKgM3;
                    correction-=.5f*fraction*(1.f-fraction)*(a+b-2.f*water);
                    const float upwind=travel[axis]>0.f ? a : b;
                    minimum=std::min(minimum,upwind);
                    maximum=std::max(maximum,upwind);
                }
                state.liquidWaterKgM3=std::clamp(state.liquidWaterKgM3+correction,minimum,maximum);
            }
            const auto [temperature,airDensity,saturation]=dynamics.airAt(p.y,state.temperatureOffsetK);
            const float condensedMix=std::max(state.vapourMixingRatio-saturation,0.f)*condenseFraction;
            const float condensed=std::min(state.vapourMixingRatio*airDensity,condensedMix*airDensity);
            const float evaporated=std::min(state.liquidWaterKgM3,
                std::max(saturation-state.vapourMixingRatio,0.f)*airDensity*evaporateFraction);
            state.vapourMixingRatio=std::clamp(state.vapourMixingRatio-
                condensed/std::max(airDensity,.1f)+evaporated/std::max(airDensity,.1f),0.f,.05f);
            state.liquidWaterKgM3=std::clamp((state.liquidWaterKgM3+condensed-evaporated)*
                                             (1.f-.003f*dt),0.f,.02f);
            // Latent heat is part of the prognostic temperature state.
            state.temperatureOffsetK+=(condensed-evaporated)*2.5e6f/
                                       std::max(airDensity*1004.f,1.f);
            state.velocity+=(dynamics.ambientWind-state.velocity)*(.08f*dt);
            const float thermalBuoyancy=dynamics.gravity*state.temperatureOffsetK/temperature;
            const float vapourBuoyancy=dynamics.gravity*.61f*
                (state.vapourMixingRatio-dynamics.ambientVapourMixingRatio);
            const float waterAcceleration=dynamics.gravity*state.liquidWaterKgM3/
                                           std::max(airDensity,.1f);
            state.velocity.y+=(dynamics.buoyancy*(thermalBuoyancy+vapourBuoyancy)-
                               dynamics.waterLoading*waterAcceleration)*dt;
            const auto dx=high[0].velocity-low[0].velocity;
            const auto dy=high[1].velocity-low[1].velocity;
            const auto dz=high[2].velocity-low[2].velocity;
            const vsg::vec3 curlTimesCell{.5f*(dy.z-dz.y),.5f*(dz.x-dx.z),.5f*(dx.y-dy.x)};
            state.velocity+=vsg::cross(curlAxis,curlTimesCell)*(dynamics.vorticity*dt);
            state.velocity*=std::max(0.f,1.f-dynamics.velocityDamping*dt);
            state.temperatureOffsetK*=std::max(0.f,1.f-.035f*dt);
            candidate.voxels[index(x,y,z)]=state;
            keep|=active(state,config_.minimumActiveWaterKgM3);
        }
        if(keep) {
            candidate.revision=revision_+1;
            next.emplace(coord,std::move(candidate));
        }
    }
    bricks_=std::move(next);++revision_;
    dirty_.clear();for(const auto& [coord,_]:bricks_)dirty_[coord]=revision_;
}

std::vector<CloudBrickUpload> SparseCloudVolume::consumeDirtyBricks() {
    std::lock_guard lock(mutex_);std::vector<CloudBrickUpload> result;result.reserve(dirty_.size());
    for(const auto& [coord,revision]:dirty_) {
        auto it=bricks_.find(coord);if(it==bricks_.end())continue;
        CloudBrickUpload upload;upload.coord=coord;upload.revision=revision;
        upload.density.resize(it->second.voxels.size());
        std::transform(it->second.voxels.begin(),it->second.voxels.end(),upload.density.begin(),
            [this](const CloudVoxelState& s){return uint16_t(std::clamp(
                s.liquidWaterKgM3/config_.referenceLiquidWaterKgM3,0.f,1.f)*65535.f+.5f);});
        result.push_back(std::move(upload));
    }
    dirty_.clear();return result;
}
std::vector<CloudBrickUpload> SparseCloudVolume::brickSnapshot() const {
    std::lock_guard lock(mutex_);std::vector<CloudBrickUpload> result;result.reserve(bricks_.size());
    for(const auto& [coord,brick]:bricks_) {
        CloudBrickUpload upload;upload.coord=coord;upload.revision=brick.revision;
        upload.density.resize(brick.voxels.size());
        std::transform(brick.voxels.begin(),brick.voxels.end(),upload.density.begin(),
            [this](const CloudVoxelState& s){return uint16_t(std::clamp(
                s.liquidWaterKgM3/config_.referenceLiquidWaterKgM3,0.f,1.f)*65535.f+.5f);});
        result.push_back(std::move(upload));
    }
    return result;
}
size_t SparseCloudVolume::activeBrickCount() const {std::lock_guard lock(mutex_);return bricks_.size();}
size_t SparseCloudVolume::activeVoxelCount() const {
    std::lock_guard lock(mutex_);size_t count=0;
    for(const auto& [_,brick]:bricks_)for(const auto& s:brick.voxels)
        count+=s.liquidWaterKgM3>config_.minimumActiveWaterKgM3;
    return count;
}
uint64_t SparseCloudVolume::revision() const {std::lock_guard lock(mutex_);return revision_;}

} // namespace rockyenv
