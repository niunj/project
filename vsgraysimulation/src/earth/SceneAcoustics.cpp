#include <vsgraysim/earth/SceneAcoustics.h>
#include <vsgraysim/earth/VoxelOcean.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace vsgraysim::earth {
namespace {
using Json=nlohmann::json;
constexpr AcousticBands weighting{-26.2,-16.1,-8.6,-3.2,0.,1.2,1.,-1.1};
constexpr AcousticBands obstacleLoss{3.,4.,6.,9.,12.,16.,20.,24.};
constexpr AcousticBands groundLoss{3.,3.,4.,4.,5.,6.,8.,10.};

double decibels(double energy) {
    return energy>0.?10.*std::log10(energy):-INFINITY;
}

vsg::dvec3 vector(const Json& value) {
    const auto xyz=value.get<std::array<double,3>>();
    const vsg::dvec3 result(xyz[0],xyz[1],xyz[2]);
    if(!std::isfinite(vsg::length(result)))throw std::invalid_argument("nonfinite acoustic vector");
    return result;
}

AcousticBands spectrum(const Json& row) {
    const auto levels=row.at("spl_bands_db").get<AcousticBands>();
    if(std::any_of(levels.begin(),levels.end(),[](double level){return !std::isfinite(level)||level<-50.||level>160.;}))
        throw std::invalid_argument("invalid acoustic spectrum");
    return levels;
}

bool bounded(double v,double low,double high) {return std::isfinite(v)&&v>=low&&v<=high;}

std::optional<VoxelSceneHit> environmentHit(const VoxelSceneSnapshot& scene,const vsg::dvec3& origin,
    const vsg::dvec3& direction,double limit) {
    std::optional<VoxelSceneHit> nearest;
    for(const auto& [id,object]:scene.objects){
        if(object.role!="terrain"&&object.role!="structure")continue;
        if(const auto hit=raycastVoxelScene(scene,origin,direction,nearest?nearest->distance:limit,false,id))
            nearest=hit;
    }
    return nearest;
}

bool blocked(const VoxelSceneSnapshot& scene,const vsg::dvec3& from,const vsg::dvec3& to) {
    const auto direction=to-from;
    const auto distance=vsg::length(direction);
    constexpr double endTolerance=.05;
    return distance>2*endTolerance&&bool(environmentHit(scene,
        from+direction*(endTolerance/distance),direction,distance-2*endTolerance));
}

bool inWater(const VoxelSceneSnapshot& scene,const vsg::dvec3& p) {
    return scene.ocean&&scene.ocean->contains(p.x,p.z)&&p.y>scene.ocean->bottom&&
        p.y<scene.ocean->surface(p.x,p.z,scene.timeSeconds()).height;
}

// ISO 9613-1 atmospheric absorption, band centres, dB per metre.
// Approximate pressure at the path's mean elevation enters as p / p0.
AcousticBands atmosphericLoss(double temperature,double humidity,double height) {
    const double t=std::clamp(temperature,253.15,323.15);
    const double tr=t/293.15, pr=std::exp(-std::clamp(height,-500.,5000.)/8434.);
    const double ps=std::pow(10.,4.6151-6.8346*std::pow(273.16/t,1.261));
    const double h=100.*std::clamp(humidity,.10,1.)*ps/pr;
    const double oxygen=pr*(24.+40400.*h*(.02+h)/(.391+h));
    const double nitrogen=pr/std::sqrt(tr)*(9.+280.*h*std::exp(-4.17*(std::cbrt(1./tr)-1.)));
    AcousticBands loss{};
    for(size_t i=0;i<loss.size();++i){
        const double f2=acousticBands[i]*acousticBands[i];
        loss[i]=8.686*f2*(1.84e-11*std::sqrt(tr)/pr+std::pow(tr,-2.5)*
            (.01275*std::exp(-2239.1/t)/(oxygen+f2/oxygen)+.1068*std::exp(-3352./t)/(nitrogen+f2/nitrogen)));
    }
    return loss;
}

std::optional<double> reflector(const VoxelSceneSnapshot& scene,const vsg::dvec3& a,const vsg::dvec3& b) {
    auto centre=(a+b)*.5;
    centre.y=std::min(a.y,b.y)+.05;
    const auto hit=environmentHit(scene,centre,{0.,-1.,0.},500.);
    if(hit&&hit->normal.y>.95&&hit->position.y<std::min(a.y,b.y)-.05)return hit->position.y;
    return {};
}

// Spectra use the mixer's common pressure-energy scale, (Pa / 20 µPa)^2.
// The displayed datum is chosen independently for the receiving medium.
void addOceanPath(AcousticReading& reading,AcousticPath path,const AcousticBands& spectrum,double gainDb) {
    for(size_t i=0;i<spectrum.size();++i){
        path.bandEnergy[i]=std::pow(10.,(spectrum[i]+gainDb)/10.);
        reading.bandEnergy[i]+=path.bandEnergy[i];
    }
    reading.paths.push_back(std::move(path));
}

void oceanAmbience(const VoxelSceneSnapshot& scene,const vsg::dvec3& receiver,AcousticReading& reading,
                  bool wet,double weight) {
    if(!scene.ocean||weight<1.e-5||receiver.y<scene.ocean->bottom)return;
    const auto& water=*scene.ocean;
    const double now=scene.timeSeconds();
    const double localLevel=water.surface(receiver.x,receiver.z,now).height;
    if(receiver.y-localLevel>220.)return;
    const auto weather=scene.weather.sample(now);
    const double wind=scene.weather.enabled?weather.windSpeed:water.windSpeed;
    const double seaEnergy=std::clamp(.14*wind+.8*water.waveHeight+.28*water.swellHeight,0.,6.);
    const double seaGain=9.*std::log2(1.+seaEnergy);
    const double mixing=10.*std::log10(weight);
    const double soundSpeed=wet?1480.:331.3*std::sqrt(weather.temperature/273.15);
    const AcousticBands surfaceBands{38.,44.,48.,50.,50.,46.,40.,32.};
    const AcousticBands submergedBands{49.,49.,46.,42.,35.,27.,19.,10.};
    const vsg::dvec3 directions[]={{1,0,0},{0,0,1},{-1,0,0},{0,0,-1}};
    for(unsigned i=0;i<4;++i){
        for(double radius:{14.,42.}){
            const auto horizontal=receiver+directions[i]*radius;
            if(!water.contains(horizontal.x,horizontal.z))continue;
            const auto surface=water.surface(horizontal.x,horizontal.z,now);
            vsg::dvec3 point{horizontal.x,surface.height+(wet?-.32:.17),horizontal.z};
            auto sky=point;
            sky.y=std::max(surface.height+100.,receiver.y+3.);
            if(environmentHit(scene,sky,{0,-1,0},sky.y-surface.height-.04))continue;
            const auto offset=point-receiver;
            const double distance=vsg::length(offset),delay=distance/soundSpeed;
            if(now+1.e-8<delay)continue;
            double gain=seaGain-5.-10.*std::log10(1.+distance*distance/320.);
            if(blocked(scene,point,receiver))gain-=wet?28.:18.;
            const double aboveMean=surface.height-water.meanSurface(point.x,point.z).x;
            gain+=std::clamp(aboveMean*2.,-2.5,2.5);
            AcousticPath path;
            path.key=(wet?"sea:water:":"sea:air:")+std::to_string(i);
            path.cue=wet?"sea_underwater":"sea_surface";
            path.sourcePosition=point;path.distance=distance;path.delay=delay;
            path.emissionTime=now-delay;path.arrivalDirection=offset/distance;
            path.playbackOffset=path.emissionTime+1.19*i;
            path.pitch=.89+.073*i;
            path.underwater=wet;
            path.highBandLoss=wet?9.:std::clamp((distance-15.)*.055,0.,14.)+12.*reading.submergedMix;
            addOceanPath(reading,std::move(path),wet?submergedBands:surfaceBands,gain+mixing);
            break;
        }
    }
    const AcousticBands splashBands{43.,50.,57.,62.,62.,57.,51.,43.};
    const AcousticBands wetSplashBands{55.,56.,52.,48.,40.,27.,20.,13.};
    for(const auto& splash:water.splashes){
        const vsg::dvec3 point{splash.x,splash.height+(wet?-.32:.17),splash.z};
        const auto offset=point-receiver;
        const double distance=vsg::length(offset),delay=distance/soundSpeed;
        const double age=now-splash.birth-delay;
        if(distance>80.||age<0.||age>=.42)continue;
        const bool screened=!wet&&blocked(scene,point,receiver);
        AcousticPath path;
        path.key="sea:slap:"+std::to_string(splash.seed)+":"+
            std::to_string(std::llround(splash.birth/scene.fixedStep));
        if(wet)path.key+="/water";
        path.cue="sea_slap";path.oneShot=true;path.screened=screened;
        path.underwater=wet;
        path.sourcePosition=point;path.distance=distance;path.delay=delay;
        path.emissionTime=splash.birth;path.playbackOffset=age;
        if(distance>.001)path.arrivalDirection=offset/distance;
        path.pitch=wet?.70:std::clamp(.88+.06*splash.speed,.9,1.22);
        path.highBandLoss=wet?16.:screened?14.:0.;
        const double gain=14.*std::log10(std::max(.5,splash.speed)/2.)-
            20.*std::log10(std::max(2.,distance)/5.)-(screened?18.:0.);
        addOceanPath(reading,std::move(path),wet?wetSplashBands:splashBands,gain+mixing);
    }
}
}

