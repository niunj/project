#pragma once
#include <functional>
#include <vsgraysim/earth/SurfaceQuality.h>
// Native Earth-program acceptance. Frozen draw and fixed-step coupled timings
// are deliberately separate; no screenshot/encoding work enters either.
inline void runOceanWeatherPass(vsgraysim::earth::VoxelScene &scene,
                                vsgraysim::viewport3d::VoxelSceneFeature &feature,
                                vsgraysim::viewport3d::Viewport3DView &view,
                                const std::filesystem::path &directory, const std::string &scenePath,
                                const std::function<void(vsg::dvec3, vsg::dvec3)> &camera,
                                const std::function<double()> &render, const std::function<double()> &native,
                                const std::function<void(const std::string &)> &capture) {
    using namespace vsgraysim::earth;
    using namespace vsgraysim::viewport3d;
    using json = nlohmann::json;
    auto require = [](bool b, const char *m) {
        if (!b)
            throw std::runtime_error(m);
    };
    const auto initial = scene.snapshot();
    auto *map = view.featureAs<EarthViewFeature>(EarthView::kId);
    auto *sky = view.featureAs<SkyFeature>(SkyFeature::kId);
    auto *cloud = view.featureAs<CloudFeature>(CloudFeature::kId);
    require(map && sky && cloud && initial->ocean, "weather ocean requires shared Earth/sky/cloud/ocean");
    sky->configure({{"year", 2026}, {"month", 9}, {"day", 10}, {"hours_utc", 4.}});
    camera({100, 16, -145}, {0, 0, -50});
    for (int i = 0; i < 8; ++i)
        render();
    if(std::getenv("VSGRAYSIM_SCENE_WEATHER_PASS")) {
        const bool timingOnly=std::getenv("VSGRAYSIM_WEATHER_TIMING_ONLY");
        const auto weatherCapture=[&](const std::string& name){if(!timingOnly)capture(name);};
        json report={{"source",surfaceQuality(initial->objects.at("taipei-coast").surface()).json()},
            {"physical_tick",initial->tick},{"views",json::array()}};
        scene.setPaused(true);
        camera({100,16,-145},{0,90,-850});
        const bool expanded=std::getenv("VSGRAYSIM_WEATHER_EXPANDED");
        std::vector<std::pair<std::string,double>> cases{{"clear",4},{"cloudy",4},{"overcast",4},{"rain",4},
            {"heavy_rain",4},{"snow",4},{"fog",4},{"cloudy",9.5},{"clear",16},{"rain",16},{"snow",22}};
        if(expanded)cases.insert(cases.end(),{{"fair_clouds",4},{"storm_clouds",4},{"light_rain",4},{"light_snow",4},{"heavy_snow",4}});
        for(const auto& item:cases) {
            scene.edit(json::array({{{"kind","environment"},{"weather",{{"initial",{{"preset",item.first}}}}},
                {"utc_origin",{{"year",2026},{"month",9},{"day",10},{"hours_utc",item.second},{"time_scale",1}}}}}),scene.snapshot()->revision);
            for(int warm=0;warm<10;++warm)render();
            const auto name=item.first+"-utc"+std::to_string(item.second);weatherCapture(name);
            if(!timingOnly&&(item.first=="rain"||item.first=="snow")&&item.second==4.) {
                ::setenv("VSGRAYSIM_PRECIPITATION","0",1);
                for(int warm=0;warm<6;++warm)render();weatherCapture(name+"-no-particles");
                ::unsetenv("VSGRAYSIM_PRECIPITATION");
                for(int warm=0;warm<6;++warm)render();weatherCapture(name);
                const QImage on(QString::fromStdString((directory/(name+".png")).string()));
                const QImage off(QString::fromStdString((directory/(name+"-no-particles.png")).string()));
                int changed=0;
                for(int y=0;y<on.height();++y)for(int x=0;x<on.width();++x){const auto a=on.pixelColor(x,y),b=off.pixelColor(x,y);if(std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())})>8)++changed;}
                require(changed>20,"rain/snow parameters changed but native particles were not visible");
                report["precipitation_visible_pixels"][item.first]=changed;
            }
            std::vector<double> times;
            for(int i=0;i<20;++i){render();times.push_back(native());}
            std::sort(times.begin(),times.end());
            const auto* current=map->weather();const auto authored=scene.snapshot()->weather.sample(initial->timeSeconds());
            require(std::abs(current->cover-authored.cover)<1e-5&&std::abs(current->fogVisibility-authored.visibility)<.01,"weather display diverged from scene");
            require(std::abs(current->snow-std::min(1.,authored.snow/8.))<1e-6,"snow state not rendered");
            require(scene.snapshot()->tick==initial->tick,"frozen weather capture advanced physical world");
            report["views"].push_back({{"name",name},{"utc",scene.snapshot()->utcOrigin},{"weather",SceneWeather::encodeState(authored)},
                {"eye",{100,16,-145}},{"focus",{0,90,-850}},{"render_median_ms",(times[9]+times[10])*.5},{"render_p95_ms",times[18]}});
            if(item.second==4.) {
                camera({5000,90,9500},{0,20,6500});
                for(int warm=0;warm<8;++warm)render();weatherCapture(name+"-coast");
                report["views"].back()["coast_view"]={{"file",name+"-coast.png"},{"eye",{5000,90,9500}},{"focus",{0,20,6500}}};
                camera({-7600,300,7800},{-9000,130,8600});
                for(int warm=0;warm<8;++warm)render();weatherCapture(name+"-land");
                report["views"].back()["land_view"]={{"file",name+"-land.png"},{"eye",{-7600,300,7800}},{"focus",{-9000,130,8600}}};
                if(expanded&&(authored.rain>0.||authored.snow>0.)){
                    const auto& surface=initial->objects.at("taipei-coast").surface();
                    const auto near=surface.sampleLocal(-7650,7720),ahead=surface.sampleLocal(-7820,7870);
                    require(near.status!=SurfaceStatus::NoData&&ahead.status!=SurfaceStatus::NoData,"missing near-precipitation viewpoint terrain");
                    const vsg::dvec3 eye{-7650,near.height+42,7720},focus{-7820,ahead.height+20,7870};
                    camera(eye,focus);
                    for(int warm=0;warm<8;++warm)render();weatherCapture(name+"-near");
                    report["views"].back()["near_view"]={{"file",name+"-near.png"},{"eye",{eye.x,eye.y,eye.z}},{"focus",{focus.x,focus.y,focus.z}}};
                }
                camera({100,16,-145},{0,90,-850});
            }
        }
        if(expanded&&!timingOnly){
            const auto authoredOcean=scene.snapshot()->ocean->encode();
            auto updateWeatherAndUtc=[&](const char* preset,int day,double hour){
                scene.edit(json::array({{{"kind","environment"},{"weather",{{"initial",{{"preset",preset}}}}},
                    {"utc_origin",{{"year",2026},{"month",9},{"day",day},{"hours_utc",hour},{"time_scale",1}}}}}),scene.snapshot()->revision);
            };
            auto* ephemeris=sky->sunProvider();
            require(ephemeris,"native sky ephemeris unavailable");
            const auto enu=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
            const vsg::dvec3 eye{100,16,-145};
            auto celestial=[&](const char* name,const char* preset,int day,double hour,bool moon,bool opposite=false){
                updateWeatherAndUtc(preset,day,hour);
                const auto utc=sceneUtc(scene.snapshot()->utcOrigin,initial->timeSeconds());
                ephemeris->syncDateTime(utc.year,utc.month,utc.day,utc.hours);
                rockyenv::EnvSunInfo solar,selected;
                require(ephemeris->sunInfo(enu,solar),"native sky has no solar direction");
                float phase=0.f;
                if(moon)require(ephemeris->moonInfo(enu,selected,phase),"native sky has no lunar direction");
                else selected=solar;
                const double azimuth=selected.azimuthDeg+(opposite?180.:0.);
                const auto heading=vsg::radians(azimuth);
                const auto elevation=vsg::radians(std::max(double(selected.elevationDeg),4.));
                const vsg::dvec3 ray{std::sin(heading)*std::cos(elevation),std::sin(elevation),-std::cos(heading)*std::cos(elevation)};
                camera(eye,eye+ray*1700.);
                for(int warm=0;warm<12;++warm)render();
                weatherCapture(name);
                report["celestial"].push_back({{"name",name},{"preset",preset},{"utc",scene.snapshot()->utcOrigin},
                    {"solar_elevation_deg",solar.elevationDeg},{"aim_azimuth_deg",azimuth},
                    {"aim_elevation_deg",selected.elevationDeg},{"lunar_phase",phase}});
            };
            celestial("dawn-clouds","twilight_clouds",9,21.55,false);
            celestial("sunset-clouds","twilight_clouds",10,9.85,false);
            celestial("sunset-away","twilight_clouds",10,9.85,false,true);
            celestial("sunset-overcast","overcast",10,9.85,false);
            celestial("day-sun","fair_clouds",10,4.,false);
            celestial("night-moon","clear",26,14.,true);
            updateWeatherAndUtc("clear",10,16.);
            const vsg::dvec3 starFocus{380,850,-1420};
            camera(eye,starFocus);
            for(int warm=0;warm<12;++warm)render();
            weatherCapture("night-stars");
            report["celestial"].push_back({{"name","night-stars"},{"utc",scene.snapshot()->utcOrigin}});
            updateWeatherAndUtc("overcast",10,16.);
            for(int warm=0;warm<12;++warm)render();
            weatherCapture("night-overcast");
            report["celestial"].push_back({{"name","night-overcast"},{"utc",scene.snapshot()->utcOrigin}});

            camera({100,10,-145},{0,1,-250});
            for(const int force:{0,3,5,8}){
                const auto setting=seaWindPreset(force);
                auto ocean=authoredOcean;
                applySeaWindPreset(ocean,force,70.);
                scene.edit(json::array({{{"kind","ocean"},{"ocean",std::move(ocean)}},
                    {{"kind","environment"},{"weather",{{"initial",{{"preset","cloudy"},{"wind_speed_mps",setting.windSpeed}}}}},
                     {"utc_origin",{{"year",2026},{"month",9},{"day",10},{"hours_utc",4.},{"time_scale",1}}}}}),scene.snapshot()->revision);
                for(int warm=0;warm<12;++warm)render();
                const auto waves=scene.snapshot()->ocean;
                require(std::abs(map->weather()->windSpeed-setting.windSpeed)<1e-5&&waves->waveHeight==setting.waveHeight,
                    "sea wind preset diverged between authoritative waves and renderer");
                weatherCapture("sea-wind-"+std::to_string(force));
                report["sea_wind"].push_back({{"beaufort_force",force},{"wind_mps",setting.windSpeed},
                    {"wave_parameter_m",waves->waveHeight},{"wavelength_m",waves->waveLength}});
            }
            scene.edit(json::array({{{"kind","ocean"},{"ocean",authoredOcean}}}),scene.snapshot()->revision);
            camera({100,16,-145},{0,90,-850});
        }
        scene.edit(json::array({{{"kind","environment"},{"weather",json()}}}),scene.snapshot()->revision);
        for(int warm=0;warm<6;++warm)render();
        require(map->weather()->rain==0&&map->weather()->snow==0&&map->weather()->opticalDepth==10.f&&map->weather()->overcastAmount<0,"disabled weather retained rain/snow or cloud optical depth");
        report["disable_restored_legacy_environment"]=true;
        const auto drawAt=[&](double seconds) {
            view.context().setSimulationTime(seconds);view.markActivity();
            const auto stamp=view.context().viewer()->getFrameStamp()->frameCount;
            QElapsedTimer wait;wait.start();double ms=0;
            do{QElapsedTimer timer;timer.start();view.renderFrame();view.context().viewer()->deviceWaitIdle();ms=timer.nsecsElapsed()*1e-6;
                if(view.context().viewer()->getFrameStamp()->frameCount!=stamp)break;QThread::msleep(1);
            }while(wait.elapsed()<3000);
            require(view.context().viewer()->getFrameStamp()->frameCount!=stamp,"weather animation did not submit");
            require(std::abs(map->weather()->seconds-seconds)<1e-9,"weather animation clock diverged");return ms;
        };
        const auto saveAnimation=[&](const std::string& name,int i) {
            if(timingOnly)return;
            QImage image=view.grabFrame();require(!image.isNull(),"weather animation framebuffer unavailable");
            const auto path=directory/name/(QString("frame-%1.png").arg(i,3,10,QChar('0')).toStdString());
            require(image.save(QString::fromStdString(path.string())),"weather animation save failed");
            require(scene.snapshot()->tick==initial->tick,"optical weather animation advanced collision world");
        };
        report["animation"]=json::array();
        for(const auto& kind:timingOnly?std::vector<std::string>{}:std::vector<std::string>{"rain","snow"}) {
            scene.edit(json::array({{{"kind","environment"},{"weather",{{"initial",{{"preset",kind},{"wind_speed_mps",8}}}}},
                {"utc_origin",{{"year",2026},{"month",9},{"day",10},{"hours_utc",4},{"time_scale",1}}}}}),scene.snapshot()->revision);
            for(int i=0;i<5;++i)render();
            std::filesystem::create_directories(directory/kind);
            for(int i=0;i<48;++i) {
                const double seconds=i/20.;
                drawAt(seconds);saveAnimation(kind,i);
            }
            report["animation"].push_back({{"type",kind},{"frames",48},{"simulation_hz",20},{"wind_speed_mps",8},{"wallclock_realtime_capture",false}});
        }
        const auto transitions=json::array({
            {{"at_seconds",0},{"duration_seconds",4},{"state",{{"preset","rain"}}}},
            {{"at_seconds",4},{"duration_seconds",4},{"state",{{"preset","snow"}}}}});
        const json evolving={{"initial",{{"preset","clear"}}},{"transitions",transitions}};
        scene.edit(json::array({{{"kind","environment"},{"weather",evolving}}}),scene.snapshot()->revision);
        std::filesystem::create_directories(directory/"transition");report["transition_frames"]=json::array();
        for(int i=0;i<=64;++i){
            const double t=i/8.;const auto expected=scene.snapshot()->weather.sample(t);const double ms=drawAt(t);
            const auto* actual=map->weather();
            require(std::abs(actual->cover-expected.cover)<1e-5&&std::abs(actual->rain-std::min(1.,expected.rain/40.))<1e-5&&std::abs(actual->snow-std::min(1.,expected.snow/8.))<1e-5,"GPU weather transition differs from scene curve");
            saveAnimation("transition",i);report["transition_frames"].push_back({{"seconds",t},{"render_ms",ms},{"cover",actual->cover},{"rain",actual->rain},{"snow",actual->snow}});
        }
        if(!timingOnly&&std::getenv("VSGRAYSIM_WEATHER_VISUAL_DETAILS")) {
            scene.edit(json::array({{{"kind","environment"},{"weather",{{"initial",{{"preset","heavy_rain"}}}}}}}),
                scene.snapshot()->revision);
            const vsg::dvec3 eye(100,2.6,-145),target(86,-.4,-162);
            camera(eye,target);
            const std::string path="rain-waterline";
            std::filesystem::create_directories(directory/path);
            for(int i=0;i<6;++i)drawAt(1.);
            for(int i=0;i<40;++i){drawAt(1.+i/20.);saveAnimation(path,i);}
            report["rain_waterline"]={{"eye",{eye.x,eye.y,eye.z}},{"focus",{target.x,target.y,target.z}},
                {"start_seconds",1.},{"sample_hz",20},{"frames",40},{"utc",scene.snapshot()->utcOrigin}};
        }
        if(std::getenv("VSGRAYSIM_WEATHER_COUPLED")) {
            // The same occupied bodies, terrain contacts, wakes, fixed step
            // and native renderer execute sequentially. No image IO is timed.
            report["coupled_frames"]=json::array();
            unsigned rayChecks=0;double maxRangeError=0;
            for(const auto& kind:{"rain","snow","transition"}) {
                const double base=scene.snapshot()->timeSeconds();
                json weather={{"initial",{{"preset",std::string(kind)=="transition"?"clear":kind}}}};
                if(std::string(kind)=="transition")weather["transitions"]=json::array({
                    {{"at_seconds",base},{"duration_seconds",.8},{"state",{{"preset","rain"}}}},
                    {{"at_seconds",base+.8},{"duration_seconds",.8},{"state",{{"preset","snow"}}}}});
                scene.edit(json::array({{{"kind","environment"},{"weather",weather}}}),scene.snapshot()->revision);
                for(int warm=0;warm<8;++warm)render();
                for(int i=1;i<=40;++i) {
                    QElapsedTimer total;total.start();QElapsedTimer compute;compute.start();
                    scene.setPaused(false);const auto step=scene.advanceTo(base+i*.04,8);scene.setPaused(true);
                    const double simulation=compute.nsecsElapsed()*1e-6;
                    require(!step.pendingSteps,"weather full simulation dropped collision substeps");
                    render();const double whole=total.nsecsElapsed()*1e-6;
                    const auto state=scene.snapshot();
                    for(const auto& [id,o]:state->objects)
                        require(feature.display().placements.at(id)->matrix==o.pose(state->tick,state->fixedStep),"weather full cycle display/body pose mismatch");
                    if(i%10==0&&state->objects.count("vessel")) {
                        const auto& body=state->objects.at("vessel");
                        const auto centre=body.pose(state->tick,state->fixedStep)*body.localCentreOfMass();
                        const auto eye=centre+vsg::dvec3(0,80,0);
                        const auto a=scene.raycast(eye,{0,-1,0},160),b=scene.rigidRaycast(eye,{0,-1,0},160);
                        require(bool(a)==bool(b),"weather complete cycle ray presence mismatch");
                        if(a&&a->object=="vessel") {
                            require(b->object==a->object&&std::abs(a->distance-b->distance)<.001,"weather complete cycle collision/range mismatch");
                            ++rayChecks;maxRangeError=std::max(maxRangeError,std::abs(a->distance-b->distance));
                        }
                    }
                    report["coupled_frames"].push_back({{"weather",kind},{"seconds",state->timeSeconds()},
                        {"simulation_interval_ms",40},{"fixed_step_seconds",state->fixedStep},{"steps",step.steps},
                        {"simulation_ms",simulation},{"buoyancy_ms",step.buoyancyMs},{"water_ms",step.waterMs},
                        {"render_ms",native()},{"complete_active_ms",simulation+native()},
                        {"complete_wall_ms",whole},{"pending_steps",step.pendingSteps}});
                }
                weatherCapture(std::string("coupled-")+kind);
            }
            require(rayChecks>=3,"full weather cycle did not exercise target range/collision queries");
            report["coupled_collision_checks"]={{"target_rays",rayChecks},{"max_range_error_m",maxRangeError},{"pose_exact",true}};
        }
        std::ofstream(directory/"scene-weather.json")<<report.dump(2)<<'\n';
        std::cout<<"PASS: native Taipei shared time/weather clear/cloud/overcast/rain/snow/fog and night views\n";return;
    }
    if (std::getenv("VSGRAYSIM_OCEAN_SHIMMER_PASS")) {
        json report = {{"source", surfaceQuality(initial->objects.at("taipei-coast").surface()).json()},
                       {"scene_seconds", initial->timeSeconds()}, {"views", json::array()}};
        const auto local = vsg::inverse(simcore::geo::makeEnuToWorldMatrix(initial->anchor));
        const vsg::dvec3 target(700, 0, 700);
        for (const auto &[name, hour] : std::vector<std::pair<std::string, double>>{
                 {"day", 4.}, {"sunset", 9.5}, {"night", 20.}}) {
            sky->configure({{"year", 2026}, {"month", 9}, {"day", 10}, {"hours_utc", hour}});
            for (int i = 0; i < 3; ++i)
                render();
            const auto source = name == "night" ? map->weather()->moon : map->weather()->sun;
            const auto light4 = local * vsg::dvec4(source.x, source.y, source.z, 0.);
            const vsg::dvec3 light(light4.x, light4.y, light4.z);
            const double horizontal = std::max(.01, std::hypot(light.x, light.z));
            const vsg::dvec3 direction(light.x / horizontal, 0, light.z / horizontal);
            const vsg::dvec3 side(-direction.z, 0, direction.x);
            const auto origin = target - direction * 120. +
                vsg::dvec3(0, std::clamp(120. * light.y / horizontal, 3., 3000.), 0);
            for (int i = 0; i < 16; ++i) {
                const auto eye = origin + side * (.03 * i);
                camera(eye, target);
                for (int warm = 0; warm < (i ? 0 : 8); ++warm)
                    render();
                render();
                const auto ms = native();
                const auto tag = name + "-" + std::to_string(i);
                capture(tag);
                report["views"].push_back({{"name", tag}, {"hours_utc", hour},
                    {"eye", {eye.x, eye.y, eye.z}}, {"focus", {target.x, target.y, target.z}},
                    {"light_local", {light.x, light.y, light.z}}, {"render_ms", ms}});
            }
        }
        require(scene.snapshot()->tick == initial->tick, "optical shimmer test advanced physics");
        std::ofstream(directory / "shimmer.json") << report.dump(2) << '\n';
        std::cout << "PASS: native frozen ocean subpixel camera sweep\n";
        return;
    }
    if (std::getenv("VSGRAYSIM_CLOUD_HALO_PASS")) {
        json report = {{"utc", {{"year", 2026}, {"month", 9}, {"day", 10}, {"hours_utc", 4.}}},
                       {"source", surfaceQuality(initial->objects.at("taipei-coast").surface()).json()},
                       {"views", json::array()}};
        auto shot = [&](const char *name, double hour, vsg::dvec3 eye, vsg::dvec3 focus) {
            sky->configure({{"year", 2026}, {"month", 9}, {"day", 10}, {"hours_utc", hour}});
            camera(eye, focus);
            for (int i = 0; i < 8; ++i)
                render();
            capture(name);
            std::vector<double> times;
            for (int i = 0; i < 15; ++i) {
                render();
                times.push_back(native());
            }
            std::sort(times.begin(), times.end());
            report["views"].push_back({{"name", name}, {"hours_utc", hour},
                                       {"eye", {eye.x, eye.y, eye.z}},
                                       {"focus", {focus.x, focus.y, focus.z}},
                                       {"render_median_ms", times[7]},
                                       {"render_p95_ms", times[14]}});
        };
        shot("low-day", 4., {100, 16, -145}, {0, 0, -50});
        shot("aerial-day", 4., {650, 180, -550}, {0, 0, -20});
        shot("sky-day", 4., {100, 16, -145}, {0, 450, -1600});
        shot("sunset-sky", 9.5, {100, 16, -145}, {0, 450, -1600});
        shot("sunset-sea", 9.5, {100, 16, -145}, {0, 0, -50});
        std::ofstream(directory / "cloud-halo.json") << report.dump(2) << '\n';
        std::cout << "PASS: native frozen Taipei cloud/reflection halo views\n";
        return;
    }
    json report = {{"scene", scenePath},
                   {"utc", {{"year", 2026}, {"month", 9}, {"day", 10}, {"hours_utc", 4.}}},
                   {"source", surfaceQuality(initial->objects.at("taipei-coast").surface()).json()},
                   {"physical_resolution_m", initial->objects.at("vessel").volume->resolution_m},
                   {"render_frames", json::array()},
                   {"frames", json::array()},
                   {"weather_cases", json::array()}};
    capture("initial");
    for (int i = 0; i < 30; ++i) {
        render();
        report["render_frames"].push_back(native());
    }
    const auto reflectionStart=(*map->cloudResources()->reflectionState)[0].w;
    const auto &ship = initial->objects.at("vessel");
    const auto c = ship.localCentreOfMass();
    scene.schedule({{"kind", "impulse"},
                    {"object", "vessel"},
                    {"tick", 151},
                    {"space", "object"},
                    {"point_m", {c.x, c.y, c.z}},
                    {"impulse_ns", {0, 0, -ship.massKg() * 3.}}});
    double rangeError = 0,maxInclination=0;
    size_t checks = 0;
    for (int i = 1; i <= 100; ++i) {
        QElapsedTimer timer;
        timer.start();
        scene.setPaused(false);
        auto step = scene.advanceTo(i * .1, 8);
        scene.setPaused(true);
        require(!step.pendingSteps, "weather ocean dropped fixed physics steps");
        double advance = timer.nsecsElapsed() / 1e6;
        render();
        const double draw = native();
        const auto state = scene.snapshot();
        const auto &body = state->objects.at("vessel");
        const auto bodyPose=body.pose(state->tick,state->fixedStep);const double inclination=std::acos(std::clamp(bodyPose[1][1],-1.,1.))*180/vsg::PI;maxInclination=std::max(maxInclination,inclination);
        for (const auto &[id, o] : state->objects)
            require(feature.display().placements.at(id)->matrix == o.pose(state->tick, state->fixedStep),
                    "ocean display/rigid pose differs");
        if (i % 5 == 0)
            for (double z : {-40., 0., 40.}) {
                auto a = scene.raycast({0, 100, z}, {0, -1, 0}, 150),
                     b = scene.rigidRaycast({0, 100, z}, {0, -1, 0}, 150);
                require(bool(a) == bool(b), "ocean visible/rigid ray mismatch");
                if (a && a->object == "vessel") {
                    require(b->object == a->object, "ocean hit object mismatch");
                    rangeError = std::max(rangeError, std::abs(a->distance - b->distance));
                    ++checks;
                }
            }
        report["frames"].push_back({{"seconds", state->timeSeconds()},
                                    {"simulation_interval_ms", 100},
                                    {"advance_wall_ms", advance},
                                    {"buoyancy_wall_ms", step.buoyancyMs},
                                    {"water_total_wall_ms", step.waterMs},
                                    {"render_submit_wait_ms", draw},
                                    {"complete_wall_ms", advance + draw},
                                    {"pose", {body.basePose[3].x, body.basePose[3].y, body.basePose[3].z}},
                                    {"velocity", {body.velocity.x, body.velocity.y, body.velocity.z}},
                                    {"inclination_deg",inclination},{"wakes", state->ocean->wakes.size()}});
        if (i == 30 || i == 60 || i == 100)
            capture("wake-" + std::to_string(i));
    }
    require(checks > 10 && rangeError < .001, "weather ocean collider/range exceeds 1mm");
    report["reflection_updates_during_10s"]=(*map->cloudResources()->reflectionState)[0].w-reflectionStart;
    report["maximum_inclination_deg"]=maxInclination;report["ballast_mass_kg"]=ship.body.ballastMassKg;
    if(ship.body.ballastMassKg>0)require(maxInclination<15.,"ballasted cruiser exceeded 15 degree heel/pitch");
    report["range_checks"] = checks;
    report["maximum_range_error_m"] = rangeError;
    const auto frozen = scene.snapshot();
    const auto water = frozen->ocean->encode();
    auto weather = [&](const char *name, double hour, float cover, double fog) {
        auto o = water;
        o["sea_fog"] = {{"visibility_m", fog}, {"height_m", 45}};
        scene.edit(json::array({{{"kind", "ocean"}, {"ocean", o}}}), scene.snapshot()->revision);
        sky->configure({{"year", 2026}, {"month", 9}, {"day", 10}, {"hours_utc", hour}});
        cloud->setAtmosphereParams({{"cover", cover}, {"volume", true}, {"wind_speed", 6}, {"wind_az", 70}});
        camera({100, 16, -145}, {0, 0, -50});
        for (int j = 0; j < 8; ++j)
            render();
        require(map->weather() && std::abs(map->weather()->cover - cover) < 1e-6,
                "runtime cloud cover did not reach Earth weather");
        capture(name);
        json times = json::array();
        for (int j = 0; j < 15; ++j) {
            render();
            times.push_back(native());
        }
        report["weather_cases"].push_back({{"name", name},
                                           {"hours_utc", hour},
                                           {"cover", cover},
                                           {"observed_cover", map->weather()->cover},
                                           {"fog_visibility_m", fog},
                                           {"render_ms", times}});
    };
    weather("cloud-reflection", 4., .65f, 0);
    weather("clear-reflection", 4., 0, 0);
    weather("sunset-reflection", 9.5, .65f, 0);
    weather("night-reflection", 16., .65f, 0);
    weather("fog-reflection", 4., .85f, 1800);
    scene.saveCheckpoint((directory / "checkpoint.json").string());
    // Additional reference-angle evidence, separate from paired low-camera timings.
    {auto o=water;o["sea_fog"]={{"visibility_m",0},{"height_m",45}};scene.edit(json::array({{{"kind","ocean"},{"ocean",o}}}),scene.snapshot()->revision);cloud->setAtmosphereParams({{"cover",.65}});sky->configure({{"year",2026},{"month",9},{"day",10},{"hours_utc",4.}});camera({650,180,-550},{0,0,-20});for(int i=0;i<4;++i)render();capture("coast-aerial");camera({100,16,-145},{0,0,-50});}
    std::ofstream(directory/"weather-ocean.partial.json")<<report.dump(2)<<'\n';
    if(std::getenv("VSGRAYSIM_OCEAN_LONG_STABILITY")){
        auto o=water;o["sea_fog"]={{"visibility_m",0},{"height_m",45}};scene.edit(json::array({{{"kind","ocean"},{"ocean",o}}}),scene.snapshot()->revision);
        cloud->setAtmosphereParams({{"cover",.65}});sky->configure({{"year",2026},{"month",9},{"day",10},{"hours_utc",4.}});
        report["long_stability"]=json::array();double peak=maxInclination;
        for(int i=101;i<=600;++i){scene.setPaused(false);const auto step=scene.advanceTo(i*.1,8);scene.setPaused(true);require(!step.pendingSteps,"long stability lost time");render();const auto state=scene.snapshot();const auto matrix=state->objects.at("vessel").pose(state->tick,state->fixedStep);const double angle=std::acos(std::clamp(matrix[1][1],-1.,1.))*180/vsg::PI;peak=std::max(peak,angle);require(angle<15.,"cruiser lost stability in 60s native run");report["long_stability"].push_back({{"seconds",state->timeSeconds()},{"inclination_deg",angle},{"buoyancy_wall_ms",step.buoyancyMs}});if(i==200||i==400||i==600){capture("stable-"+std::to_string(i));report["long_maximum_inclination_deg"]=peak;std::ofstream(directory/"weather-ocean.partial.json")<<report.dump(2)<<'\n';}}
        report["long_maximum_inclination_deg"]=peak;scene.saveCheckpoint((directory/"long-checkpoint.json").string());
    }
    if(std::getenv("VSGRAYSIM_OCEAN_LONG_STABILITY")){
        report["live_frames"]=json::array();const double base=scene.snapshot()->timeSeconds();QElapsedTimer pacing;pacing.start();
        for(int i=1;i<=90;++i){while(pacing.nsecsElapsed()/1e6<i*1000./30.)QThread::msleep(1);QElapsedTimer timer;timer.start();scene.setPaused(false);const auto step=scene.advanceTo(base+i/30.,8);scene.setPaused(true);require(!step.pendingSteps,"paced stability test dropped physics ticks");const double ms=timer.nsecsElapsed()/1e6;render();report["live_frames"].push_back({{"advance_wall_ms",ms},{"render_submit_wait_ms",native()},{"buoyancy_wall_ms",step.buoyancyMs},{"complete_wall_ms",ms+native()}});}
        report["live_simulation_seconds"]=3.;report["live_wall_seconds"]=pacing.nsecsElapsed()/1e9;
    }
    report["scope"] = "actual 960x540 Vulkan frames; native update/submit/deviceWaitIdle; 100 coupled "
                      "samples each 100ms of fixed-step simulation; excludes screenshots, IO and warmup; "
                      "complete=advance+native render, not GPU-only timing";
    std::ofstream(directory / "weather-ocean.json") << report.dump(2) << '\n';
    std::cout << "PASS: Taipei ocean weather, coupled fixed steps, native draw, surface collision/ranges\n";
}
