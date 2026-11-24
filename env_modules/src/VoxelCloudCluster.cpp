#include <rocky_env/VoxelCloudCluster.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace rockyenv {
namespace {
uint32_t mixBits(uint64_t value) {
    value^=value>>30;value*=0xbf58476d1ce4e5b9ULL;
    value^=value>>27;value*=0x94d049bb133111ebULL;
    value^=value>>31;return uint32_t(value);
}
float random01(uint64_t cloudId,uint32_t stream) {
    return float(mixBits(cloudId+0x9e3779b97f4a7c15ULL*uint64_t(stream+1))>>8)/16777216.f;
}
float smoothUnit(float value) {
    value=std::clamp(value,0.f,1.f);return value*value*(3.f-2.f*value);
}
float mixingNoise(vsg::vec3 p,uint64_t seed) {
    const int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    const float fx=smoothUnit(p.x-x),fy=smoothUnit(p.y-y),fz=smoothUnit(p.z-z);
    float result=0.f;
    for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
        const auto key=uint32_t(x+dx)*73856093u^
                       uint32_t(y+dy)*19349663u^uint32_t(z+dz)*83492791u;
        result+=random01(seed,key)*(dx?fx:1.f-fx)*(dy?fy:1.f-fy)*(dz?fz:1.f-fz);
    }
    return result;
}
}