double AcousticReading::dbSpl()const {
    return decibels(std::accumulate(bandEnergy.begin(),bandEnergy.end(),0.)*(underwater?400.:1.));
}

double AcousticReading::dbA()const {
    if(underwater)return -INFINITY;
    double energy=0.;
    for(size_t i=0;i<bandEnergy.size();++i)energy+=bandEnergy[i]*std::pow(10.,weighting[i]/10.);
    return decibels(energy);
}

nlohmann::json AcousticReading::encode()const {
    AcousticBands levels{};
    const double datum=underwater?400.:1.;
    for(size_t i=0;i<levels.size();++i)levels[i]=decibels(bandEnergy[i]*datum);
    auto entries=Json::array();
    for(const auto& p:paths)entries.push_back({{"id",p.key},{"cue",p.cue},{"medium",p.underwater?"water":"air"},
        {"emission_s",p.emissionTime},{"delay_s",p.delay},
        {"source_position_m",{p.sourcePosition.x,p.sourcePosition.y,p.sourcePosition.z}},{"path_length_m",p.distance},
        {"doppler",p.doppler},{"spl_db",decibels(std::accumulate(p.bandEnergy.begin(),p.bandEnergy.end(),0.)*datum)},
        {"reflected",p.reflected},{"screened",p.screened},{"estimated_source",p.estimated}});
    return {{"spl_db",dbSpl()},{"dba",underwater?Json(nullptr):Json(dbA())},
        {"medium",underwater?"water":"air"},{"submerged_mix",submergedMix},{"reference_pressure_pa",referencePressure()},
        {"pressure_rms_pa",20e-6*std::sqrt(std::accumulate(bandEnergy.begin(),bandEnergy.end(),0.))},
        {"band_hz",acousticBands},{"band_spl_db",levels},{"paths",std::move(entries)}};
}

