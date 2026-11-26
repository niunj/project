#include <vsgraysim/model_packages/equipment/sonar/DepthSounder.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include "VoxelTerrain.h"
#include <algorithm>

namespace vsgraysim::model_packages {

std::optional<vsg::dvec3> DepthSounder::origin(const earth::VoxelSceneSnapshot& scene) const {
    const auto host=scene.objects.find(hostObject);
    if(host==scene.objects.end()||!host->second.volume||!host->second.volume->occupiedCount())return {};
    return host->second.pose(scene.tick,scene.fixedStep)*localPosition;
}

SonarFrame DepthSounder::sample(const earth::VoxelSceneSnapshot& scene, const vsg::dvec3& p) const {
    SonarFrame frame;
    frame.sensor_id=equipment.equipment_id;
    frame.platform_id=equipment.platform_id;
    frame.tick=scene.tick;
    frame.frame_id=scene.tick+1;
    frame.surface_revision=scene.contentRevision;
    frame.sim_time=scene.timeSeconds();
    frame.range_limit_m=rangeMetres;
    frame.water_column_enabled=waterColumnReturns;
    frame.bottom=BottomReturn::DryTransducer;
    if(!scene.ocean||!scene.ocean->contains(p.x,p.z))return frame;
    frame.transducer_draft_m=std::max(0.,scene.ocean->surface(p.x,p.z,frame.sim_time).height-p.y);
    if(frame.transducer_draft_m<.02||scene.ocean->densityAt(p,frame.sim_time)<=0.)return frame;
    frame.bottom=BottomReturn::NoBottom;
    const vsg::dvec3 down{0,-1,0};
    const auto hit=earth::raycastVoxelScene(scene,p,down,rangeMetres,true,seabedObject);
    if(waterColumnReturns&&p.y>scene.ocean->bottom){
        const double limit=std::min({rangeMetres,p.y-scene.ocean->bottom,hit?hit->distance:rangeMetres});
        double nearest=limit;
        for(const auto& [id,object]:scene.objects){
            if(id==hostObject||object.role!="target")continue;
            const auto reflection=earth::raycastVoxelScene(scene,p,down,nearest,false,id);
            if(reflection&&reflection->distance>1e-5&&reflection->distance<nearest){
                nearest=reflection->distance;
                frame.echo_range_m=nearest;
            }
        }
        if(frame.echo_range_m>0.)return frame;
    }
    if(!hit||hit->distance<=0.||hit->position.y<scene.ocean->bottom)return frame;
    frame.bottom=BottomReturn::Valid;
    frame.clearance_m=hit->distance;
    frame.water_depth_m=frame.clearance_m+frame.transducer_draft_m;
    frame.two_way_ms=frame.clearance_m*(2./1.5);
    return frame;
}

}
