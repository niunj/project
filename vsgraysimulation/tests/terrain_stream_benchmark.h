#pragma once
#include <vsgraysim/Simulation.h>
#include <vsgraysim/earth/EarthDataService.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>

namespace terrain_benchmark {
using namespace vsgraysim;
using json=nlohmann::json;
namespace fs=std::filesystem;
inline void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }

inline json scenario() {
    const auto custom=std::getenv("VSGRAYSIM_BENCH_EARTH_SCENE");
    const auto path=fs::absolute(custom?custom:"config/earth/global-voxel.scene.json").string();
    json doc;std::ifstream(path)>>doc;
    doc.at("earth")["map"]=earth::EarthDataService::resolveMapPath(doc,path);
    return doc;
}
inline void run(const fs::path& root,vsg::Device* device=nullptr,earth::PrepareSurfaceView prepare={},
                std::function<void(const Simulation&)> render={},std::function<uint64_t()> observerRevision={},
                std::function<float()> environmentWeight={}) {
    using Clock=std::chrono::steady_clock;
    auto doc=scenario();
    doc["simulation"]={{"sensor_backend","gpu"},{"physics_step",.01}};
    doc["sensors"]["sensor_list"]={{{"sensor_id",51},{"type","ir"},{"band","MWIR"},
        {"tx_params",{{"power",0},{"wavelength",4e-6}}},
        {"signal_params",{{"image_width",64},{"image_height",48},{"frame_interval_s",.02}}},
        {"aperture",{{"position_offset",{0,5000,0}},{"direction",{0,-1,0}},{"fov_horizontal",10}}}}};
    const auto path=root/"camera.json";std::ofstream(path)<<doc;
    Simulation sim;sim.setSensorDevice(device);
    sim.setEarthViewPreparation(std::move(prepare));
    check(sim.initFromScenario(path.string()) && sim.gpuSensorBackendActive(),"real GPU benchmark initialization");
    sim.physics()->setGravity({0,0,0});
    simcore::RigidState survey;
    survey.position={0,1960,0};
    check(sim.physics()->createBoxBody("survey",{1,1,1},survey,1),"survey platform");
    sim.mountSensorOnObject(51,"survey",{0,-3,0},{0,-1,0});
    if(render) render(sim);
    auto published=sim.earthSurface();
    sim.setEarthCommitHandler([&](auto surface){
        check(sim.physics()->snapshot()->surface_revision==surface->revision,"published ahead of Bullet");
        published=surface;
    });
    std::vector<double> changes,steady,stepTimes,viewTimes,lodTimes,tileTimes,observerTimes,environmentTimes;
    const auto tracePath=std::getenv("VSGRAYSIM_BENCH_SWITCH_CSV");
    struct SwitchSample { int64_t start_ns, end_ns; uint64_t revision; bool lod; };
    std::vector<SwitchSample> trace;
    trace.reserve(100);
    double maxCollisionError=0;
    unsigned lodChanges=0,steps=0;
    uint64_t lastFrame=0;
    const int soakSeconds=std::getenv("VSGRAYSIM_SOAK_SECONDS") ? std::stoi(std::getenv("VSGRAYSIM_SOAK_SECONDS")) : 0;
    check(soakSeconds>=0 && soakSeconds<=43200,"invalid soak duration");
    const auto soakStart=Clock::now();
    for(;steps<(soakSeconds?10000000u:5000u) &&
         (changes.size()<100 || std::chrono::duration<double>(Clock::now()-soakStart).count()<soakSeconds);++steps) {
        // A continuous stress trajectory crosses adjacent windows and the L11/L12 threshold.
        sim.physics()->setVelocity("survey",{(steps/117)%2 ? -8000. : 8000.,
                                             (steps/29)%2 ? -900. : 900.,0});
        const auto before=sim.earthSurface();
        const uint64_t oldObserver=observerRevision ? observerRevision() : 0;
        auto start=Clock::now();
        check(sim.step(.02),"real DEM streaming step failed");
        const auto stepEnd=Clock::now();
        if(render) render(sim);
        const auto end=Clock::now();
        const auto elapsed=std::chrono::duration<double,std::milli>(end-start).count();
        if(environmentWeight) {
            float weight=environmentWeight();
            if(weight>0.f && weight<1.f) environmentTimes.push_back(elapsed);
        }
        if(observerRevision && observerRevision()!=oldObserver) observerTimes.push_back(elapsed);
        const auto surface=sim.earthSurface();
        check(sim.completedFrames().size()==1,"missing or duplicated MWIR exposure");
        const auto& product=sim.completedFrames().front();
        check(surface==published && surface->revision==sim.physics()->snapshot()->surface_revision &&
                product.surface_revision==surface->revision && product.samples.size()>0 &&
                (!steps || product.frame_id==lastFrame+1) && std::abs(product.sim_time-(steps+1)*.02)<1e-7,
                "terrain generation or independent sensor cadence mismatch");
        lastFrame=product.frame_id;
        if(before!=surface) {
            changes.push_back(elapsed);
            if(tracePath) {
                const auto ns=[](auto t){return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();};
                trace.push_back({ns(start),ns(end),surface->revision,before->declaration.at("tile")[0]!=surface->declaration.at("tile")[0]});
            }
            stepTimes.push_back(std::chrono::duration<double,std::milli>(stepEnd-start).count());
            viewTimes.push_back(elapsed-stepTimes.back());
            const bool lodChanged=before->declaration.at("tile")[0]!=surface->declaration.at("tile")[0];
            lodChanges+=lodChanged;
            (lodChanged ? lodTimes : tileTimes).push_back(elapsed);
            const auto pos=sim.physics()->snapshot()->bodies.at("survey").position;
            const auto sample=surface->sampleLocal(pos.x,pos.z);
            double height;vsg::dvec3 normal;
            check(sample.status!=earth::SurfaceStatus::NoData && sim.physics()->groundAt(pos.x,pos.z,height,normal),
                  "missing collision under the moving platform");
            maxCollisionError=std::max(maxCollisionError,std::abs(height-sample.height));
            check(maxCollisionError<.02,"Bullet differs from the pinned surface by more than 2 cm");
        } else steady.push_back(elapsed);
    }
    if(tracePath) {
        std::ofstream out;
        out.exceptions(std::ios::failbit|std::ios::badbit);
        out.open(tracePath);
        out<<"monotonic_start_ns,monotonic_end_ns,surface_revision,lod_change\n";
        for(const auto& sample:trace) out<<sample.start_ns<<','<<sample.end_ns<<','<<sample.revision<<','<<sample.lod<<'\n';
        out.close();
    }
    check(changes.size()>=100 && lodChanges>0 && lodChanges<changes.size(),"insufficient tile/LOD transitions");
    auto describe=[](std::vector<double>& times) {
        std::sort(times.begin(),times.end());
        auto at=[&](double q){return times[size_t(std::ceil(q*times.size()))-1];};
        std::cout<<" n="<<times.size()<<" p50_ms="<<at(.50)<<" p95_ms="<<at(.95)<<" max_ms="<<times.back()
                 <<" over_33ms="<<std::count_if(times.begin(),times.end(),[](double ms){return ms>33.;});
    };
    std::cout<<(render?"NATIVE viewport + ":"HEADLESS ")<<"BENCH real DEM+RGB, mounted 64x48 MWIR, step including commit, Bullet, shader and readback; LOD switches="<<lodChanges<<" switched";
    describe(changes);std::cout<<" steady";describe(steady);
    if(render) {
        std::cout<<" switched_simulation";describe(stepTimes);
        std::cout<<" switched_viewport";describe(viewTimes);
    }
    std::cout<<" LOD";describe(lodTimes);std::cout<<" adjacent";describe(tileTimes);
    if(observerRevision) {
        check(!observerTimes.empty(),"no observed geographic frame handoffs");
        std::cout<<" observer";describe(observerTimes);
    }
    if(environmentWeight) {
        check(environmentTimes.size()>=50,"insufficient global/local weather overlap frames");
        std::cout<<" weather_overlap";describe(environmentTimes);
    }
    std::cout<<" elapsed_wall_s="<<std::chrono::duration<double>(Clock::now()-soakStart).count()<<" simulated_s="<<steps*.02<<" frames="<<steps;
    const auto meetsBudget=[](const auto& sorted){return sorted.at(size_t(std::ceil(sorted.size()*.95))-1)<=33.;};
    const bool withinBudget=meetsBudget(changes) && (observerTimes.empty() || meetsBudget(observerTimes)) &&
                            (environmentTimes.empty() || meetsBudget(environmentTimes));
    std::cout<<" collision_max_error_m="<<maxCollisionError<<" budget_33ms="<<(withinBudget?"PASS":"FAIL")<<"\n";
    check(withinBudget,"terrain switch P95 exceeded 33 ms");
}
}