void AcousticMeter::observe(double seconds,const AcousticReading& reading) {
    const double energy=std::pow(10.,reading.dbA()/10.);
    if(last_<0.||seconds<last_||underwater_!=reading.underwater){
        *this={};last_=seconds;previous_=energy;underwater_=reading.underwater;return;
    }
    if(underwater_)return;
    const double dt=seconds-last_;
    if(dt<=0.)return;
    const double mean=(previous_+energy)*.5;
    elapsed_+=dt;exposure_+=mean*dt;
    fast_+=(mean-fast_)*(-std::expm1(-dt/.125));
    maximum_=std::max(maximum_,fast_);
    last_=seconds;previous_=energy;
}

nlohmann::json AcousticMeter::encode()const {
    if(underwater_)return {{"window_s",0.},{"laeq_db",nullptr},{"lafmax_estimate_db",nullptr},{"sel_db",nullptr}};
    return {{"window_s",elapsed_},{"laeq_db",decibels(elapsed_>0.?exposure_/elapsed_:0.)},
        {"lafmax_estimate_db",decibels(maximum_)},{"sel_db",decibels(exposure_)}};
}

void SceneAcoustics::ingest(const VoxelSceneSnapshot& scene,const Json& data,std::string_view epoch) {
    const double now=scene.timeSeconds();
    if(epoch_!=epoch||now<lastTime_){tracks_.clear();epoch_=epoch;lastTime_=-1.;}
    if(now==lastTime_)return;
    lastTime_=now;
    for(auto& [key,track]:tracks_)track.seen=false;
    for(const auto name:{"loops","events"}){
        const auto list=data.find(name);
        if(list==data.end()||!list->is_array())continue;
        const bool event=std::string_view(name)=="events";
        for(const auto& item:*list)try{
            if(!item.contains("spl_bands_db"))continue;
            const auto medium=item.value("medium",std::string("air"));
            if(medium!="air"&&medium!="water")continue;
            const auto key=item.at("key").get<std::string>();
            Emission s;
            s.cue=item.at("cue").get<std::string>();
            s.oneShot=event;
            s.underwater=medium=="water";
            s.time=event?item.value("emission_s",double(item.value("tick",scene.tick))*scene.fixedStep):now;
            s.position=vector(item.at("position_m"));
            if(item.contains("velocity_mps"))s.velocity=vector(item.at("velocity_mps"));
            if(item.contains("forward"))s.forward=vector(item.at("forward"));
            s.bands=spectrum(item);
            s.reference=item.value("acoustic_reference_m",10.);
            s.minimumDistance=item.value("min_distance_m",std::min(1.,s.reference));
            s.pitch=item.value("pitch",1.);
            s.duration=event?item.value("duration_s",.35):0.;
            s.directivity=item.value("rear_loss_db",0.);
            s.estimated=item.value("estimated_source",true);
            if(key.empty()||key.size()>192||s.cue.size()>48||!bounded(s.reference,.1,1000.)||
                !bounded(s.minimumDistance,.1,s.reference)||
                !bounded(s.pitch,s.cue=="sea_propeller"?.01:.25,4.)||
                !bounded(s.duration,0.,10.)||!bounded(s.directivity,0.,40.)||
                !bounded(now-s.time,-1e-6,maxPathSeconds+10.)||inWater(scene,s.position)!=s.underwater)continue;
            if(tracks_.size()>=256&&!tracks_.count(key))continue;
            auto& track=tracks_[key];
            track.seen=true;
            if(event&&!track.samples.empty())continue;
            track.samples.push_back(std::move(s));
        }catch(const std::exception&){continue;}
    }
    for(auto it=tracks_.begin();it!=tracks_.end();){
        auto& track=it->second;
        auto& samples=track.samples;
        if(!track.seen&&!samples.back().oneShot&&samples.back().enabled){
            auto end=samples.back();end.position+=end.velocity*(now-end.time);
            end.time=now;end.enabled=false;
            samples.push_back(std::move(end));
        }
        while(samples.size()>1&&(samples.size()>2048||samples[1].time<now-maxPathSeconds-1.))samples.pop_front();
        const auto& back=samples.back();
        const double expiry=back.time+back.duration+maxPathSeconds+1.;
        if(!track.seen&&now>expiry)it=tracks_.erase(it);
        else ++it;
    }
}

