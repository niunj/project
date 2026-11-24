#pragma once
inline void sceneWeatherChecks() {
    const auto weather=json{{"initial",{{"preset","clear"},{"wind_azimuth_deg",350}}},
        {"transitions",{{{"at_seconds",10},{"duration_seconds",20},{"state",{{"preset","rain"},{"wind_azimuth_deg",10}}}},
                         {{"at_seconds",40},{"duration_seconds",20},{"state",{{"preset","snow"}}}}}}};
    auto document=definition();document["weather"]=weather;document["utc_origin"]={{"year",2024},{"month",2},{"day",28},{"hours_utc",23.5},{"time_scale",2}};
    auto world=VoxelScene::load(document);const auto before=world->snapshot();
    check(before->weather.sample(0).rain==0&&before->weather.sample(35).rain==6,"weather timeline missed clear/rain endpoints");
    const auto mid=before->weather.sample(20);
    check(mid.rain>0&&mid.rain<6&&(mid.windAzimuth<1e-9||mid.windAzimuth>359.999),"weather wind did not take shortest north crossing");
    const auto p=before->weather.windTravel(20),q=before->weather.windTravel(20.001);
    check(std::abs((q[1]-p[1])/.001-mid.windSpeed)<.002&&std::abs(q[0]-p[0])<.001,"weather airflow was multiplied by current wind instead of integrated");
    check(before->weather.sample(60).snow==2&&before->weather.sample(60).rain==0,"weather did not transition to snow");
    const auto frozen=before->weather.sample(before->timeSeconds());world->advanceTo(100,8);
    check(world->snapshot()==before&&world->snapshot()->weather.sample(0).rain==frozen.rain,"paused weather advanced by wall clock");
    const auto utc=sceneUtc(before->utcOrigin,1800);
    check(utc.year==2024&&utc.month==2&&utc.day==29&&std::abs(utc.hours-.5)<1e-12,"weather UTC leap-day/rate failed");
    const auto back=sceneUtc(before->utcOrigin,-86400);
    check(back.month==2&&back.day==26&&back.hours==23.5,"negative scene UTC failed");
    auto clone=VoxelScene::load(world->checkpoint());
    check(clone->snapshot()->weather.encode()==before->weather.encode()&&clone->snapshot()->utcOrigin==before->utcOrigin,"checkpoint/IPC dropped weather or clock");
    auto invalid=weather;invalid["transitions"][1]["at_seconds"]=20;
    rejects([&]{world->edit(json::array({{{"kind","environment"},{"utc_origin",{{"year",2026},{"month",1},{"day",1},{"hours_utc",0}}},{"weather",invalid}}}),before->revision);},"overlapping weather accepted");
    check(world->snapshot()==before,"failed environment edit partially changed UTC");
    world->edit(json::array({{{"kind","environment"},{"weather",{{"initial",{{"preset","fog"}}}}}}}),before->revision);
    check(world->snapshot()->contentRevision==before->contentRevision&&world->snapshot()->fixedStep==before->fixedStep,"weather edit changed physical resolution/topology");
    world->setPaused(false);world->advanceTo(.02,1);
    check(world->snapshot()->tick==1&&world->snapshot()->weather.sample(.02).visibility==220,"weather step/checkpoint cadence differs");
    for(const auto& name:{"clear","fair_clouds","cloudy","overcast","storm_clouds","twilight_clouds",
                         "light_rain","rain","moderate_rain","heavy_rain","light_snow","snow","moderate_snow","heavy_snow","fog"}) {
        const auto state=SceneWeather::preset(name);check(std::isfinite(state.visibility)&&state.cloudTop>state.cloudBase,"invalid weather preset");
    }
    for(const auto& family:{std::array<const char*,3>{"light_rain","moderate_rain","heavy_rain"},
                           std::array<const char*,3>{"light_snow","moderate_snow","heavy_snow"}}){
        const auto light=SceneWeather::preset(family[0]),medium=SceneWeather::preset(family[1]),heavy=SceneWeather::preset(family[2]);
        check(light.precipitation()<medium.precipitation()&&medium.precipitation()<heavy.precipitation()&&
              light.visibility>medium.visibility&&medium.visibility>heavy.visibility,"precipitation tiers are visually degenerate");
    }
    auto water=VoxelOcean::load(json::object());
    for(int level=0;level<=8;++level){
        auto config=water->encode();applySeaWindPreset(config,level,120.);
        const auto sea=VoxelOcean::load(config);
        const auto expected=seaWindPreset(level);
        check(sea->windSpeed==expected.windSpeed&&sea->waveHeight==expected.waveHeight&&
              sea->windAzimuth==120.&&sea->harmonics.empty()==(level==0),"sea preset did not regenerate its physical waves");
        if(level>0)check(expected.windSpeed>seaWindPreset(level-1).windSpeed&&expected.waveHeight>seaWindPreset(level-1).waveHeight,
                         "sea presets did not increase wind and waves");
    }
    rejects([&]{SceneWeather::load({{"initial",{{"preset","snwo"}}}});},"unknown weather preset silently accepted");
    rejects([&]{SceneWeather::load({{"initial",{{"rain_mm_h",-1}}}});},"negative rain accepted");
    rejects([&]{SceneWeather::load({{"initial",{{"cloud_base_m",4000},{"cloud_top_m",3000}}}});},"inverted clouds accepted");
    std::cout<<"PASS: unified scene weather transitions, UTC rollover, pause/step, atomic edit and checkpoint/IPC persistence\n";
}
