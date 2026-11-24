#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include "../src/earth/VoxelTerrain.h"
#include <vsg/all.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <vsgraysim/earth/SceneTasks.h>
#include <vsgraysim/earth/VoxelOcean.h>
using namespace vsgraysim::earth;
using json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
json box(const std::string& id,double x=0){return {{"id",id},{"position_m",{x,0,0}},
    {"source",{{"type","box"},{"dimensions",{8,8,8}},{"cell_size_m",1}}},
    {"material",{{"temperature_k",320},{"density_kg_m3",2000}}}};}
json definition(){auto moving=box("moving",20);moving["velocity_mps"]={2,0,0};return {{"format","voxel-scene-v1"},
    {"fixed_step_seconds",.02},{"objects",{box("block"),moving}},
    {"events",{{{"tick",2},{"kind","damage"},{"object","block"},{"space","object"},{"point_m",{4,4,4}},{"radius_m",2.1}},
                {{"tick",4},{"kind","settle"},{"object","block"}}}}};}
void authoringAndQueries(){
    auto scene=VoxelScene::load(definition());auto original=scene->snapshot();
    check(original->objects.at("block").volume==original->objects.at("moving").volume,"instances did not share immutable baked geometry");
    auto hit=scene->raycast({4,20,4},{0,-2,0},30);check(hit&&hit->object=="block"&&std::abs(hit->position.y-8)<1e-8,"world ray does not hit the correct object");
    auto change=json::array({{{"kind","place"},{"object","moving"},{"position_m",{30,0,0}},{"rotation_deg",{0,90,0}}},
                             {{"kind","remove"},{"object","missing"}}});
    rejects([&]{scene->edit(change,original->revision);},"invalid batch was partially committed");
    check(scene->snapshot()==original,"failed edit replaced the current snapshot");
    scene->edit(json::array({{{"kind","place"},{"object","moving"},{"position_m",{30,0,0}},{"rotation_deg",{0,90,0}}}}),original->revision);
    auto placed=scene->snapshot();check(placed->contentRevision==original->contentRevision,"pose edit rebuilt voxel topology");
    hit=scene->raycast({26,20,4},{0,-1,0},30);check(hit&&hit->object=="moving","rotated object ray did not use its rigid transform");
    rejects([&]{scene->edit(json::array({{{"kind","remove"},{"object","moving"}}}),original->revision);},"stale authoring revision was accepted");
    rejects([&]{scene->edit(json::array({{{"kind","remove"},{"object","block"}}}),placed->revision);},"deleted object still has queued events");
    json matrix=json::array();const auto rigid=placed->objects.at("moving").pose(placed->tick,placed->fixedStep);
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)matrix.push_back(rigid[c][r]);
    scene->edit(json::array({{{"kind","place"},{"object","moving"},{"pose",matrix}}}),placed->revision);
    check(scene->snapshot()->objects.at("moving").pose(0,.02)==rigid,"matrix authoring discarded object rotation");
    auto scaled=matrix;scaled[0]=2;auto beforeInvalid=scene->snapshot();
    rejects([&]{scene->edit(json::array({{{"kind","place"},{"object","moving"},{"pose",scaled}}}),beforeInvalid->revision);},"scaled pose was accepted");
    check(scene->snapshot()==beforeInvalid,"invalid pose partially published");
    scene->cancel(1);scene->cancel(2);scene->edit(json::array({{{"kind","remove"},{"object","block"}}}),scene->snapshot()->revision);
    rejects([&]{scene->cancel(1);},"canceled sequence remained active");
    scene->setPaused(false);rejects([&]{scene->edit(json::array({{{"kind","remove"},{"object","moving"}}}),placed->revision);},"live scene permitted an authoring transaction");
}
void steppingAndCheckpoint(){
    auto fine=VoxelScene::load(definition()),coarse=VoxelScene::load(definition());fine->setPaused(false);coarse->setPaused(false);
    auto retained=fine->snapshot();
    for(int i=1;i<=10;++i)fine->advanceTo(i*.02,1);
    auto catchup=coarse->advanceTo(.2,3);check(catchup.steps==3&&catchup.pendingSteps==7,"catch-up discarded simulation time");
    while(coarse->advanceTo(.2,3).pendingSteps){}
    check(fine->checkpoint()==coarse->checkpoint(),"render cadence changed event order or voxel results");
    check(retained->objects.at("block").volume->occupiedCount()==512&&fine->snapshot()->objects.at("block").volume->occupiedCount()<512,"damage mutated a retained snapshot");
    check(fine->snapshot()->journal.size()==2&&fine->snapshot()->journal[0]["sequence"]==1,"event journal lost deterministic ordering");
    check(std::abs(fine->snapshot()->objects.at("moving").pose(10,.02)[3][0]-20.4)<1e-12,"prescribed motion uses display time instead of integer ticks");
    auto beforeEvent=VoxelScene::load(definition());beforeEvent->setPaused(false);beforeEvent->advanceTo(.02);
    auto midEvent=VoxelScene::load(beforeEvent->checkpoint());beforeEvent->advanceTo(.1,10);midEvent->advanceTo(.1,10);
    check(beforeEvent->checkpoint()==midEvent->checkpoint(),"restoring pending events changed the executed journal");
    auto resumed=VoxelScene::load(fine->checkpoint());fine->advanceTo(.5,32);resumed->advanceTo(.5,32);
    check(fine->checkpoint()==resumed->checkpoint(),"checkpoint continuation changed state or motion epoch");
    resumed->setPaused(true);auto stopped=resumed->snapshot();check(resumed->advanceTo(2).steps==0&&resumed->snapshot()==stopped,"pause advanced the scene");
    rejects([&]{resumed->advanceTo(.1);},"time reversal silently discarded history");
    auto empty=definition();empty["objects"]=json::array({box("tiny")});empty["events"]=json::array({{{"tick",1},{"kind","damage"},{"object","tiny"},{"point_m",{4,4,4}},{"radius_m",100}}});
    auto erased=VoxelScene::load(empty);erased->setPaused(false);erased->advanceTo(.02);
    auto decoded=VoxelScene::load(erased->checkpoint());check(decoded->snapshot()->objects.at("tiny").massKg()==0,"empty destroyed object cannot round-trip");
    check(!decoded->raycast({4,20,4},{0,-1,0},30),"destroyed object retained collision geometry");
}
void publicationAndAdapters(){
    auto scene=VoxelScene::load(definition());scene->setPaused(false);auto old=scene->snapshot();
    scene->setPreparePublication([](auto){throw std::runtime_error("injected renderer allocation failure");});
    rejects([&]{scene->advanceTo(.04,2);},"failed publication was admitted");
    check(scene->snapshot()==old&&scene->checkpoint()["pending_events"].size()==2,"failed publication consumed time/events");
    scene->setPreparePublication([&](auto){scene->setPaused(true);});
    rejects([&]{scene->advanceTo(.02);},"reentrant publication mutated the owner state");
    check(scene->snapshot()==old&&!scene->paused(),"reentrant failure leaked state");
    scene->setPreparePublication({});scene->advanceTo(.02,1);const auto justBefore=scene->checkpoint();
    scene->setPreparePublication([](auto){throw std::runtime_error("damage mesh allocation failed");});
    rejects([&]{scene->advanceTo(.04,1);},"damage resource failure was ignored");
    check(scene->checkpoint()==justBefore,"failed damage consumed its event or published partial occupancy");
    scene->setPreparePublication({});scene->advanceTo(.04,1);
    auto current=scene->snapshot();auto display=makeVoxelSceneDisplay(*current);auto sensor=makeVoxelSceneSensorData(current);
    auto cached=makeVoxelSceneDisplay(*current,&display);check(cached.reusedDrawings==current->objects.size()&&cached.builtDrawings==0,"unchanged geometry was rebuilt");
    auto recolored=*current;recolored.objects.begin()->second.material["color_rgba"]={1,0,0,1};auto changed=makeVoxelSceneDisplay(recolored,&display);check(changed.builtDrawings==1&&changed.reusedDrawings+1==current->objects.size(),"material edit did not invalidate exactly one object");
    check(sensor.snapshot==current&&display.contentRevision==current->contentRevision,"display and sensor did not pin the same snapshot");
    check(sensor.instances.size()==current->objects.size(),"sensor adapter lost voxel instances");
    for(const auto& instance:sensor.instances){const auto& object=current->objects.at(instance.id);
        check(instance.geometry->indexCount==object.volume->indices().size(),"sensor geometry differs from display topology");
        const auto* properties=instance.geometry->arrays[1]->data->cast<vsg::vec2Array>();check(properties&&properties->at(0).x==320,"sensor lost object temperature");}
    scene->advanceTo(.06);display.sync(*scene->snapshot());
    std::atomic<bool> denied{false};std::thread other([&]{check(bool(scene->snapshot()),"cross-thread snapshot read failed");try{scene->setPaused(true);}catch(const std::logic_error&){denied=true;}});other.join();check(denied,"cross-thread scene mutation was allowed");
}
void meshImportPersistence(){
    const auto root=std::filesystem::temp_directory_path()/("voxel-model-import-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{root};
    std::filesystem::create_directories(root/"assets");
    {std::ofstream obj(root/"assets/cube.obj");obj<<"v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nf 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 4 3 7 8\nf 1 4 8 5\nf 2 6 7 3\n";}
    json declaration={{"format","voxel-scene-v1"},{"objects",json::array({
        {{"id","imported"},{"source",{{"type","mesh"},{"path","assets/cube.obj"},{"max_dimension",12},{"fill_interior",true}}}}})}};
    {std::ofstream out(root/"scene.json");out<<declaration;}
    auto scene=VoxelScene::open((root/"scene.json").string());
    check(scene->snapshot()->objects.at("imported").massKg()>0&&bool(scene->raycast({0,10,0},{0,-1,0},20)),"relative OBJ import did not create a solid queryable volume");
    scene->saveCheckpoint((root/"resume.json").string());std::filesystem::remove_all(root/"assets");
    auto restored=VoxelScene::open((root/"resume.json").string());
    check(scene->checkpoint()==restored->checkpoint(),"checkpoint still depends on the original mesh asset");
    check(bool(restored->raycast({0,10,0},{0,-1,0},20)),"restored imported target lost collision geometry");
}
void codecAndPersistence(){
    auto scene=VoxelScene::load(definition());const auto package=scene->snapshot()->objects.at("block").volume->encodeVolume();
    auto corrupt=package;corrupt["occupied_runs"]={{0,4},{3,2}};rejects([&]{VoxelTerrain::decodeVolume(corrupt);},"overlapping volume runs were admitted");
    corrupt=package;corrupt["dimensions"]={4000000,4000000,4000000};rejects([&]{VoxelTerrain::decodeVolume(corrupt);},"oversized volume was admitted");
    auto duplicate=definition();duplicate["objects"].push_back(duplicate["objects"][0]);rejects([&]{VoxelScene::load(duplicate);},"duplicate stable ids were admitted");
    auto invalidCheckpoint=scene->checkpoint();invalidCheckpoint["content_revision"]=2;rejects([&]{VoxelScene::load(invalidCheckpoint);},"inconsistent revision metadata was admitted");
    auto wrongDate=definition();wrongDate["utc_origin"]={{"year",2026},{"month",2},{"day",29}};
    rejects([&]{VoxelScene::load(wrongDate);},"invalid UTC calendar date was admitted");
    wrongDate["utc_origin"]["year"]=2024;auto leap=VoxelScene::load(wrongDate);
    check(leap->checkpoint()["utc_origin"]["day"]==29,"UTC epoch was not preserved in checkpoint");
    auto bad=definition();bad["objects"][0]["material"]["density_kg_m3"]=-1;rejects([&]{VoxelScene::load(bad);},"invalid material was admitted");
    auto path=std::filesystem::temp_directory_path()/("voxel-scene-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    scene->saveCheckpoint(path.string());auto restored=VoxelScene::open(path.string());check(scene->checkpoint()==restored->checkpoint(),"atomic checkpoint file did not round-trip");
    {std::ifstream in(path);json before;in>>before;rejects([&]{scene->saveCheckpoint((path/"missing/file.json").string());},"invalid save target was admitted");std::ifstream verify(path);json after;verify>>after;check(before==after,"failed save corrupted an existing checkpoint");}
    std::filesystem::remove(path);
}
}
void taskAndProcessIsolation(){
    SceneTasks tasks(4,2);std::atomic<int> active{0},maximum{0};std::vector<std::future<int>> futures;
    for(int i=0;i<12;++i)futures.push_back(tasks.submit(SceneTaskKind::Target,[&,i]{const int n=++active;int old=maximum;while(n>old&&!maximum.compare_exchange_weak(old,n)){}std::this_thread::sleep_for(std::chrono::milliseconds(5));--active;if(i==3)throw std::runtime_error("one target task failed");return i;}));
    int successes=0;for(auto& future:futures)try{future.get();++successes;}catch(const std::runtime_error&){}check(successes==11&&maximum<=2&&maximum>=1,"target task failure escaped or concurrency budget was exceeded");
    auto terrain=tasks.submit(SceneTaskKind::Terrain,[]{return 17;});check(terrain.get()==17,"target task exception disabled another category");
    const auto root=std::filesystem::temp_directory_path()/("voxel-worker-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{root};
    {std::ofstream obj(root/"plane.obj");obj<<"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";}
    for(const auto& mode:{std::string("crash"),std::string("timeout")}){auto worker=root/mode;{std::ofstream file(worker);file<<"#!/bin/sh\nulimit -c 0\n"<<(mode=="crash"?"kill -SEGV $$\n":"exec /bin/sleep 60\n");}std::filesystem::permissions(worker,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write|std::filesystem::perms::owner_exec);
        auto bad=json{{"id","bad"},{"required",false},{"source",{{"type","mesh"},{"path",(root/"plane.obj").string()},{"isolate_import",true},{"import_worker",worker.string()},{"import_timeout_ms",100}}}};
        auto badTerrain=json{{"id","bad-terrain"},{"required",false},{"source",{{"type","earth"},{"map","unused-by-crashing-worker.map.json"}}}};
        auto declaration=json{{"format","voxel-scene-v1"},{"loading",{{"workers",3},{"target_jobs",2},{"isolate_prepare",true},{"worker",worker.string()},{"timeout_ms",100}}},{"objects",{bad,badTerrain,box("healthy")}}};auto loaded=VoxelScene::load(declaration);check(loaded->snapshot()->objects.count("healthy")&&!loaded->snapshot()->objects.count("bad"),"worker fault prevented a healthy object loading");check(loaded->snapshot()->loadDiagnostics[0]["status"]=="failed","worker fault was silently discarded");declaration["objects"][0]["required"]=true;rejects([&]{VoxelScene::load(declaration);},"required worker failure published a partial scene");
    }
    auto isolated=json{{"format","voxel-scene-v1"},{"objects",{{{"id","compiled"},{"source",{{"type","mesh"},{"path",(root/"plane.obj").string()},{"max_dimension",8},{"surface_detail","source_clipped"},{"isolate_import",true}}}}}}};auto loaded=VoxelScene::load(isolated);check(loaded->snapshot()->objects.at("compiled").volume->appearance()->detailed(),"real isolated worker failed to return validated geometry");
    auto direct=definition();direct["ocean"]={{"half_extent_m",100},{"bottom_m",-20},{"wave_height_m",.5}};auto isolatedOcean=direct;isolatedOcean["loading"]={{"workers",3},{"target_jobs",1},{"isolate_prepare",true}};auto a=VoxelScene::load(direct),b=VoxelScene::load(isolatedOcean);check(a->snapshot()->ocean->encode()==b->snapshot()->ocean->encode(),"isolated ocean preparation changed the canonical wave field");
    const auto sceneFile=root/"async.vscene.json";{std::ofstream out(sceneFile);out<<isolated.dump();}auto preparation=VoxelScene::prepareFileAsync(sceneFile.string());const auto staged=preparation.document.get();auto installed=VoxelScene::load(staged);check(installed->snapshot()->objects.at("compiled").volume->vertices()==loaded->snapshot()->objects.at("compiled").volume->vertices(),"background scene staging changed target geometry");
    const auto cancelStart=std::chrono::steady_clock::now();{auto cancelled=VoxelScene::prepareFileAsync(sceneFile.string());cancelled.cancellation->store(true);try{cancelled.document.get();}catch(const std::runtime_error&){} }check(std::chrono::steady_clock::now()-cancelStart<std::chrono::seconds(3),"cancelled background scene did not reap its worker promptly");
    std::cout<<"PASS: bounded category tasks, normal exception recovery, subprocess crash/timeout isolation and real asset compilation\n";
}
#include "scene_weather_checks.h"
int main()try{sceneWeatherChecks();taskAndProcessIsolation();authoringAndQueries();steppingAndCheckpoint();publicationAndAdapters();meshImportPersistence();codecAndPersistence();std::cout<<"PASS: atomic voxel authoring, deterministic events/motion, immutable publication, sensor/display topology and checkpoints\n";return 0;}
catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