AcousticReading SceneAcoustics::listen(const VoxelSceneSnapshot& scene,const vsg::dvec3& receiver,
    const vsg::dvec3& receiverVelocity,bool reflections)const {
    AcousticReading result;
    result.underwater=inWater(scene,receiver);
    const double now=scene.timeSeconds();
    const auto weather=scene.weather.sample(now);
    if(scene.ocean&&scene.ocean->contains(receiver.x,receiver.z)&&receiver.y>scene.ocean->bottom){
        const double depth=scene.ocean->surface(receiver.x,receiver.z,now).height-receiver.y;
        const double u=std::clamp((depth+.60)/1.25,0.,1.);
        result.submergedMix=u*u*(3.-2.*u);
    }
    oceanAmbience(scene,receiver,result,false,1.-result.submergedMix);
    oceanAmbience(scene,receiver,result,true,result.submergedMix);
    for(const auto& [key,track]:tracks_){
        const auto& samples=track.samples;
        const auto& last=samples.back();
        const double weight=last.underwater?result.submergedMix:1.-result.submergedMix;
        if(weight<1.e-5)continue;
        const double c=last.underwater?1480.:331.3*std::sqrt(weather.temperature/273.15);
        if(vsg::length(last.position-receiver)>c*maxPathSeconds*2.)continue;
        const auto positionAt=[&](double t){
            auto after=std::upper_bound(samples.begin(),samples.end(),t,[](double time,const Emission& s){return time<s.time;});
            if(after==samples.begin())return std::pair{&samples.front(),samples.front().position};
            const auto& before=*std::prev(after);
            if(after==samples.end())return std::pair{&before,before.position+before.velocity*std::max(0.,t-before.time)};
            const auto alpha=(t-before.time)/(after->time-before.time);
            return std::pair{&before,before.position+(after->position-before.position)*alpha};
        };
        const auto trace=[&](std::optional<double> plane) {
            auto image=[&](vsg::dvec3 p){if(plane)p.y=2.*(*plane)-p.y;return p;};
            const auto distanceAt=[&](double t){return vsg::length(image(positionAt(t).second)-receiver);};
            double emitted=last.time;
            if(last.oneShot){
                const auto lag=distanceAt(emitted)/c;
                if(now<emitted+lag||now>=emitted+lag+last.duration)return;
            }else{
                double low=std::max(samples.front().time,now-maxPathSeconds), high=now;
                if(high<low||low+distanceAt(low)/c>now)return;
                for(int i=0;i<18;++i){
                    const double middle=(low+high)*.5;
                    if(middle+distanceAt(middle)/c>now)high=middle;
                    else low=middle;
                }
                emitted=(low+high)*.5;
            }
            auto [source,p]=positionAt(emitted);
            const auto imagePoint=image(p);
            const auto fullRay=imagePoint-receiver;
            const auto length=vsg::length(fullRay);
            if(!source->enabled||source->underwater!=last.underwater||length/c>maxPathSeconds)return;
            vsg::dvec3 firstDirection=receiver-p;
            bool screened=false;
            if(plane){
                if(p.y<=*plane+.05||receiver.y<=*plane+.05)return;
                const auto bounce=receiver+fullRay*((*plane-receiver.y)/fullRay.y);
                const auto hit=environmentHit(scene,bounce+vsg::dvec3{0.,.25,0.},{0.,-1.,0.},.5);
                if(!hit||hit->normal.y<=.95||blocked(scene,p,hit->position+vsg::dvec3{0.,.05,0.})||
                    blocked(scene,hit->position+vsg::dvec3{0.,.05,0.},receiver))return;
                firstDirection=bounce-p;
            }else screened=blocked(scene,p,receiver);
            AcousticPath path;
            path.key=plane?key+"/reflection":key;
            path.cue=source->cue;path.oneShot=source->oneShot;path.estimated=source->estimated;
            path.underwater=source->underwater;
            path.screened=screened;path.reflected=bool(plane);
            path.sourcePosition=p;path.distance=length;
            path.emissionTime=emitted;path.delay=length/c;path.pitch=source->pitch;
            path.playbackOffset=std::max(0.,source->oneShot?now-emitted-path.delay:emitted-samples.front().time);
            if(length>.001){
                path.arrivalDirection=fullRay/length;
                auto sourceVelocity=source->oneShot?vsg::dvec3{}:source->velocity;
                if(plane)sourceVelocity.y=-sourceVelocity.y;
                const auto radial=vsg::dot(sourceVelocity,path.arrivalDirection);
                if(vsg::length(sourceVelocity)<.90*c&&vsg::length(receiverVelocity)<.90*c)
                    path.doppler=std::clamp((c+vsg::dot(receiverVelocity,path.arrivalDirection))/(c+radial),.5,2.);
            }
            double directivity=0.;
            if(vsg::length(source->forward)>.001&&vsg::length(firstDirection)>.001)
                directivity=.5*source->directivity*(1.-vsg::dot(vsg::normalize(source->forward),vsg::normalize(firstDirection)));
            const double effectiveLength=std::max(source->minimumDistance,length);
            const double spreading=20.*std::log10(effectiveLength/source->reference);
            const auto air=atmosphericLoss(weather.temperature,weather.humidity,scene.anchor.alt_m+(p.y+receiver.y)*.5);
            constexpr AcousticBands waterLoss{.00002,.00004,.00009,.00019,.00038,.00065,.0013,.0031};
            const auto& loss=source->underwater?waterLoss:air;
            const double interfaceLoss=source->underwater?4.*(1.-result.submergedMix):17.*result.submergedMix;
            path.highBandLoss=(loss[6]-loss[2])*(effectiveLength-source->reference)+interfaceLoss+
                (screened?obstacleLoss[6]-obstacleLoss[2]:0.)+(plane?groundLoss[6]-groundLoss[2]:0.);
            for(size_t i=0;i<path.bandEnergy.size();++i){
                const double high=std::clamp(std::log2(acousticBands[i]/250.)/4.,0.,1.);
                const double attenuation=spreading+loss[i]*(effectiveLength-source->reference)+directivity+high*interfaceLoss+
                    (screened?obstacleLoss[i]:0.)+(plane?groundLoss[i]:0.);
                path.bandEnergy[i]=weight*std::pow(10.,(source->bands[i]-attenuation)/10.);
                result.bandEnergy[i]+=path.bandEnergy[i];
            }
            result.paths.push_back(std::move(path));
        };
        trace({});
        if(reflections&&!last.underwater&&result.paths.size()<512)
            if(const auto plane=reflector(scene,last.position,receiver))trace(plane);
    }
    return result;
}

