#include <rocky_env/EnvApi.h>
#include <rocky_env/AtmosphereRender.h>
#include <rocky_env/RuntimePaths.h>
#include <rocky_env/EnvPhysicsService.h>
#include <rocky_env/SensorApi.h>
#include <simcore/GeoFrame.h>
#if VSGRAYSIM_WITH_ROCKY
#include <rocky/vsg/VSGContext.h>
#endif
#include <vsg/all.h>
#include "../env_modules/src/AtmosphereLUT.h"
#include "../env_modules/src/EnvCommon.h"
#include "../env_modules/src/EphemerisBridge.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <chrono>

namespace {
using namespace rockyenv;
using namespace rockyenv::sensor;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool near(double a,double b,double tolerance=1e-8){return std::abs(a-b)<=tolerance*std::max({1.,std::abs(a),std::abs(b)});}
EnvFrame frame(double latitude,double longitude,double hour=4) {
    simcore::geo::GeoAnchor anchor;anchor.lat_deg=latitude;anchor.lon_deg=longitude;
    EnvFrame f;f.model=simcore::geo::makeEnuToWorldMatrix(anchor);f.view_proj=vsg::dmat4(1.);
    f.cam_pos_enu={0,100,0};f.year=2026;f.month=9;f.day=16;f.hours_utc=hour;
    f.cloud_cover=0;f.precipitation=0;f.visibility=10000;return f;
}
void siderealTimeContract() {
    EphemerisBridge sky;
    constexpr double pi=3.14159265358979323846,radians=pi/180.;
    sky.setDateTime(2000,1,1,12.);
    require(std::abs(std::remainder(sky.greenwichSiderealRadians()-280.4606*radians,2.*pi))<.001*radians,
            "Greenwich stellar angle disagrees with the J2000 epoch");
    sky.setDateTime(2026,9,25,23.75);
    const double start=sky.greenwichSiderealRadians();
    sky.setDateTime(2026,9,25,24.75);
    require(near(std::remainder(sky.greenwichSiderealRadians()-start,2.*pi),15.0410685*radians,1e-6),
            "stellar rotation was discontinuous across UTC midnight");
    const auto observer=frame(25.078,121.772).model;
    EnvSunInfo direction;
    float sunRadius=0.f,moonRadius=0.f,earlierMoonRadius=0.f,phase=0.f;
    require(sky.sunInfo(observer,direction,&sunRadius)&&sunRadius>.00455f&&sunRadius<.00475f,
            "Sun angular radius disagrees with its observer distance");
    require(sky.moonInfo(observer,direction,phase,&moonRadius)&&moonRadius>.0039f&&moonRadius<.0052f,
            "Moon angular radius is outside its orbital range");
    sky.setDateTime(2026,9,15,10.5);
    require(sky.moonInfo(observer,direction,phase,&earlierMoonRadius)&&
            std::abs(earlierMoonRadius-moonRadius)>1.e-5f,"Moon angular radius did not follow distance");
}
struct InspectSky : vsg::Visitor {
    bool lut=false,separate=false;int textureCount=0;
    vsg::ref_ptr<vsg::floatArray> skyUniform;
    void apply(vsg::Node& node)override {node.traverse(*this);}
    void apply(vsg::StateGroup& group)override {
        for(const auto& command:group.stateCommands) {
            if(auto* bind=dynamic_cast<vsg::BindDescriptorSet*>(command.get());bind&&bind->firstSet==0&&bind->descriptorSet)
                for(const auto& descriptor:bind->descriptorSet->descriptors)
                    if(auto* buffer=dynamic_cast<vsg::DescriptorBuffer*>(descriptor.get()))for(const auto& info:buffer->bufferInfoList)
                        if(auto values=info->data.cast<vsg::floatArray>();values&&values->size()>=92)skyUniform=values;
            if(auto* bind=dynamic_cast<vsg::BindDescriptorSet*>(command.get());bind&&bind->firstSet==1&&bind->descriptorSet) {
                textureCount=int(bind->descriptorSet->descriptors.size());
                if(textureCount==5) {
                    lut=true;
                    for(size_t i=0;i<5;++i) {
                        auto* image=dynamic_cast<vsg::DescriptorImage*>(bind->descriptorSet->descriptors[i].get());
                        require(image&&image->imageInfoList.size()==1,"LUT sampler missing image");
                        const auto view=image->imageInfoList[0]->imageView;
                        require(view&&view->image&&view->image->data,"LUT texture has no CPU upload data");
                        require(view->viewType==(i==1?VK_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_2D),"LUT dimensionality is incorrect");
                        if(i==0) {
                            const auto sky=view->image->data.cast<vsg::vec4Array2D>();
                            const auto earth=referenceAtmosphereTransmittance().cast<vsg::vec4Array2D>();
                            require(sky&&earth&&sky->width()==earth->width()&&sky->height()==earth->height(),
                                    "sky and Earth transmission tables differ in layout");
                            for(size_t pixel:{128u,190u,3650u,12730u})
                                require((*sky)[pixel]==(*earth)[pixel],"sky and Earth transmission tables differ in calibration");
                        }
                        if(i==3) {
                            const auto terrain=view->image->data.cast<vsg::ubvec2Array2D>();
                            require(terrain&&terrain->width()==512&&terrain->height()==256&&
                                    terrain->properties.format==VK_FORMAT_R8G8_UNORM,"Moon relief channel is missing");
                            auto extremes=std::minmax_element(terrain->begin(),terrain->end(),
                                [](const auto& a,const auto& b){return a.y<b.y;});
                            require(int(extremes.second->y)-int(extremes.first->y)>165,"Moon map relief is flat");
                        }
                        if(i==4)require(view->image->data->width()==2048&&view->image->data->height()==1024,
                                        "catalog star atlas dimensions are incorrect");
                    }
                }
            }
            if(auto* bind=dynamic_cast<vsg::BindGraphicsPipeline*>(command.get());bind&&bind->pipeline) {
                for(const auto& state:bind->pipeline->pipelineStates)
                    if(auto* input=dynamic_cast<vsg::VertexInputState*>(state.get()))
                        if(input->vertexBindingDescriptions.size()==3 && input->vertexAttributeDescriptions.size()==3 &&
                           input->vertexAttributeDescriptions[1].binding==1 && input->vertexAttributeDescriptions[2].binding==2)separate=true;
            }
        }
        group.traverse(*this);
    }
};
struct CountingOcean final : IEnvModule,IOceanSampler {
    mutable std::atomic<int> samples{0};
    mutable std::atomic<bool> exactGrid{true};
    double expectedTime=0,weather=0;
    bool enabled_=true;
    const char* name() const override{return "ocean";}
    vsg::Node* rootNode()override{return nullptr;}
    void onFrame(const EnvFrame& f)override{expectedTime=f.time_sec;weather=f.precipitation;}
    void setEnabled(bool value)override{enabled_=value;}
    bool enabled()const override{return enabled_;}
    SeaState seaState()const override{SeaState state;state.beaufort=5;return state;}
    float seaTempK()const override{return 292.5f;}
    float waveHeight(double,double,double)const override{throw std::logic_error("unexpected wave-height sample");}
    vsg::vec3 waveNormal(double,double,double)const override{throw std::logic_error("unexpected wave-normal sample");}
    vsg::vec3 orbitalVelocity(double,double,double)const override{throw std::logic_error("unexpected orbital sample");}
    float foamCoverage(double x,double z,double time)const override{
        const int index=samples.fetch_add(1)%16;
        if(x!=-300.+200.*(index/4)||z!=-300.+200.*(index%4)||time!=expectedTime)exactGrid=false;
        return float(.2+.0001*x+.0002*z+.01*time+.1*weather);
    }
    double mean()const{return .2+.01*expectedTime+.1*weather;}
};
struct CountingStack final : IEnvStack {
    CountingOcean ocean;
    vsg::Node* worldRoot()override{return nullptr;}
    vsg::Node* contentRoot()override{return nullptr;}
    void onFrame(EnvFrame& f)override{ocean.onFrame(f);}
    IEnvModule* module(const char* type)override{return std::string(type)=="ocean"?&ocean:nullptr;}
    std::vector<IEnvModule*> modules()override{return {&ocean};}
};
void lazyOceanContracts(){
    CountingStack stack;EnvPhysicsService service;service.bind(&stack);
    EnvFrame f;f.time_sec=2;f.precipitation=.3f;stack.onFrame(f);service.update(f);
    EnvRequest request;request.band=BandSpec::make(BandType::TV);request.sim_time=f.time_sec;
    const auto atmosphere=service.answerAtmosphere(request);
    service.answerAtmosphere(request);
    require(stack.ocean.samples==0,"refresh or atmosphere-only answer triggered ocean spatial sampling");
    require(atmosphere.sea_surface_temp_k==0&&atmosphere.beaufort==-1,"atmosphere-only response claims complete ocean fields");
    const auto full=service.answer(request);
    require(stack.ocean.samples==16&&stack.ocean.exactGrid&&near(full.foam_fraction,stack.ocean.mean(),1e-7),"complete response changed the exact 16-point ocean mean or timestamp");
    require(full.sea_surface_temp_k==292.5&&full.beaufort==5&&near(full.sky_radiance,atmosphere.sky_radiance)&&near(full.sigma_per_km,atmosphere.sigma_per_km),"lazy ocean response altered atmospheric or sea metadata values");
    for(int i=0;i<4;++i)service.answer(request);
    require(stack.ocean.samples==16,"repeated complete responses recomputed current ocean mean");
    f.time_sec=3;stack.onFrame(f);service.update(f);service.answerAtmosphere(request);
    require(stack.ocean.samples==16,"new-time atmosphere query eagerly sampled ocean");
    std::vector<std::thread> readers;
    for(int i=0;i<8;++i)readers.emplace_back([&]{service.answer(request);});
    for(auto& reader:readers)reader.join();
    require(stack.ocean.samples==32&&stack.ocean.exactGrid&&near(service.answer(request).foam_fraction,stack.ocean.mean(),1e-7),"concurrent complete answers duplicated sampling or reused a stale-time mean");
    f.precipitation=.9f;stack.onFrame(f);service.update(f);
    require(stack.ocean.samples==32,"same-time weather refresh eagerly sampled ocean");
    require(near(service.answer(request).foam_fraction,stack.ocean.mean(),1e-7)&&stack.ocean.samples==48,"same-time weather update retained stale ocean mean");
    service.bind(nullptr);service.update(f);const auto absent=service.answer(request);
    require(stack.ocean.samples==48&&absent.foam_fraction==0&&absent.sea_surface_temp_k==0&&absent.beaufort==-1,"unbinding retained the old ocean sampler or metadata");
    std::cout<<"PASS: zero-sample atmosphere queries, exact lazy ocean means, concurrent reuse and time/weather invalidation\n";
}
void headlessConsistency(void* context) {
    EnvironmentStackParams parameters;parameters.enable_cloud=false;parameters.enable_ocean=false;parameters.sky.rocky_context=context;parameters.sky.voxel_earth=!context;
    auto stack=createEnvironmentStack(parameters);require(bool(stack),"environment stack creation failed");
    EnvPhysicsService graphical,headless;graphical.bind(stack.get());
    EnvRequest request;request.band=BandSpec::make(BandType::TV);request.los_el_deg=30;request.alt=2000;
    for(double longitude:{121.772,-58.228,0.,179.9})for(double latitude:{25.078,-60.,80.})for(double hour:{4.,16.}) {
        auto f=frame(latitude,longitude,hour);f.cloud_cover=.6f;f.precipitation=.4f;f.visibility=5000;
        stack->onFrame(f);graphical.update(f);headless.update(f);
        for(const auto band:{BandType::TV,BandType::MWIR,BandType::LWIR}) {
            request.band=BandSpec::make(band);
            const auto a=graphical.answer(request),b=headless.answer(request);
            require(near(a.sky_radiance,b.sky_radiance)&&near(a.sigma_per_km,b.sigma_per_km)&&near(a.sun_irradiance,b.sun_irradiance),"GUI/headless atmosphere differs for same geodetic weather state");
            require(near(a.is_day,b.is_day)&&near(a.sun_el_deg,b.sun_el_deg),"GUI/headless ephemeris differs");
            double direction[3];dirFromAzEl(b.sun_az_deg,b.sun_el_deg,direction);
            const auto truth=headless.sunDirEnu();
            require(vsg::length(vsg::dvec3(direction[0],direction[1],direction[2])-vsg::dvec3(vsg::normalize(truth)))<1e-6,"solar angles violate north-clockwise coordinate convention");
        }
    }
    auto clear=frame(25.078,121.772);headless.update(clear);request.band=BandSpec::make(BandType::TV);const auto day=headless.answer(request);
    require(day.is_day>.99&&day.sun_irradiance>1,"UTC04 Taiwan daylight incorrectly treated as night");
    auto opposite=frame(25.078,-58.228);headless.update(opposite);const auto night=headless.answer(request);
    require(night.is_day<.01&&night.sun_irradiance<day.sun_irradiance*.01,"headless night depends on UTC hour instead of observer longitude");
    clear.precipitation=1;clear.cloud_cover=.8f;clear.visibility=1000;headless.update(clear);const auto wet=headless.answer(request);
    require(wet.sigma_per_km>day.sigma_per_km&&wet.sun_irradiance<day.sun_irradiance,"headless rain/cloud extinction was not applied");
}
void coordinateAndLut(void* context) {
    SkyParams parameters;parameters.rocky_context=context;parameters.voxel_earth=!context;parameters.use_bruneton_lut=true;
    auto sky=createSkyModule(parameters);require(bool(sky),"LUT sky creation failed");
    InspectSky inspection;sky->rootNode()->accept(inspection);
    require(inspection.lut&&inspection.separate,"actual sky graph did not bind LUT textures with separate vertex arrays");
    auto* skyControl=dynamic_cast<ISkyControl*>(sky.get());require(skyControl&&inspection.skyUniform,"actual sky control/uniform missing");
    auto sampleFrame=frame(25.078,121.772);
    skyControl->setShowAtmosphere(false);sky->onFrame(sampleFrame);require((int((*inspection.skyUniform)[47])&1)==0,"disabled atmosphere remained active in actual sky shader uniform");
    skyControl->setShowAtmosphere(true);sky->onFrame(sampleFrame);require((int((*inspection.skyUniform)[47])&1)==1,"enabled atmosphere did not return in actual sky shader uniform");
    const float starAngle=(*inspection.skyUniform)[88];
    require(inspection.skyUniform->size()>=96&&(*inspection.skyUniform)[92]>.0045f&&(*inspection.skyUniform)[93]>.0039f,
            "observer-centred solar/lunar radii did not reach the sky shader");
    auto clearFrame=sampleFrame;clearFrame.visibility=45000;clearFrame.overcast=0;
    sky->onFrame(clearFrame);
    require((*inspection.skyUniform)[91]==0&&(*inspection.skyUniform)[94]==0&&(*inspection.skyUniform)[95]==0,
            "clear weather changed the sky's clear-air optical path");
    auto rainFrame=sampleFrame;rainFrame.cloud_base_m=600;rainFrame.overcast=1;
    rainFrame.cloud_cover=1;rainFrame.precipitation=.85f;rainFrame.visibility=1400;
    sky->onFrame(rainFrame);
    const float groundHaze=(*inspection.skyUniform)[94];
    require((*inspection.skyUniform)[91]>.99f&&groundHaze>.3f&&(*inspection.skyUniform)[95]>.8f&&
            (*inspection.skyUniform)[88]==starAngle,
            "rain below the deck lost optical attenuation or changed the sidereal clock");
    rainFrame.cam_pos_enu.y=1300;sky->onFrame(rainFrame);
    require((*inspection.skyUniform)[91]>.25f&&(*inspection.skyUniform)[91]<.75f&&(*inspection.skyUniform)[94]<groundHaze,
            "sky optical weather jumped across the authored cloud base");
    rainFrame.cam_pos_enu.y=3300;sky->onFrame(rainFrame);
    require((*inspection.skyUniform)[91]==0&&(*inspection.skyUniform)[94]<.1f&&(*inspection.skyUniform)[95]==0,
            "a low rainy cloud deck veiled the sky above it");
    auto nextHour=sampleFrame;nextHour.hours_utc+=1.;sky->onFrame(nextHour);
    require(near(std::remainder(double((*inspection.skyUniform)[88]-starAngle),2.*3.141592653589793),
                 vsg::radians(15.0410685),1e-6),"sky shader stellar angle does not follow the common UTC clock");
    auto* query=dynamic_cast<IAtmosphereQuery*>(sky.get());require(query,"atmosphere query unavailable");
    vsg::vec3 reference;
    for(double latitude:{0.,25.,80.,-70.}) {
        auto f=frame(latitude,20,11);sky->onFrame(f);
        auto a=simcore::geo::geodeticToECEF(latitude,20,1000),b=simcore::geo::geodeticToECEF(latitude,20,2000);
        vsg::vec3 tau,radiance;require(query->transmittance(a,b,tau,radiance),"ECEF atmosphere query failed");
        require(std::isfinite(radiance.x)&&tau.x>0&&tau.x<1,"invalid atmosphere path output");
        if(latitude==0)reference=tau;else require(vsg::length(reference-tau)<1e-5,"same altitude path varies by ellipsoid latitude");
        require(query->transmittance(a,a,tau,radiance)&&tau==vsg::vec3(1,1,1)&&radiance==vsg::vec3(0,0,0),"zero path is not transparent");
    }
    AtmosphereLUT first,second;require(first.init()&&second.init(),"LUT init failed");
    const float rayleigh[3]{5.803e-6f,13.558e-6f,33.1e-6f},mie[3]{3.996e-6f,3.996e-6f,3.996e-6f},ext[3]{4.444e-6f,4.444e-6f,4.444e-6f},ozone[3]{.650e-6f,1.881e-6f,.085e-6f};
    const auto generatedAt=std::chrono::steady_clock::now();require(first.generate(rayleigh,mie,ext,ozone),"LUT generation failed");
    const auto cachedAt=std::chrono::steady_clock::now();require(second.generate(rayleigh,mie,ext,ozone),"cached LUT failed");
    const auto cachedEnd=std::chrono::steady_clock::now();
    std::cout<<"Packed atmosphere LUT "<<first.textures().scattering->extent.width<<'x'<<first.textures().scattering->extent.height<<'x'<<first.textures().scattering->extent.depth
             <<" generated_ms="<<std::chrono::duration<double,std::milli>(cachedAt-generatedAt).count()<<" cache_hit_ms="<<std::chrono::duration<double,std::milli>(cachedEnd-cachedAt).count()<<'\n';
    require(first.textures().scattering->extent.depth==AtmosphereLUTConfig::SCATTERING_PACKED_DEPTH,"packed scattering texture/data extent mismatch");
    const float mu=.3f,muSun=.3f;
    const float nuForward=1.f,nuBackward=mu*muSun-(1-mu*mu);
    const auto forward=first.sampleScatteringCPU(1000,mu,muSun,nuForward),backward=first.sampleScatteringCPU(1000,mu,muSun,nuBackward);
    std::cout<<"Sky directional radiance forward="<<forward.x<<','<<forward.y<<','<<forward.z<<" back="<<backward.x<<','<<backward.y<<','<<backward.z<<'\n';
    require(forward.x>backward.x*1.5f&&forward.y>backward.y*1.5f,"solar/antisolar sky has no direction-dependent Mie lobe");
    const float beforeSlice=-1.f+2.f/3.f-1e-4f,afterSlice=beforeSlice+2e-4f;
    const auto sliceBefore=first.sampleScatteringCPU(1000,mu,muSun,mu*muSun+(1-mu*mu)*beforeSlice);
    const auto sliceAfter=first.sampleScatteringCPU(1000,mu,muSun,mu*muSun+(1-mu*mu)*afterSlice);
    require(vsg::length(sliceBefore-sliceAfter)<1e-3f,"relative-azimuth slice boundary is discontinuous");
    for(float altitude:{0.f,1000.f,99999.f,100000.f})for(float view:{-1.f,0.f,.5f,1.f})for(float solar:{-1.f,0.f,.5f,1.f}) {
        const auto value=first.sampleScatteringCPU(altitude,view,solar,view*solar);
        require(std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z)&&value.x>=0&&value.y>=0&&value.z>=0,"packed atmosphere block boundary produces invalid radiance");
    }
    std::cout<<"PASS: phase-resolved solar/antisolar radiance, continuous azimuth slices and altitude-block boundaries\n";
    float previous=0;
    for(float altitude:{0.f,1000.f,10000.f,64000.f,80000.f,95000.f,100000.f}) {
        const auto trans=first.sampleTransmittanceCPU(altitude,.8f);
        const auto ambient=first.sampleIrradianceCPU(altitude,.8f);
        require(std::isfinite(trans.x)&&std::isfinite(trans.y)&&std::isfinite(trans.z)&&
                std::isfinite(ambient.x)&&trans.x>=previous-1e-4f&&trans.x<=1.001f,
                "0..100 km atmosphere LUT is discontinuous or nonfinite");
        previous=trans.x;
    }
    require(first.textures().scattering!=second.textures().scattering&&first.textures().scattering->data==second.textures().scattering->data,"LUT cache shares device images or duplicates CPU tables");
    AtmosphereLUT single;
    require(single.init() && single.generate(rayleigh,mie,ext,ozone,1),"single scattering reference failed");
    auto singleTable=single.textures().scattering->data.cast<vsg::vec4Array3D>();
    auto multipleTable=first.textures().scattering->data.cast<vsg::vec4Array3D>();
    double extra=0;
    for(size_t i=0;i<singleTable->size();++i) for(int c=0;c<3;++c) {
        float one=singleTable->data()[i][c],many=multipleTable->data()[i][c];
        require(std::isfinite(many) && one>=0 && many>=one-1e-7,"multiple scattering lost energy or generated nonfinite radiance");
        extra+=many-one;
    }
    require(extra>0,"higher scattering orders contributed no radiance");
    require(!single.generate(rayleigh,mie,ext,ozone,0),"invalid scattering order accepted");
    const auto old=first.sampleTransmittanceCPU(1000,.8f);const float changed[3]{8e-6f,8e-6f,8e-6f};
    require(second.generate(rayleigh,mie,changed,ozone),"alternate atmosphere failed");
    require(vsg::length(first.sampleTransmittanceCPU(1000,.8f)-old)<1e-8,"regeneration mutated another view's cached table");
    double north[3];dirFromAzEl(0,0,north);require(north[0]==0&&north[1]==0&&north[2]==-1,"north points south");
}
}
int main(){
    rockyenv::runtime::configureDefaults();
    try {
        simcore::geo::GeoAnchor anchor;anchor.lat_deg=65;anchor.lon_deg=121;
        EnvFrame local;local.model=simcore::geo::makeEnuToWorldMatrix(anchor);
        const auto center=simcore::geo::localToECEF(anchor,{0,0,0});
        const auto eye=simcore::geo::localToECEF(anchor,{100,800,1000});
        const auto view=vsg::LookAt::create(eye,center,simcore::geo::geoUp(65,121));
        local.view_proj=vsg::Perspective::create(60,1.6,1,300000)->transform()*view->transform();
        const auto prepared=localRenderFrame(local);
        const vsg::dvec4 point{10,20,30,1};const auto exact=local.view_proj*(local.model*point);
        const auto approximate=vsg::mat4(prepared.view_proj)*vsg::vec4(10,20,30,1);
        require(std::abs(exact.x/exact.w-approximate.x/approximate.w)<1e-5 &&
            std::abs(exact.y/exact.w-approximate.y/approximate.w)<1e-5,"local effects lost precision through ECEF float cancellation");
        siderealTimeContract();lazyOceanContracts();headlessConsistency(nullptr);coordinateAndLut(nullptr);
#if VSGRAYSIM_WITH_ROCKY
        auto viewer=vsg::Viewer::create();auto context=rocky::VSGContextFactory::create(viewer);
        headlessConsistency(context.get());coordinateAndLut(context.get());
#endif
        std::cout<<"PASS: WGS84 atmospheric paths, shared GUI/headless weather spectra, north-clockwise sunlight, real LUT binding and immutable table caching\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
