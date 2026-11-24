#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/OrbitalGlobe.h>
#include <vsgraysim/earth/PlanetWeather.h>
#include <vsgraysim/earth/TerrainDisplay.h>
#include <vsg/app/View.h>
#include <vsg/app/Camera.h>
#include <vsgraysim/earth/EnvironmentTerrain.h>
#include <rocky_env/EnvApi.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/app/CompileTraversal.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/RenderPass.h>
#include <vsg/core/Exception.h>
#include <vsg/core/Array3D.h>
#include <vsg/commands/Command.h>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) try {
    using namespace vsgraysim::earth;
    if(argc!=2 && !(argc==3 && std::string_view(argv[2])=="--gpu")) return 1;
    std::ifstream input(argv[1]);
    auto surface=EarthDataService::loadScenario(nlohmann::json::parse(input),argv[1]);
    if(!surface || !surface->orbital || !surface->revision) return 2;
    auto local=makeTerrainDisplay(*surface);
    if(local->children.empty() || !surface->columnTopGeodetic(surface->anchor.lat_deg,surface->anchor.lon_deg)) return 3;

    OrbitalGlobe globe(surface->orbital);
    globe.setWindows(surface->declaration.at("tile").get<OrbitalGlobe::TileKey>());
    rockyenv::EnvironmentStackParams params;
    params.sky.voxel_earth=true;
    if (argc==3) params.ocean.wave_mode=2;
    EnvironmentTerrain terrain;
    terrain.bind(surface);
    params.ocean.terrain=&terrain;
    if (terrain.revision()!=surface->revision || !std::isfinite(terrain.heightENU(0,0)) ||
        std::isfinite(terrain.waterBedENU(surface->max_x+1,surface->max_z+1,0))) return 5;
    auto environment=rockyenv::createEnvironmentStack(params);
    rockyenv::EnvFrame frame;
    frame.model=simcore::geo::makeEnuToWorldMatrix(surface->anchor);
    PlanetWeather global;
    PlanetWeatherFrame weather;
    weather.eye=simcore::geo::geodeticToECEF(25.,122.,10000.);
    weather.windSpeed=9; weather.cover=.5f; weather.seconds=frame.time_sec;
    weather.detail=localWeatherDetail(10000.);
    global.update(weather);
    if (!(weather.detail>0 && weather.detail<1) || localWeatherDetail(100)!=1 ||
        localWeatherDetail(1e6)!=0) return 9;
    const auto east=simcore::geo::geoUp(20.,180.), west=simcore::geo::geoUp(20.,-180.);
    if (std::abs(cloudFraction(weather,east)-cloudFraction(weather,west))>1e-5) return 10;
    auto detailVoxels=planetCloudDetailTexture().cast<vsg::ubyteArray3D>();
    const auto voxelLayout=planetCloudVoxelLayout();
    if (planetCloudTexture()->dimensions()!=3 || !detailVoxels || detailVoxels->dimensions()!=3 ||
        detailVoxels->width()!=voxelLayout.detailSide || detailVoxels->height()!=voxelLayout.detailSide ||
        detailVoxels->depth()!=voxelLayout.detailSide || voxelLayout.nearVoxelMetres>40.01f) return 11;
    int cloudyColumns=0,totalColumns=0;
    for(int lat=-80;lat<=80;lat+=10) for(int lon=-170;lon<=170;lon+=10) {
        ++totalColumns;
        const auto normal=simcore::geo::geoUp(double(lat),double(lon));
        const float attenuation=cloudFraction(weather,normal);
        if(!std::isfinite(attenuation)||attenuation<0||attenuation>1)return 16;
        auto clear=weather;clear.cover=0;
        if(cloudFraction(clear,normal)>1e-6f)return 16;
        auto thicker=weather;thicker.opticalDepth*=2.f;
        if(cloudFraction(thicker,normal)+1e-6f<attenuation)return 16;
        cloudyColumns+=attenuation>.25f;
    }
    std::cout<<"nominal_cloud_column_fraction="<<double(cloudyColumns)/totalColumns<<"\n";
    // cloudFraction is column attenuation (1-T), not an areal cloud-cover
    // probability. A fixed 25% attenuation threshold cannot be required to
    // occupy the requested cover fraction after changing water extinction.
    if(cloudyColumns==0||cloudyColumns==totalColumns)return 16;
    weather.cover=1.f; weather.cloudBase=600.f; weather.cloudHeight=2600.f;
    bool foundVolume=false;
    for(int lat=-65;lat<=65 && !foundVolume;lat+=13) for(int lon=-170;lon<=170 && !foundVolume;lon+=17) {
        const auto middle=simcore::geo::geodeticToECEF(double(lat),double(lon),1600.);
        if(cloudDensity(weather,middle)<.0001f)continue;
        const auto below=simcore::geo::geodeticToECEF(double(lat),double(lon),400.);
        const auto above=simcore::geo::geodeticToECEF(double(lat),double(lon),2800.);
        if(cloudDensity(weather,below)!=0 || cloudDensity(weather,above)!=0)return 12;
        const auto normal=simcore::geo::geoUp(double(lat),double(lon));
        const auto base=simcore::geo::geodeticToECEF(double(lat),double(lon),600.);
        const float transmission=cloudTransmittance(weather,base,normal,2000.,32);
        if(transmission>=.999f)return 13;
        if(std::abs(cloudTransmittance(weather,base,normal,500000.,32)-transmission)>1e-4f)return 17;
        const auto orbitSide=simcore::geo::geodeticToECEF(double(lat),double(lon),3000.);
        if(std::abs(cloudTransmittance(weather,orbitSide,-normal,500000.,32)-transmission)>1e-4f)return 18;
        auto thicker=weather;thicker.opticalDepth*=2.f;
        if(std::abs(cloudTransmittance(thicker,base,normal,2000.,32)-transmission*transmission)>1e-4f)return 15;
        foundVolume=true;
    }
    if(!foundVolume)return 14;
    frame.detailWeight=weather.detail;
    frame.cloudEnvelope=cloudFraction(weather,east)/weather.cover;
    auto world=vsg::Group::create();
    auto tangent=vsg::MatrixTransform::create(frame.model);
    world->addChild(globe.node);
    world->addChild(vsg::ref_ptr<vsg::Node>(environment->worldRoot()));
    world->addChild(global.node);
    world->addChild(tangent);
    tangent->addChild(local);
    tangent->addChild(vsg::ref_ptr<vsg::Node>(environment->contentRoot()));
    environment->onFrame(frame);
    for(auto name : {"sky","cloud","ocean"}) {
        auto module=environment->module(name);
        if(!module || !module->rootNode()) return 4;
    }
    if (argc==3) {
        auto instance=vsg::Instance::create(vsg::Names{},vsg::Names{},VK_API_VERSION_1_1);
        auto [physical,family]=instance->getPhysicalDeviceAndQueueFamily(VK_QUEUE_GRAPHICS_BIT);
        if (!physical || family<0) return 6;
        auto device=vsg::Device::create(physical.get(),vsg::QueueSettings{{family,{1.f}}},vsg::Names{},vsg::Names{});
        environment->setDevice(device.get());
        auto* fft=environment->module("ocean")->fftCommands();
        if (!fft) return 8;
        world->addChild(vsg::ref_ptr<vsg::Node>(fft));
        environment->onFrame(frame);
        auto requirements=vsg::CollectResourceRequirements::create();
        auto viewport=vsg::ViewportState::create(VkExtent2D{640,480});
        auto camera=vsg::Camera::create(vsg::Perspective::create(45.,640./480.,0.5,1e8),
                                        vsg::LookAt::create(),viewport);
        auto view=vsg::View::create(camera);
        view->addChild(world);
        view->accept(*requirements);
        auto compile=vsg::CompileTraversal::create(device,requirements->requirements);
        auto& context=*compile->contexts.front();
        context.renderPass=vsg::createRenderPass(device.get(),VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_D32_SFLOAT);
        context.defaultPipelineStates.push_back(viewport);
        compile->compile(view);
        std::cout<<"GPU="<<physical->getProperties().deviceName<<'\n';
    }
    std::cout<<"revision="<<surface->revision<<" terrain_indices="<<surface->indices().size()<<'\n';
} catch (const vsg::Exception& error) {
    std::cerr << error.message << " (VkResult " << error.result << ")\n";
    return 7;
}