void validateAcousticTopic(const Json& config) {
    if(!config.is_object()||config.value("version",0)!=1)throw std::invalid_argument("invalid acoustic topic version");
    for(const auto name:{"sources","probes"}){
        const bool sources=std::string_view(name)=="sources";
        const auto list=config.value(name,Json::array());
        if(!list.is_array()||list.size()>(sources?32:8))throw std::invalid_argument("invalid acoustic topic list");
        std::set<std::string> names;
        for(const auto& row:list){
            const auto id=row.at("id").get<std::string>();
            if(id.empty()||id.size()>64||(!sources&&id=="camera")||!names.insert(id).second)throw std::invalid_argument("invalid acoustic object ID");
            vector(row.at("position_m"));
            if(!sources)continue;
            const auto cue=row.at("cue").get<std::string>();
            const auto mount=row.value("mount_object",std::string());
            const auto kind=row.value("owner",std::string());
            const auto owner=row.value("owner_id",uint32_t(0));
            const auto states=row.value("states",std::vector<std::string>{});
            const auto reference=row.value("acoustic_reference_m",10.);
            if(cue.empty()||cue.size()>48||mount.size()>128||(kind!="platforms"&&kind!="equipment"&&!kind.empty())||
                ((owner==0)!=kind.empty())||(!states.empty()&&kind.empty())||states.size()>16||
                (row.value("replace_default",false)&&kind.empty())||
                !bounded(reference,.1,1000.)||!bounded(row.value("min_distance_m",std::min(1.,reference)),.1,reference)||
                !bounded(row.value("rear_loss_db",0.),0.,40.)||
                !bounded(row.value("pitch",1.),.25,4.))throw std::invalid_argument("invalid acoustic source");
            if(!row.value("estimated_source",true)){
                const auto& calibration=row.at("calibration");
                if(calibration.at("source").get<std::string>().empty()||calibration.at("operating_condition").get<std::string>().empty())
                    throw std::invalid_argument("calibrated acoustic source requires measurement provenance");
            }
            if(row.contains("forward"))vector(row.at("forward"));
            spectrum(row);
        }
    }
}