VoxelCloudCluster::VoxelCloudCluster(VoxelCloudClusterState state,SparseCloudConfig config)
    :state_(state),volume_(std::make_unique<SparseCloudVolume>(config)) {
    if(state_.id==0 || !std::isfinite(state_.baseMetres) ||
       !std::isfinite(state_.depthMetres) || state_.depthMetres<=0 ||
       !std::isfinite(state_.horizontalRadius) || state_.horizontalRadius<=0)
        throw std::invalid_argument("invalid voxel cloud cluster");

    // Seed a population of independently rising moist thermals. These parcels
    // are initial thermodynamic state, not a density function evaluated by the
    // renderer. Their different ages, radii, entrainment and paths prevent the
    // volume from degenerating into a stack of equal horizontal lobes.
    const float cell=config.voxelSizeMetres;
    struct Parcel {
        vsg::vec3 centre,velocity;
        float radius,vapour,temperature,liquid;
        vsg::vec3 axis{};
        float stretch=1.f;
    };
    struct DryPocket {vsg::vec3 centre;float radius,strength;};
    std::vector<Parcel> parcels;
    std::vector<DryPocket> dryPockets;
    const int thermalCount=std::clamp(int(std::ceil(state_.horizontalRadius/135.f)),8,14);
    uint32_t stream=0;
    uint32_t fringeStream=20000;
    const vsg::vec3 wind{state_.wind.x,0.f,state_.wind.z};
    const float windSpeed=vsg::length(wind);
    const auto windDirection=windSpeed>.01f ? wind/windSpeed : vsg::vec3{};
    for(int thermal=0;thermal<thermalCount;++thermal) {
        const float angle=2.f*float(3.141592653589793)*random01(state_.id,stream++);
        const float radial=std::sqrt(random01(state_.id,stream++));
        const float sourceRadius=thermal==0 ? 0.f :
            state_.horizontalRadius*(.06f+.62f*radial);
        const vsg::vec3 sourceOffset(std::cos(angle)*sourceRadius,0,std::sin(angle)*sourceRadius);
        const float matureRandom=random01(state_.id,stream++);
        const float mature=thermal==0 ? 1.f : matureRandom;
        const float columnDepth=state_.depthMetres*(.32f+.68f*mature);
        // Adjacent states along a rising trajectory overlap by roughly one
        // radius. A sparsely sampled trajectory appears as horizontal discs
        // even though every sample is a real 3-D voxel sphere.
        const int stages=std::clamp(int(std::ceil(columnDepth/
            std::max(state_.horizontalRadius*.13f,cell*2.5f))),7,18);
        const float phase=2.f*float(3.141592653589793)*random01(state_.id,stream++);
        const float turning=2.9f+2.2f*random01(state_.id,stream++);
        const float shearRate=.09f+.07f*random01(state_.id,stream++);
        for(int stage=0;stage<stages;++stage) {
            const float h=std::clamp((float(stage)+.20f+.65f*random01(state_.id,stream++))/
                                     float(stages),.025f,.98f);
            const float age=h*columnDepth/std::max(state_.updraft,1.f);
            const float entrainment=std::clamp(1.f-(.28f+.34f*random01(state_.id,stream++))*h,
                                               .25f,1.f);
            const float meanderAngle=phase+h*turning;
            const float meander=state_.horizontalRadius*(.025f+.13f*h)*
                                 (.45f+.55f*random01(state_.id,stream++));
            const vsg::vec3 path(std::cos(meanderAngle)*meander,0,
                                 std::sin(meanderAngle)*meander);
            const float crown=std::sin(float(3.141592653589793)*h);
            const float radius=std::max(cell*1.8f,state_.horizontalRadius*
                (.18f+.14f*random01(state_.id,stream++))*(.78f+.50f*crown));
            const vsg::vec3 shear=state_.wind*(age*shearRate);
            const vsg::vec3 centre=state_.centre+sourceOffset+path+shear+
                vsg::vec3(0,state_.baseMetres+h*columnDepth,0);
            const float vapour=.0075f+(state_.vapour-.0075f)*entrainment;
            const float updraft=state_.updraft*entrainment*(.58f+.75f*crown);
            const Parcel parent{centre,state_.wind+vsg::vec3(0,updraft,0),radius,
                std::clamp(vapour,.004f,.03f),state_.temperatureExcess*entrainment,
                (.00012f+.00078f*crown)*entrainment};
            parcels.push_back(parent);
            // A rising thermal sheds lateral child eddies. Each child is a
            // separate moist parcel with its own scale and momentum, so the
            // stored voxel field develops cauliflower billows instead of one
            // analytically smooth metaball or a vertical string of discs.
            const int children=1+int(random01(state_.id,stream++)>.58f);
            for(int child=0;child<children;++child) {
                const float childAngle=2.f*float(3.141592653589793)*random01(state_.id,stream++);
                const float side=random01(state_.id,stream++)>.5f ? 1.f : -1.f;
                const float childRadius=radius*(.34f+.36f*random01(state_.id,stream++));
                const float offset=radius*(.42f+.46f*random01(state_.id,stream++));
                const vsg::vec3 lateral(std::cos(childAngle),.22f*side,std::sin(childAngle));
                const vsg::vec3 childCentre=centre+lateral*offset;
                const vsg::vec3 childVelocity=parent.velocity+
                    vsg::vec3(lateral.x*updraft*.20f,side*updraft*.08f,lateral.z*updraft*.20f);
                parcels.push_back({childCentre,childVelocity,std::max(childRadius,cell*1.6f),
                    parent.vapour*.94f,parent.temperature*.88f,parent.liquid*.78f});
            }
            // The outer thermals shed dilute filaments. Separate their
            // random stream so the core thermals keep the same trajectories.
            const bool exposed=sourceRadius>.32f*state_.horizontalRadius || (thermal==0&&h>.65f);
            if(exposed && h>.18f && h<.94f && stage%3==thermal%3) {
                const auto outward=vsg::normalize(sourceOffset+
                    vsg::vec3(std::cos(meanderAngle),0,std::sin(meanderAngle))*
                    (.16f*state_.horizontalRadius));
                const float leeward=.42f+.58f*smoothUnit((vsg::dot(outward,windDirection)+.85f)/1.55f);
                const vsg::vec3 crosswind{-outward.z,0,outward.x};
                const auto downwind=windSpeed>.01f ? windDirection : outward;
                const auto direction=vsg::normalize(.86f*outward+.84f*leeward*downwind+
                    vsg::vec3(0,.12f+.27f*random01(state_.id,fringeStream++),0));
                const float width=std::max(1.6f*cell,radius*(.23f+.10f*random01(state_.id,fringeStream++)));
                const float bend=.4f+.85f*random01(state_.id,fringeStream++);
                const float side=random01(state_.id,fringeStream++)>.5f ? 1.f : -1.f;
                const auto root=centre+outward*(radius*.88f);
                for(int part=0;part<3;++part) {
                    const float age=float(part);
                    const float taper=1.f-.18f*age;
                    const auto axis=vsg::normalize(direction+crosswind*(side*bend*.22f*age));
                    const auto position=root+direction*(width*(.65f+1.17f*age+.37f*age*age))+
                        crosswind*(side*bend*.32f*width*age*age);
                    const auto velocity=state_.wind+direction*(updraft*.19f)+vsg::vec3(0,updraft*.32f*taper,0);
                    parcels.push_back({position,velocity,std::max(1.35f*cell,width*taper),
                        parent.vapour*(.78f-.07f*age),parent.temperature*.38f*taper,
                        parent.liquid*.38f*leeward*std::exp(-.64f*age),axis,1.95f-.24f*age});
                }
            }
        }
    }

    // Dry environmental eddies are explicit clear-air parcels. They carve
    // scalloped holes into condensate at the cloud edge and between thermals,
    // representing entrainment instead of shader-side erosion noise.
    const int pocketCount=std::clamp(thermalCount*2,20,36);
    for(int i=0;i<pocketCount;++i) {
        const float angle=2.f*float(3.141592653589793)*random01(state_.id,stream++);
        const float h=.08f+.86f*random01(state_.id,stream++);
        const float radius=std::max(cell*1.5f,state_.horizontalRadius*
            (.055f+.11f*random01(state_.id,stream++)));
        const float radial=state_.horizontalRadius*(.42f+.62f*random01(state_.id,stream++));
        dryPockets.push_back({state_.centre+vsg::vec3(std::cos(angle)*radial,
            state_.baseMetres+h*state_.depthMetres,std::sin(angle)*radial),radius,
            .28f+.46f*random01(state_.id,stream++)});
    }

    // Fit the complete support of the cloud before rasterizing it. Cropping
    // a ranked list can cut straight through the wet part of a billow and
    // leave a cloud with an opaque brick-shaped side.
    struct Candidate {
        CloudBrickCoord coord;float influence;
        std::vector<uint16_t> parcelIndices,dryPocketIndices;
    };
    const float brick=cell*float(config.brickSide);
    auto layoutBricks=[&] {
        std::vector<Candidate> candidates;
        std::vector<vsg::vec3> extents;
        extents.reserve(parcels.size());
        vsg::vec3 minimum(1e30f,1e30f,1e30f),maximum(-1e30f,-1e30f,-1e30f);
        for(const auto& p:parcels) {
            const float extension=p.stretch*p.stretch-1.f;
            vsg::vec3 extent;
            for(int axis=0;axis<3;++axis) {
                extent[axis]=p.radius*std::sqrt(1.f+extension*p.axis[axis]*p.axis[axis]);
                minimum[axis]=std::min(minimum[axis],p.centre[axis]-extent[axis]);
                maximum[axis]=std::max(maximum[axis],p.centre[axis]+extent[axis]);
            }
            extents.push_back(extent);
        }
        const int xmin=int(std::floor(minimum.x/brick)),xmax=int(std::floor(maximum.x/brick));
        const int ymin=int(std::floor(minimum.y/brick)),ymax=int(std::floor(maximum.y/brick));
        const int zmin=int(std::floor(minimum.z/brick)),zmax=int(std::floor(maximum.z/brick));
        for(int bz=zmin;bz<=zmax;++bz)for(int by=ymin;by<=ymax;++by)for(int bx=xmin;bx<=xmax;++bx) {
            const vsg::vec3 centre((bx+.5f)*brick,(by+.5f)*brick,(bz+.5f)*brick);
            const auto distanceToBrick=[&](const vsg::vec3& p) {
                const auto d=p-centre;
                const vsg::vec3 delta(std::max(0.f,std::abs(d.x)-brick*.5f),
                                      std::max(0.f,std::abs(d.y)-brick*.5f),
                                      std::max(0.f,std::abs(d.z)-brick*.5f));
                return vsg::length(delta);
            };
            Candidate candidate{{bx,by,bz},0,{},{}};
            for(size_t i=0;i<parcels.size();++i) {
                const auto& p=parcels[i];
                const auto delta=p.centre-centre;
                const auto& extent=extents[i];
                if(std::abs(delta.x)>extent.x+brick*.5f||
                   std::abs(delta.y)>extent.y+brick*.5f||
                   std::abs(delta.z)>extent.z+brick*.5f)continue;
                const float influence=1.f-distanceToBrick(p.centre)/(p.radius*p.stretch);
                if(influence>0) {
                    candidate.influence+=p.liquid*smoothUnit(influence);
                    candidate.parcelIndices.push_back(uint16_t(i));
                }
            }
            if(candidate.influence>0) {
                for(size_t i=0;i<dryPockets.size();++i)
                    if(distanceToBrick(dryPockets[i].centre)<dryPockets[i].radius)
                        candidate.dryPocketIndices.push_back(uint16_t(i));
                candidates.push_back(std::move(candidate));
            }
        }
        return candidates;
    };
    auto candidates=layoutBricks();
    const vsg::vec3 pivot{state_.centre.x,state_.baseMetres+.24f*state_.depthMetres,state_.centre.z};
    for(int attempt=0;candidates.size()>config.maxBricks&&attempt<8;++attempt) {
        const float factor=std::min(.92f,.96f*std::cbrt(float(config.maxBricks)/float(candidates.size())));
        for(auto& p:parcels) {
            p.centre=pivot+(p.centre-pivot)*factor;
            p.radius*=factor;
        }
        for(auto& p:dryPockets) {
            p.centre=pivot+(p.centre-pivot)*factor;
            p.radius*=factor;
        }
        candidates=layoutBricks();
    }
    // A budget smaller than the eight neighbours at a brick corner can
    // require a final cap even when the entire support fits in one cell.
    if(candidates.size()>config.maxBricks) {
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
            return a.influence>b.influence;
        });
        candidates.resize(config.maxBricks);
    }
    for(auto& p:parcels)p.axis*=std::sqrt(1.f-1.f/(p.stretch*p.stretch));
    const int side=int(config.brickSide);
    const CloudDynamics initialAir;
    for(const auto& candidate:candidates) {
        std::vector<CloudVoxelState> voxels(size_t(side)*side*side);
        bool active=false;
        for(int z=0;z<side;++z)for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const vsg::vec3 point((candidate.coord.x*side+x+.5f)*cell,
                                  (candidate.coord.y*side+y+.5f)*cell,
                                  (candidate.coord.z*side+z+.5f)*cell);
            float weight=0,liquid=0,filamentWater=0;CloudVoxelState value;
            for(const auto parcelIndex:candidate.parcelIndices) {
                const auto& p=parcels[parcelIndex];
                const vsg::vec3 relative=point-p.centre;
                const float axial=vsg::dot(relative,p.axis);
                const float distance=std::sqrt(std::max(0.f,vsg::dot(relative,relative)-axial*axial));
                const float q=std::max(0.f,1.f-distance/p.radius);
                const float w=q*q*(3.f-2.f*q);
                if(w<=0)continue;
                liquid+=p.liquid*w;
                if(p.stretch>1.f)filamentWater+=p.liquid*w;
                value.vapourMixingRatio=std::max(value.vapourMixingRatio,p.vapour*w);
                value.temperatureOffsetK=std::max(value.temperatureOffsetK,p.temperature*w);
                // Rising thermals form toroidal circulation: upward core,
                // outward crown and entraining/downward edge. This coherent
                // velocity is physical initial state, not rendered noise.
                const float horizontal=std::hypot(relative.x,relative.z);
                const vsg::vec3 radial=horizontal>1e-4f ?
                    vsg::vec3(relative.x/horizontal,0,relative.z/horizontal):vsg::vec3{};
                const float ring=std::sin(std::clamp(distance/p.radius,0.f,1.f)*float(3.141592653589793));
                const float circulation=p.velocity.y*.32f*ring;
                const vsg::vec3 thermalVelocity=p.velocity+
                    radial*(circulation*relative.y/std::max(p.radius,1.f))+
                    vsg::vec3(0,-circulation*horizontal/std::max(p.radius,1.f),0);
                value.velocity+=thermalVelocity*w;weight+=w;
            }
            if(weight>0) {
                float dryFactor=1.f;
                for(const auto pocketIndex:candidate.dryPocketIndices) {
                    const auto& pocket=dryPockets[pocketIndex];
                    const float q=1.f-vsg::length(point-pocket.centre)/pocket.radius;
                    if(q>0)dryFactor*=1.f-pocket.strength*smoothUnit(q);
                }
                // Condensation grows above the lifting condensation level.
                // A gradual onset also resolves the underside within the
                // same stored water field used for optics and dynamics.
                const float basal=smoothUnit((point.y-state_.baseMetres)/(3.f*cell));
                // Dry-air mixing at resolved eddy scales disturbs the
                // interface. Store it in the liquid and vapour fields so
                // thickness and shadows follow the same wisps at all ranges.
                const float edge=1.f-.69f*smoothUnit((liquid-.00030f)/.00160f);
                auto local=point-state_.centre;
                // Elongate the dry gaps along the wind in the stored field.
                // The opaque core is protected from the strongest erosion.
                local-=windDirection*(.38f*vsg::dot(local,windDirection));
                const float mixing=.43f*mixingNoise(local/(4.3f*cell),state_.id)+
                    .34f*mixingNoise(local/(2.9f*cell),state_.id+19u)+
                    .23f*mixingNoise(local/(2.05f*cell),state_.id+31u);
                dryFactor*=1.f-.97f*edge*smoothUnit((mixing-.38f)/.23f);
                value.liquidWaterKgM3=std::clamp(liquid*dryFactor*basal,0.f,.02f);
                value.vapourMixingRatio*=.30f+.70f*dryFactor;
                if(value.liquidWaterKgM3>0.f) {
                    // Seed pre-existing droplets in nearly saturated air.
                    // Very thin tails relax towards the dry exterior, while
                    // capping supersaturation avoids thickening the original
                    // cloud abruptly when the first dynamics step runs.
                    const float saturation=initialAir.airAt(point.y,value.temperatureOffsetK).saturationMixingRatio;
                    const float water=value.liquidWaterKgM3;
                    const float humidAir=smoothUnit(water/6.e-6f);
                    const float protectedAir=std::max(smoothUnit(water/.00065f),filamentWater/liquid);
                    const float minimumHumidity=(.988f+.010f*protectedAir)*humidAir;
                    const float maximumHumidity=1.001f+.003f*protectedAir;
                    value.vapourMixingRatio=std::clamp(value.vapourMixingRatio,
                        saturation*minimumHumidity,saturation*maximumHumidity);
                }
                value.velocity/=weight;
            }
            voxels[size_t(x)+size_t(side)*(size_t(y)+size_t(side)*size_t(z))]=value;
            active|=value.liquidWaterKgM3>config.minimumActiveWaterKgM3 ||
                    value.vapourMixingRatio>1e-6f;
        }
        if(active)volume_->setBrick(candidate.coord,voxels);
    }
}

bool VoxelCloudCluster::resident(const vsg::vec3& p) const { return volume_->resident(p); }
float VoxelCloudCluster::density(const vsg::vec3& p) const { return volume_->density(p); }
void VoxelCloudCluster::step(float dt,const CloudDynamics& dynamics) { volume_->step(dt,dynamics); }

} // namespace rockyenv
