#pragma once
#include "../src/earth/VoxelTerrain.h"
#include <functional>
#include <numeric>
inline void runEarthTopicPass(vsgraysim::earth::VoxelScene& scene,vsgraysim::viewport3d::VoxelSceneFeature& feature,
 vsgraysim::viewport3d::Viewport3DView& view,const std::filesystem::path& directory,const std::string& topic,
 const std::function<void(vsg::dvec3,vsg::dvec3)>& camera,const std::function<double()>& render,
 const std::function<double()>& native,const std::function<void(const std::string&)>& capture){
 using namespace vsgraysim::earth;using namespace vsgraysim::viewport3d;using json=nlohmann::json;
 auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 const auto initial=scene.snapshot();require(initial->ocean&&initial->objects.count("vessel"),"topic lacks ocean or target");
 auto* map=view.featureAs<EarthViewFeature>(EarthView::kId);auto* clouds=view.featureAs<CloudFeature>(CloudFeature::kId);
 for(int i=0;i<8;++i)render();require(map&&map->weather()&&map->weather()->clouds&&map->weather()->cloudObjects&&clouds&&clouds->enabled(),"topic did not enable the shared single-object GPU cloud field");
 const auto& vessel=initial->objects.at("vessel");json report={{"topic",topic},{"utc_origin",initial->utcOrigin},{"loading",initial->loadDiagnostics},{"clouds",true},{"ocean",true},{"background_stage_after_view_ready_wall_ms",view.property("topicPreparationMs").toDouble()},{"frames_while_preparing",view.property("topicPreparationFrames").toULongLong()},{"source_clipped",vessel.volume->appearance()&&vessel.volume->appearance()->detailed()},{"voxel_size_m",vessel.volume->resolution_m},{"occupied_voxels",vessel.volume->occupiedCount()},{"surface_vertices",vessel.surface().vertices().size()},{"surface_patches",vessel.volume->appearance()?vessel.volume->appearance()->patches.size():0},{"atlas_pixels",vessel.surface().imagery_width*vessel.surface().imagery_height},{"surface_triangles",vessel.surface().indices().size()/3},{"full_rebuild_reference",bool(std::getenv("VSGRAYSIM_SCENE_REBUILD_ALL"))},{"render_frames",json::array()},{"simulation_frames",json::array()}};
 camera({160,58,200},{0,10,0});capture("joint-overview");camera({5000,450,9500},{0,30,6500});capture("joint-coast-clouds");camera({160,58,200},{0,10,0});
 for(int i=0;i<60;++i){double draw=render();report["render_frames"].push_back({{"render_wall_ms",draw},{"render_submit_wait_ms",native()}});}
 auto* top=view.window();top->setFixedSize(1920,1080);QApplication::processEvents(QEventLoop::AllEvents,50);for(int i=0;i<3;++i)render();
 camera({32,17,-87},{0,7,-60});capture("detail-stern-flight-deck");camera({27,25,-12},{0,20,-12});capture("detail-bridge");camera({24,41,-12},{0,34,-12});capture("detail-radar");camera({28,23,75},{0,10,52});capture("detail-bow");
 top->setFixedSize(960,540);QApplication::processEvents(QEventLoop::AllEvents,50);camera({160,58,200},{0,10,0});for(int i=0;i<3;++i)render();
 double rangeError=0;size_t ranges=0;
 for(int i=1;i<=20;++i){QElapsedTimer timer;timer.start();scene.setPaused(false);auto step=scene.advanceTo(i*.04,8);scene.setPaused(true);require(!step.pendingSteps,"joint topic dropped physics ticks");const double update=timer.nsecsElapsed()/1e6,draw=render();const auto state=scene.snapshot();for(const auto& [id,o]:state->objects)require(feature.display().placements.at(id)->matrix==o.pose(state->tick,state->fixedStep),"joint display placement differs from authoritative body pose");
  for(double z:{-40.,0.,40.}){auto a=scene.raycast({0,100,z},{0,-1,0},130),b=scene.rigidRaycast({0,100,z},{0,-1,0},130);if(a&&a->object=="vessel"){require(b&&b->object=="vessel","Bullet missed source-clipped visible surface");rangeError=std::max(rangeError,std::abs(a->distance-b->distance));++ranges;}}
  report["simulation_frames"].push_back({{"simulation_interval_ms",40},{"advance_wall_ms",update},{"buoyancy_wall_ms",step.buoyancyMs},{"water_drag_wall_ms",step.waterDragMs},{"water_total_wall_ms",step.waterMs},{"render_wall_ms",draw},{"render_submit_wait_ms",native()},{"complete_wall_ms",update+draw}});
 }
 require(ranges>=20&&rangeError<.001,"joint source-surface collision/sensor range failed 1mm consistency");report["target_range_checks"]=ranges;report["maximum_target_range_error_m"]=rangeError;
 const auto before=scene.snapshot();std::string terrain;std::optional<VoxelSceneHit> point;
 std::ofstream(directory/"topic-acceptance.partial.json")<<report.dump(2)<<'\n';
 for(const auto& [id,o]:before->objects)if(o.earth){terrain=id;const auto bounds=o.localBounds();vsg::dvec3 selected{};double highest=-1e30;const double margin=std::max(400.,o.volume->resolution_m*4.);
  for(const auto& v:o.surface().vertices())if(v.x>bounds.min.x+margin&&v.x<bounds.max.x-margin&&v.z>bounds.min.z+margin&&v.z<bounds.max.z-margin&&v.y>highest){selected=v;highest=v.y;}
  if(highest>2){auto hit=scene.raycast({selected.x,5000,selected.z},{0,-1,0},10000);if(hit&&hit->object==id)point=hit;}break;}
 require(point.has_value(),"topic lacks a land test point");const auto targetBefore=feature.display().drawings.at("vessel").node;
 const double radius=std::max(160.,before->objects.at(terrain).volume->resolution_m*2.1);
 scene.schedule({{"kind","damage"},{"object",terrain},{"tick",before->tick+1},{"space","scene"},{"point_m",{point->position.x,point->position.y,point->position.z}},{"radius_m",radius}});
 QElapsedTimer damage;damage.start();scene.setPaused(false);const auto step=scene.advanceTo(before->timeSeconds()+.02,8);scene.setPaused(true);require(!step.pendingSteps,"topic terrain destruction lost physics time");const double damageMs=damage.nsecsElapsed()/1e6;render();
 require(feature.display().drawings.at("vessel").source==vessel.volume,"terrain edit changed source target geometry");
 if(!std::getenv("VSGRAYSIM_SCENE_REBUILD_ALL"))require(feature.display().drawings.at("vessel").node==targetBefore&&feature.display().reusedDrawings>=1,"terrain damage rebuilt the unchanged detailed target");
 auto a=scene.raycast({point->position.x,5000,point->position.z},{0,-1,0},10000),b=scene.rigidRaycast({point->position.x,5000,point->position.z},{0,-1,0},10000);require(bool(a)==bool(b),"damaged terrain display/Bullet hit mismatch");double terrainError=0;if(a){require(a->object==b->object,"damaged terrain hit a different collision object");terrainError=std::abs(a->distance-b->distance);require(terrainError<.03,"damaged terrain range discrepancy exceeds 3cm");require(a->position.y<point->position.y-.1,"terrain damage did not lower the visible/query surface");}
 camera(point->position+vsg::dvec3(radius*2,radius*2,radius*2),point->position);capture("joint-terrain-damaged");
 report["damage"]={{"advance_wall_ms",damageMs},{"buoyancy_wall_ms",step.buoyancyMs},{"reused_drawings",feature.display().reusedDrawings},{"built_drawings",feature.display().builtDrawings},{"maximum_terrain_range_error_m",terrainError}};
 QElapsedTimer modeTimer;modeTimer.start();scene.edit(json::array({{{"kind","terrain_mode"},{"object",terrain},{"mode","voxel"}}}),scene.snapshot()->revision);const double modeMs=modeTimer.nsecsElapsed()/1e6;render();capture("joint-terrain-voxel");report["terrain_mode_switch_ms"]=modeMs;require(scene.snapshot()->objects.at(terrain).terrainMode=="voxel","topic failed HD/voxel mode switch");
 const auto checkpoint=directory/"topic-checkpoint.json";scene.saveCheckpoint(checkpoint.string());auto restored=VoxelScene::open(checkpoint.string());require(restored->snapshot()->objects.at("vessel").volume->vertices()==scene.snapshot()->objects.at("vessel").volume->vertices(),"topic checkpoint changed the target source surface");report["checkpoint_bytes"]=std::filesystem::file_size(checkpoint);report["checkpoint_restored"]=true;
 report["scope"]="60 frozen draw samples; 20 x 40ms joint simulation; one terrain destruction update, separately timed. Native submit+device wait, not GPU-only timestamps. Screenshots/encoding/checkpoint excluded. No long-duration ship stability claim.";
 std::ofstream(directory/"topic-acceptance.json")<<report.dump(2)<<'\n';std::cout<<"PASS: unified Taipei terrain/cloud/ocean/source target, GPU draw, physics/sensor consistency, damage cache and checkpoint\n";
}