void appendAcousticSources(const Json& config,const VoxelSceneSnapshot& scene,Json& presentation) {
    if(config.is_null())return;
    presentation["acoustic_probes"]=config.value("probes",Json::array());
    auto& loops=presentation["loops"];
    const auto sources=config.value("sources",Json::array());
    std::set<std::string> replaced;
    for(const auto& source:sources)
        if(source.value("replace_default",false)){
            const auto family=source.at("owner")=="platforms"?"platform:":"equipment:";
            replaced.insert(family+std::to_string(source.at("owner_id").get<uint32_t>()));
        }
    for(auto it=loops.begin();it!=loops.end();){
        const auto key=it->at("key").get<std::string>();
        if(replaced.count(key.substr(0,key.find('/'))))it=loops.erase(it);
        else ++it;
    }
    for(auto source:sources){
        if(source.contains("owner_id")){
            const auto& owners=presentation.at(source.at("owner").get<std::string>());
            const auto owner=std::find_if(owners.begin(),owners.end(),[&](const auto& row){return row.at("id")==source.at("owner_id");});
            if(owner==owners.end())continue;
            const auto states=source.value("states",std::vector<std::string>{});
            if(!states.empty()&&std::find(states.begin(),states.end(),owner->at("state").get<std::string>())==states.end())continue;
        }
        auto p=vector(source.at("position_m"));
        if(source.contains("mount_object")){
            const auto body=scene.objects.find(source.at("mount_object").get<std::string>());
            if(body==scene.objects.end())continue;
            const auto& object=body->second;
            const auto pose=object.pose(scene.tick,scene.fixedStep);
            p=pose*p;
            if(source.contains("forward")){
                const auto v=pose*vsg::dvec4(vector(source.at("forward")),0.);
                source["forward"]={v.x,v.y,v.z};
            }
            const auto v=object.velocity+vsg::cross(object.angularVelocity,p-pose*vsg::dvec3{});
            source["velocity_mps"]={v.x,v.y,v.z};
        }
        source["key"]="acoustic:"+source.at("id").get<std::string>();
        source["position_m"]={p.x,p.y,p.z};
        loops.push_back(std::move(source));
    }
}

}
