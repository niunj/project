#include <rocky_env/VoxelWeather.h>
#include <rocky_env/AtmosphereRender.h>
#include <rocky_env/CloudStepClock.h>
#include <rocky_env/VoxelCloudCluster.h>
#include <rocky_env/GpuVoxelCloud.h>
#include "EarthShader.h"
#include "CloudTemporal.h"
#include "WeatherReflection.h"
#include "CloudLightCache.h"
#include "CloudLightField.h"
#include "Precipitation.h"
#include <vsg/commands/Draw.h>
#include <vsg/commands/Commands.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Array3D.h>
#include <vsg/utils/GraphicsPipelineConfigurator.h>
#include <vsg/state/DescriptorImage.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <future>
#include <memory>
#include <map>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

namespace rockyenv {
namespace {
constexpr double pi=3.141592653589793, a=6378137., b=6356752.314245;
constexpr CloudVoxelLayout voxelLayout{};
constexpr float referenceLiquidWaterKgM3=.003f;
constexpr int sparsePageX=40,sparsePageY=20,sparsePageZ=40;
constexpr int sparseBrickSide=8,sparseAtlasAxis=9,sparseSlotSide=sparseBrickSide+2;
constexpr int sparseAtlasSide=sparseAtlasAxis*sparseSlotSide;
constexpr int cloudBinSide=12,cloudObjectsLimit=cloudBinSide*cloudBinSide;
constexpr int cloudBinSlots=12,cloudObjectStart=16,cloudObjectStride=3;
constexpr int cloudBinStart=cloudObjectStart+cloudObjectsLimit*cloudObjectStride;
constexpr int cloudFlowIndex=cloudBinStart+cloudObjectsLimit*(cloudBinSlots/4);
constexpr int cloudAgeStart=cloudFlowIndex+1;
constexpr int cloudParameterCount=cloudAgeStart+cloudObjectsLimit/4;
constexpr float cloudObjectHalfSpan=40000.f,cloudBinWidth=2.f*cloudObjectHalfSpan/cloudBinSide;
static_assert(cloudParameterCount*sizeof(vsg::vec4)<=16384,"cloud uniform exceeds the Vulkan minimum");

struct EllipsoidProjection {vsg::dvec3 surface,up;double altitude=0;};
EllipsoidProjection projectWgs84(const vsg::dvec3& ecef) {
    const double longitude=std::atan2(ecef.y,ecef.x);
    const double horizontal=std::hypot(ecef.x,ecef.y);
    const double e2=1.-(b*b)/(a*a);
    double latitude=std::atan2(ecef.z,horizontal*(1.-e2));
    for(int i=0;i<6;++i) {
        const double s=std::sin(latitude);
        const double n=a/std::sqrt(1.-e2*s*s);
        latitude=std::atan2(ecef.z+e2*n*s,horizontal);
    }
    const double s=std::sin(latitude),c=std::cos(latitude);
    const double sl=std::sin(longitude),cl=std::cos(longitude);
    const double n=a/std::sqrt(1.-e2*s*s);
    EllipsoidProjection result;
    result.up={c*cl,c*sl,s};
    result.surface={n*c*cl,n*c*sl,n*(1.-e2)*s};
    result.altitude=vsg::dot(ecef-result.surface,result.up);
    return result;
}

bool hasCloudAnchor(const VoxelWeatherFrame& frame) {
    return vsg::length(frame.cloudAnchor)>1.;
}

struct SparsePlanetCloud {
    struct Prepared {
        std::vector<std::shared_ptr<VoxelCloudCluster>> clusters;
        vsg::dvec3 origin{},east{1,0,0},north{0,1,0},up{0,0,1};
        vsg::vec3 profile{};
        double seconds=0;
    };
    std::vector<std::shared_ptr<VoxelCloudCluster>> clusters;
    std::future<Prepared> pending;
    vsg::ref_ptr<vsg::uintArray3D> pages=vsg::uintArray3D::create(sparsePageX,sparsePageY,sparsePageZ,uint32_t(0));
    vsg::ref_ptr<vsg::ushortArray3D> atlas=vsg::ushortArray3D::create(
        sparseAtlasSide,sparseAtlasSide,sparseAtlasSide,uint16_t(0));
    // [0..6] describe the 32 m sparse field, [7..11] the camera-neighbour
    // kilometre-scale field used while descending from orbit.  Keeping both
    // layouts in one block makes cloud rendering and all shadow consumers
    // switch the same committed weather revision.
    vsg::ref_ptr<vsg::vec4Array> parameters=vsg::vec4Array::create(cloudParameterCount,vsg::vec4{});
    vsg::dvec3 origin{},east{1,0,0},north{0,1,0},up{0,0,1};
    vsg::vec3 profile{};
    CloudBrickCoord pageOrigin{-sparsePageX/2,0,-sparsePageZ/2};
    double lastSeconds=0;
    bool enabled=false;
    SparsePlanetCloud() {
        pages->properties.format=VK_FORMAT_R32_UINT;
        atlas->properties.format=VK_FORMAT_R16_UNORM;
        pages->properties.dataVariance=atlas->properties.dataVariance=vsg::DYNAMIC_DATA;
        parameters->properties.dataVariance=vsg::DYNAMIC_DATA;
        (*parameters)[6]=vsg::vec4(referenceLiquidWaterKgM3,12e-6f,1000.f,2.f);
    }
};
SparsePlanetCloud& sparseCloud() { static SparsePlanetCloud value;return value; }
float smooth(float x) { x=std::clamp(x,0.f,1.f); return x*x*(3-2*x); }
float cloudObjectRandom(uint32_t seed) {
    seed=(seed^(seed>>16))*0x7feb352du;
    seed=(seed^(seed>>15))*0x846ca68bu;
    return float((seed^(seed>>16))>>8)/16777216.f;
}
int objectBinCoordinate(float p) {
    return int(std::floor((p+cloudObjectHalfSpan)/cloudBinWidth));
}
int objectBinOffset(int x,int z) {
    return cloudBinStart+(z*cloudBinSide+x)*(cloudBinSlots/4);
}
void updateCloudObjects(const VoxelWeatherFrame& f) {
    auto& data=*sparseCloud().parameters;
    if(!f.cloudObjects||!f.clouds||f.cover<=0.f) {
        if(data[13].w!=0.f){data[13].w=0.f;data.dirty();}
        return;
    }
    const bool authored=f.overcastAmount>=0.f;
    const bool layer=f.overcast&&!authored;
    const float grey=authored?std::clamp(f.overcastAmount,0.f,1.f):0.f;
    const auto viewer=projectWgs84(f.eye);
    const vsg::dvec3 residentOrigin(data[12].x,data[12].y,data[12].z);
    const bool keepChart=data[13].w>0.f&&vsg::length(viewer.surface-residentOrigin)<20000.;
    const auto chart=hasCloudAnchor(f) ? projectWgs84(f.cloudAnchor) :
        keepChart ? projectWgs84(residentOrigin) : viewer;
    auto east=vsg::cross(vsg::dvec3(0,0,1),chart.up);
    if(vsg::length(east)<1.e-5)east=vsg::cross(vsg::dvec3(1,0,0),chart.up);
    east=vsg::normalize(east);
    const auto north=vsg::cross(chart.up,east);
    const auto tangent=vsg::cross(f.windAxis,chart.up);
    const vsg::vec2 wind{float(vsg::dot(tangent,east))*f.windSpeed,float(vsg::dot(tangent,north))*f.windSpeed};
    const float windLength=std::hypot(wind.x,wind.y);
    const auto seed=uint32_t(std::llround(std::abs(chart.surface.x)+3.*std::abs(chart.surface.y)))|1u;
    const float depth=f.cloudHeight-f.cloudBase;
    std::fill(data.data()+cloudBinStart,data.data()+cloudFlowIndex,vsg::vec4{});
    const auto airflow=windLength>.001f ? wind/windLength : vsg::vec2(1.f,0.f);
    data[cloudFlowIndex]=vsg::vec4(airflow.x,airflow.y,windLength,0.f);
    std::array<int,cloudObjectsLimit> binCounts{};
    int count=0;
    for(int z=0;z<cloudBinSide;++z)for(int x=0;x<cloudBinSide;++x) {
        if(layer&&(x%2||z%2))continue;
        const uint32_t id=seed+uint32_t(x+z*cloudBinSide)*7919u;
        const auto random=[id](uint32_t stream){return cloudObjectRandom(id+stream*0x9e3779b9u);};
        const float membership=authored?smooth((.10f+.89f*f.cover-random(1))/.10f):1.f;
        if(!layer&&random(1)>.10f+.89f*f.cover)continue;
        const float life=80.f+150.f*random(2);
        const double cycles=f.seconds/life+random(3);
        const float age=float(cycles-std::floor(cycles));
        const float envelope0=layer ? .78f+.22f*std::sin(float(cycles*2.*pi)) :
            smooth(age/.19f)*smooth((1.f-age)/.30f);
        const float growth0=layer ? .93f+.07f*std::sin(float(cycles*2.*pi)) :
            .59f+.41f*smooth(age/.48f);
        const float envelope=envelope0*membership;
        const float growth=growth0;
        const float size=(layer ? 11100.f+1900.f*random(4) : 1460.f+2250.f*random(4))*growth*(1.f+.3f*grey);
        const vsg::vec2 drift=wind*(.60f+.54f*random(5));
        const auto horizontal=[&](int cell,float jitter,float speed) {
            const double base=(cell+(layer ? 1.f : .5f)+.67f*(jitter-.5f))*cloudBinWidth-cloudObjectHalfSpan;
            return float(layer ? std::remainder(base+speed*f.seconds,2.*cloudObjectHalfSpan) : base+speed*life*age);
        };
        const float rise=layer ? .5f+.5f*std::sin(float(cycles*2.*pi)) : age;
        vsg::vec3 centre{
            horizontal(x,random(6),drift.x),
            f.cloudBase+depth*(layer ? .44f+.12f*random(7) : .22f+.32f*random(7))+(110.f+130.f*random(8))*rise,
            horizontal(z,random(9),drift.y)};
        const float development=random(10);
        float halfHeight=std::min(depth*(layer ? .29f+.11f*development : .11f+.31f*development*development)*growth,
            std::max(1.f,(std::min(centre.y-f.cloudBase,f.cloudHeight-centre.y))/1.95f));
        if(!layer) {
            // A shared condensation level with independently developing tops.
            // The old centre-first height clamp flattened almost every cloud.
            const float base=f.cloudBase+std::min(140.f,.04f*depth)*random(7)+
                             (45.f+55.f*random(8))*rise;
            halfHeight=std::min({depth*(.115f+.29f*development)*growth,
                size*(.22f+.66f*development*development),
                std::max(1.f,(f.cloudHeight-base)/2.80f)});
            centre.y=base+.82f*halfHeight;
        }
        const vsg::vec3 radii{size,halfHeight,size*(.69f+.57f*random(11))};
        const float yaw=f.windSpeed>.01f ? std::atan2(wind.y,wind.x)+1.4f*(random(12)-.5f) :
            2.f*float(pi)*random(12);
        const int row=cloudObjectStart+count*cloudObjectStride;
        data[row]=vsg::vec4(centre,envelope*(layer ? .20f+.11f*random(13) : .036f+.094f*random(13))*(1.f+.8f*grey));
        data[row+1]=vsg::vec4(radii,std::max(radii.x,radii.z)*2.40f);
        data[row+2]=vsg::vec4(std::cos(yaw),std::sin(yaw),random(14),random(15));
        data[cloudAgeStart+count/4][count%4]=life*age;
        const float reach=data[row+1].w;
        for(int iz=std::max(0,objectBinCoordinate(centre.z-reach));iz<=std::min(cloudBinSide-1,objectBinCoordinate(centre.z+reach));++iz)
            for(int ix=std::max(0,objectBinCoordinate(centre.x-reach));ix<=std::min(cloudBinSide-1,objectBinCoordinate(centre.x+reach));++ix) {
                const int slot=binCounts[iz*cloudBinSide+ix]++;
                const int offset=objectBinOffset(ix,iz);
                if(slot<cloudBinSlots)data[offset+slot/4][slot%4]=float(count+1);
                else data[offset].x=-1.f; // crowded bins use the full bounded list
            }
        ++count;
    }
    data[12]=vsg::vec4(vsg::vec3(chart.surface),float(count));
    data[13]=vsg::vec4(vsg::vec3(east),cloudObjectHalfSpan);
    data[14]=vsg::vec4(vsg::vec3(chart.up),f.cloudBase);
    data[15]=vsg::vec4(vsg::vec3(north),f.cloudHeight);
    data.dirty();
}
float noise(vsg::vec3 p);
float sampleCloudObjects(const vsg::dvec3& ecef) {
    const auto& data=*sparseCloud().parameters;
    if(data[13].w<=0.f)return 0.f;
    const auto vector=[](const vsg::vec4& a){return vsg::dvec3(a.x,a.y,a.z);};
    const auto d=ecef-vector(data[12]);
    const vsg::vec3 p(float(vsg::dot(d,vector(data[13]))),float(vsg::dot(d,vector(data[14]))),
                      float(vsg::dot(d,vector(data[15]))));
    const int x=objectBinCoordinate(p.x),z=objectBinCoordinate(p.z);
    if(x<0||x>=cloudBinSide||z<0||z>=cloudBinSide||p.y<data[14].w||p.y>data[15].w)return 0.f;
    const int bin=objectBinOffset(x,z);
    const bool crowded=data[bin].x<0.f;
    float water=0.f;
    const auto kernel=[](const vsg::vec3& p,const vsg::vec3& centre,const vsg::vec3& r) {
        const vsg::vec3 q{(p.x-centre.x)/r.x,(p.y-centre.y)/r.y,(p.z-centre.z)/r.z};
        const float d=std::max(0.f,1.f-vsg::dot(q,q));
        return d*d;
    };
    for(int i=0;i<(crowded ? int(data[12].w) : cloudBinSlots);++i) {
        const int index=crowded ? i : int(data[bin+i/4][i%4])-1;
        if(index<0)break;
        const int row=cloudObjectStart+index*cloudObjectStride;
        const auto& centre=data[row];const auto& radii=data[row+1];const auto& shape=data[row+2];
        const auto d=p-vsg::vec3(centre.x,centre.y,centre.z);
        if(std::abs(d.x)>radii.w||std::abs(d.z)>radii.w||std::abs(d.y)>1.95f*radii.y)continue;
        vsg::vec3 q{
            (shape.x*d.x+shape.y*d.z)/radii.x+.20f*d.y/radii.y,
            d.y/radii.y,(-shape.y*d.x+shape.x*d.z)/radii.z};
        const float condensation=smooth((q.y+.82f)/.18f);
        // Conservative bounds after accounting for the strongest noise warp.
        if(q.x<-1.58f||q.x>1.90f||q.y<-.82f||q.y>1.90f||std::abs(q.z)>1.39f||
           vsg::dot(q,q)>5.50f)continue;
        // Broad clouds still need rounded small-scale detail. Compensate for
        // their aspect ratio so the noise does not stretch into flat ribbons.
        const float widthRatio=std::clamp(radii.x/radii.y,1.f,3.2f);
        const float lengthRatio=std::clamp(radii.z/radii.y,1.f,3.2f);
        const vsg::vec3 detail{q.x*widthRatio,q.y,q.z*lengthRatio};
        const float billow=noise(3.7f*detail+vsg::vec3(15.f*shape.z,18.f*shape.w,9.f))-.5f;
        const float edge=noise(8.1f*detail+vsg::vec3(3.f,24.f*shape.z,19.f*shape.w))-.5f;
        q*=1.f+.35f*billow+.10f*edge;
        q.y+=.13f*billow;
        const float sx=7.13f*shape.z+3.71f*shape.w-std::floor(7.13f*shape.z+3.71f*shape.w);
        const float sy=11.71f*shape.z+5.19f*shape.w-std::floor(11.71f*shape.z+5.19f*shape.w);
        // A taller cloud concentrates condensate into a narrower rising
        // body and better separated upper billows. Preserve thin fair-weather
        // humilis and the shallow rounded edges of layered clouds.
        // Use the same bounded aspect inputs as the normalized density cache.
        const float aspect=1.f/std::sqrt(widthRatio*lengthRatio);
        const float developed=smooth((aspect-.3125f)/.1675f);
        const float bodyWidth=.94f-.14f*developed;
        const float body=kernel(q,{0,0,0},{bodyWidth,.86f,bodyWidth});
        const float crown=kernel(q,{-.43f+.49f*shape.z-.12f*developed,.37f+.22f*shape.w+.10f*developed,-.22f+.44f*sx},
                                {.46f+.20f*sy+.05f*developed,.53f+.13f*shape.z+.08f*developed,.43f+.23f*shape.w});
        const float crest=kernel(q,{.32f+.23f*sy,.58f+.13f*shape.z+.10f*developed,-.36f+.40f*shape.w},
                                {.35f+.14f*sx+.05f*developed,.46f+.15f*shape.w,.36f+.17f*sy});
        const float flank=kernel(q,{-.56f+.99f*sx,-.03f+.26f*sy,.44f-.85f*shape.w},
                                {.49f+.16f*shape.z,.42f+.13f*sy,.48f+.15f*sx});
        const float turret=kernel(q,{-.44f+.82f*sy,.82f+.09f*shape.w+.08f*developed,.40f-.82f*shape.z},
                                 {.28f+.12f*shape.w+.05f*developed,.28f+.10f*shape.z,.30f+.11f*sx});
        const float lip=kernel(q,{.70f-.20f*shape.w,.10f+.30f*shape.z,.25f-.60f*sy},
                              {.34f,.32f+.10f*shape.w,.36f});
        const float tail=kernel(q,{.77f,-.15f,-.26f},{.69f,.31f,.34f});
        const float lobe=std::max({body,(.89f+.21f*developed)*crown,.80f*flank,
            (.82f+.20f*developed)*crest,(.70f+.25f*developed)*turret,.67f*lip});
        const float eroded=std::max(0.f,lobe-(.05f+.09f*(.5f-edge)));
        const float bulk=std::max(eroded*smooth(eroded/.09f)*(1.20f+.45f*edge),.13f*tail);
        const float fringe=std::max(smooth((lobe-.006f)/.046f),.56f*tail)*
                           (1.f-smooth((lobe-.20f)/.47f));
        float condensate=bulk;
        if(fringe>.002f) {
            const auto& flow=data[cloudFlowIndex];
            const float along=d.x*flow.x+d.z*flow.y;
            const float across=d.z*flow.x-d.x*flow.y;
            const float drift=flow.z*data[cloudAgeStart+index/4][index%4];
            const float tuft=noise({(along-.26f*drift)/48.f+17.f*shape.z,
                                    (d.y+.03f*across)/34.f,
                                    across/41.f+21.f*shape.w});
            const float thread=noise({(along-.58f*drift)/175.f+13.f*shape.w,
                                      (d.y-.026f*along+.012f*across)/24.f+7.f,
                                      (across-.045f*d.y)/30.f+23.f*shape.z});
            const float strand=smooth((thread-.56f)/.16f);
            const float loose=bulk*(.10f+1.46f*smooth((tuft-.37f)/.28f)+1.10f*strand)+
                              .035f*strand*(1.f-smooth((lobe-.09f)/.24f));
            condensate+=fringe*(loose-bulk);
        }
        water=std::max(water,centre.w*condensate*condensation);
    }
    return water*smooth((cloudObjectHalfSpan-std::max(std::abs(p.x),std::abs(p.z)))/5000.f);
}
std::array<float,3> cloudSplineWeights(float t) {
    const float a=.5f-t,b=.5f+t;
    return {.5f*a*a,.75f-t*t,.5f*b*b};
}
float cloudExtinctionPerMetre(float normalizedLiquidWater,float opticalScale) {
    constexpr float dropletRadius=12e-6f,waterDensity=1000.f,extinctionEfficiency=2.f;
    const float massExtinction=3.f*extinctionEfficiency/(4.f*waterDensity*dropletRadius);
    return std::max(normalizedLiquidWater,0.f)*referenceLiquidWaterKgM3*massExtinction*
           std::max(opticalScale,0.f)/10.f;
}
float noise(vsg::vec3 p) {
    const int x=int(std::floor(p.x)), y=int(std::floor(p.y)), z=int(std::floor(p.z));
    const auto hash=[](uint32_t i,uint32_t j,uint32_t k) {
        uint32_t v=i*1597334677u ^ j*3812015801u ^ k*2798796415u;
        v=(v^(v>>16))*2246822519u; return float(v^(v>>13))/4294967296.f;
    };
    const float u=smooth(p.x-x),v=smooth(p.y-y),w=smooth(p.z-z);
    float sum=0;
    for(int k=0;k<2;++k) for(int j=0;j<2;++j) for(int i=0;i<2;++i)
        sum+=hash(x+i,y+j,z+k)*(i?u:1-u)*(j?v:1-v)*(k?w:1-w);
    return sum;
}
float tiledNoise(vsg::vec3 p,int period) {
    const int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    const auto hash=[period](int i,int j,int k) {
        const uint32_t a=uint32_t((i%period+period)%period);
        const uint32_t b=uint32_t((j%period+period)%period);
        const uint32_t c=uint32_t((k%period+period)%period);
        uint32_t h=a*73856093u ^ b*19349663u ^ c*83492791u;
        h=(h^(h>>16))*2246822519u;
        return float((h^(h>>13))>>8)/16777216.f;
    };
    const float u=smooth(p.x-x),v=smooth(p.y-y),w=smooth(p.z-z);
    float value=0;
    for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
        value+=hash(x+dx,y+dy,z+dz)*(dx?u:1-u)*(dy?v:1-v)*(dz?w:1-w);
    return value;
}
const auto& cloudVolume() {
    static const auto image=[] {
        constexpr int width=voxelLayout.macroWidth,height=voxelLayout.macroHeight,
                      depth=voxelLayout.macroDepth;
        auto data=vsg::ubyteArray3D::create(width,height,depth,uint8_t(0));
        const auto hash=[](uint32_t value) {
            value^=value>>16;value*=0x7feb352du;value^=value>>15;
            value*=0x846ca68bu;value^=value>>16;return float(value>>8)/16777216.f;
        };
        const float texelScale=float(width)/512.f;
        // Coarse orbital LOD of discrete cloud objects. Synthetic weather is
        // used only to choose moist/CAPE cells when no external forecast is
        // connected; density is rasterized once into real 3-D condensate
        // voxels. The shader never reconstructs a cloud shell from noise.
        const auto rasterSystem=[&](uint32_t key,float centreX,float centreY,float radiusX,
                                    float radiusY,float base,float vertical,float angle,
                                    float strength) {
            const float cs=std::cos(angle),sn=std::sin(angle);
            const float reach=std::max(radiusX,radiusY)*1.35f;
            const int xmin=int(std::floor(centreX-reach-1)),xmax=int(std::ceil(centreX+reach+1));
            const int ymin=std::max(0,int(std::floor(centreY-reach-1)));
            const int ymax=std::min(height-1,int(std::ceil(centreY+reach+1)));
            const int zmin=std::max(0,int(std::floor(base))),zmax=std::min(depth-1,int(std::ceil(base+vertical)));
            for(int z=zmin;z<=zmax;++z) {
                const float hz=(float(z)+.5f-base)/vertical;if(hz<=0||hz>=1)continue;
                const float verticalShape=smooth(std::min(hz/.22f,(1.f-hz)/.30f));
                for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x) {
                    const float px=float(x)+.5f-centreX,py=float(y)+.5f-centreY;
                    const float rx=cs*px+sn*py,ry=-sn*px+cs*py;
                    const float radial=rx*rx/(radiusX*radiusX)+ry*ry/(radiusY*radiusY);
                    if(radial>=1.f)continue;
                    const float phase=float(key%17u);
                    // Finer-grained noise so each synoptic system shows
                    // internal cloudlet structure instead of a smooth plate.
                    const vsg::vec3 systemPoint(rx/radiusX,ry/radiusY,hz);
                    const float structure=.46f*noise(systemPoint*10.f+vsg::vec3(phase,phase*.7f,phase*1.3f))+
                                          .34f*noise(systemPoint*23.f+vsg::vec3(phase*1.7f,phase*.9f,phase*.4f))+
                                          .20f*noise(systemPoint*47.f+vsg::vec3(phase*.6f,phase*1.1f,phase*1.9f));
                    // A synoptic system is a population of separated stored
                    // condensate cells, not one solid ellipsoid. The resolved
                    // gaps are what keep the orbital LOD from looking like a
                    // smooth plastic disc when the camera approaches it.
                    const float fragmented=smooth((structure-.43f)/.20f);
                    const float value=strength*smooth(1.f-radial)*verticalShape*fragmented;
                    const int wrapped=(x%width+width)%width;
                    data->at(wrapped,y,z)=std::max(data->at(wrapped,y,z),
                        uint8_t(std::clamp(value,0.f,1.f)*255.f+.5f));
                }
            }
        };
        // Wide, shallow synoptic systems establish fronts and stratiform
        // decks at the orbital LOD. They are stored condensate voxels, not a
        // fragment-shader shell. Local sparse/GPU fields replace them during
        // descent.
        constexpr uint32_t systemCount=220;
        for(uint32_t key=1;key<=systemCount;++key) {
            const float u=hash(key*911u),v=hash(key*353u);
            const double latitude=pi*(.5-v),longitude=2*pi*(u-.5);
            const vsg::vec3 direction(float(std::cos(latitude)*std::cos(longitude)),
                                      float(std::cos(latitude)*std::sin(longitude)),float(std::sin(latitude)));
            const float humidity=.6f*noise(direction*2.7f+vsg::vec3(.3f,1.2f,.8f))+
                                 .4f*noise(direction*7.1f+vsg::vec3(1.8f,.2f,2.4f));
            if(humidity<.47f)continue;
            const float centreX=u*width,centreY=v*height;
            const float radiusX=(3.f+9.f*hash(key*367u))*texelScale;
            const float radiusY=(1.2f+3.f*hash(key*389u))*texelScale;
            // Synoptic systems span the full cloud-depth column (h 0.05..0.95),
            // not only the bottom eighth of the 40-texel volume. cloudDensity
            // samples h=(alt-cloudBase)/(cloudHeight-cloudBase) directly.
            rasterSystem(key,centreX,centreY,radiusX,radiusY,2.f+16.f*hash(key*397u),
                         10.f+20.f*hash(key*401u),2.f*float(pi)*hash(key*409u),
                         .58f+.34f*humidity);
        }
        constexpr uint32_t candidateCount=26000;
        for(uint32_t key=1;key<=candidateCount;++key) {
            // An irregular point process avoids exposing a latitude/longitude
            // seeding lattice at orbital distance. Correlated weather still
            // controls acceptance, so accepted objects gather into systems.
            const float u=hash(key*43u),v=hash(key*61u);
            const double latitude=pi*(.5-v),longitude=2*pi*(u-.5);
            const vsg::vec3 direction(float(std::cos(latitude)*std::cos(longitude)),
                                      float(std::cos(latitude)*std::sin(longitude)),float(std::sin(latitude)));
            // Correlated global weather fields decide where discrete cloud
            // objects are born. They are not sampled as visible cloud density.
            const float humidity=.62f*noise(direction*3.2f+vsg::vec3(.2f,1.1f,.7f))+
                                 .38f*noise(direction*9.5f+vsg::vec3(2.3f,.4f,1.8f));
            const float cape=(.38f+.62f*float(std::cos(latitude)))*
                             (.55f*noise(direction*4.7f+vsg::vec3(1.7f,.3f,2.1f))+
                              .45f*noise(direction*13.f+vsg::vec3(.1f,2.7f,.6f)));
            if(humidity<.50f || cape<.21f)continue;
            const float centreX=u*width,centreY=v*height;
            const float radiusX=(.58f+1.55f*hash(key*73u))*texelScale;
            const float radiusY=(.38f+1.05f*hash(key*89u))*texelScale;
            const float base=1.f+14.f*hash(key*101u);
            const float vertical=8.f+22.f*cape*hash(key*113u);
            const int xmin=int(std::floor(centreX-radiusX*1.5f-1)),xmax=int(std::ceil(centreX+radiusX*1.5f+1));
            const int ymin=std::max(0,int(std::floor(centreY-radiusY*1.5f-1)));
            const int ymax=std::min(height-1,int(std::ceil(centreY+radiusY*1.5f+1)));
            const int zmin=std::max(0,int(std::floor(base))),zmax=std::min(depth-1,int(std::ceil(base+vertical)));
            for(int z=zmin;z<=zmax;++z) {
                const float hz=(float(z)+.5f-base)/vertical;
                if(hz<=0 || hz>=1)continue;
                const float drift=(hz-.35f)*(hash(key*127u)-.5f)*radiusX*1.4f;
                const float pulse=.82f+.18f*std::sin(float(pi)*(hz*3.f+hash(key*131u)));
                const float bulb=(.34f+.82f*std::sin(float(pi)*std::pow(hz,.78f)))*pulse;
                for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x) {
                    const int wrapped=(x%width+width)%width;
                    const float px=float(x)+.5f-centreX-drift,py=float(y)+.5f-centreY;
                    float radial=(px*px/(radiusX*radiusX)+py*py/(radiusY*radiusY))/(bulb*bulb);
                    // A coarse object is a union of several resolved thermal
                    // lobes, rasterized into condensate voxels once. This is
                    // data construction, not shader-side procedural erosion.
                    for(uint32_t lobe=0;lobe<3;++lobe) {
                        const float angle=2.f*float(pi)*hash(key*(151u+lobe*14u));
                        const float ox=std::cos(angle)*radiusX*(.22f+.22f*hash(key*(157u+lobe*16u)));
                        const float oy=std::sin(angle)*radiusY*(.18f+.20f*hash(key*(163u+lobe*18u)));
                        const float scale=.48f+.25f*hash(key*(167u+lobe*20u));
                        radial=std::min(radial,((px-ox)*(px-ox)/(radiusX*radiusX)+
                            (py-oy)*(py-oy)/(radiusY*radiusY))/(bulb*bulb*scale*scale));
                    }
                    if(radial>=1)continue;
                    const float verticalShape=smooth(std::min(hz/.16f,(1.f-hz)/.22f));
                    const float value=smooth(1.f-radial)*verticalShape*(.55f+.45f*humidity);
                    data->at(wrapped,y,z)=std::max(data->at(wrapped,y,z),
                        uint8_t(std::clamp(value,0.f,1.f)*255.f+.5f));
                }
            }
        }
        data->properties.format=VK_FORMAT_R8_UNORM; return data;
    }();
    return image;
}
const auto& cloudDetail() {
    static const auto image=[] {
        // Camera-neighbour R8 condensate volume. It is populated by the
        // background intermediate-cloud builder and committed at an update
        // boundary. The fragment shader only samples stored liquid water; it
        // never reconstructs a density shell from procedural noise.
        constexpr int side=voxelLayout.detailSide;
        auto data=vsg::ubyteArray3D::create(side,side,side,uint8_t(0));
        data->properties.format=VK_FORMAT_R8_UNORM;
        data->properties.dataVariance=vsg::DYNAMIC_DATA;
        return data;
    }();
    return image;
}
float sampleVolume(const vsg::ubyteArray3D& data,double u,double v,double w,bool repeat) {
    const auto ix=[](int i,int count,bool wrap) {return wrap ? (i%count+count)%count : std::clamp(i,0,count-1);};
    const double x=u*data.width()-.5,y=v*data.height()-.5,z=w*data.depth()-.5;
    const int i=int(std::floor(x)),j=int(std::floor(y)),k=int(std::floor(z));
    const float fx=float(x-i),fy=float(y-j),fz=float(z-k);
    float result=0;
    for(int dz=0;dz<2;++dz) for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx)
        result+=data.at(ix(i+dx,data.width(),true),ix(j+dy,data.height(),repeat),ix(k+dz,data.depth(),repeat))*
                (dx?fx:1-fx)*(dy?fy:1-fy)*(dz?fz:1-fz)/255.f;
    return result;
}

struct IntermediatePlanetCloud {
    struct Prepared {
        std::vector<uint8_t> condensate;
        vsg::dvec3 origin{},east{1,0,0},north{0,1,0},up{0,0,1};
        float cloudBase=0,cloudHeight=1,horizontalCell=1000,cover=0;
        bool dense=false;
    };
    std::future<Prepared> pending;
    vsg::dvec3 origin{},east{1,0,0},north{0,1,0},up{0,0,1};
    float cloudBase=0,cloudHeight=1,horizontalCell=1000,cover=0;
    bool dense=false;
    bool enabled=false;
};
IntermediatePlanetCloud& intermediateCloud() { static IntermediatePlanetCloud value;return value; }

IntermediatePlanetCloud::Prepared prepareIntermediateCloud(VoxelWeatherFrame f,
                                                             vsg::dvec3 surface,
                                                             vsg::dvec3 up) {
    constexpr int side=voxelLayout.detailSide;
    IntermediatePlanetCloud::Prepared result;
    result.origin=surface;result.up=up;
    result.east=vsg::cross(vsg::dvec3(0,0,1),up);
    if(vsg::length(result.east)<1e-5)result.east=vsg::cross(vsg::dvec3(1,0,0),up);
    result.east=vsg::normalize(result.east);
    result.north=vsg::normalize(vsg::cross(up,result.east));
    result.cloudBase=f.cloudBase;result.cloudHeight=f.cloudHeight;result.cover=f.cover;
    // Use the existing atlas at a regional scale for a field of smaller
    // cloud objects. Both modes store the same normalized liquid water.
    const bool dense=f.denseRegionalClouds;
    result.dense=dense;
    result.horizontalCell=dense ? 500.f : 1000.f;
    result.condensate.assign(size_t(side)*side*side,uint8_t(0));
    const auto hash=[](uint32_t value) {
        value^=value>>16;value*=0x7feb352du;value^=value>>15;
        value*=0x846ca68bu;value^=value>>16;return float(value>>8)/16777216.f;
    };
    const auto store=[&](int x,int y,int z,float density) {
        if(x<0||y<0||z<0||x>=side||y>=side||z>=side)return;
        auto& value=result.condensate[size_t(x)+side*(size_t(y)+side*size_t(z))];
        value=std::max(value,uint8_t(std::clamp(density,0.f,1.f)*255.f+.5f));
    };
    const float threshold=.68f-.58f*f.cover;
    const float layerDepth=std::max(f.cloudHeight-f.cloudBase,1.f);
    const auto tangent=vsg::cross(f.windAxis,up);
    const vsg::vec2 wind{float(vsg::dot(tangent,result.east)),float(vsg::dot(tangent,result.north))};
    const float windLength=std::hypot(wind.x,wind.y);
    const vsg::vec2 downwind=windLength>.001f ? wind/windLength : vsg::vec2(1.f,0.f);
    const float shear=std::clamp(f.windSpeed/16.f,0.f,1.f);
    // Seed one independent stored condensate object per moist synoptic cell.
    // The global field supplies only its thermodynamic envelope; all visible
    // edges, towers and gaps below are rasterized into this 3-D volume now.
    const int weatherCellVoxels=dense ? 5 : 8;
    uint32_t objectKey=1;
    for(int gz=-side/2;gz<side/2;gz+=weatherCellVoxels)
        for(int gx=-side/2;gx<side/2;gx+=weatherCellVoxels,++objectKey) {
            const float jitter=dense ? 1.f : .8f;
            const float jitterX=(hash(objectKey*43u)-.5f)*weatherCellVoxels*jitter;
            const float jitterZ=(hash(objectKey*61u)-.5f)*weatherCellVoxels*jitter;
            const float localX=(gx+jitterX)*result.horizontalCell;
            const float localZ=(gz+jitterZ)*result.horizontalCell;
            const auto point=surface+result.east*double(localX)+result.north*double(localZ)+
                             up*double(f.cloudBase+.5f*layerDepth);
            const auto direction=vsg::normalize(point);
            const double longitude=std::atan2(direction.y,direction.x);
            const double latitude=std::asin(std::clamp(direction.z,-1.,1.));
            const float macro=sampleVolume(*cloudVolume(),longitude/(2*pi)+.5,
                                           .5-latitude/pi,.48,false);
            float moisture=smooth((macro-threshold)/.16f);
            if(dense) {
                // Mesoscale moisture supports groups of clouds and clear
                // gaps between them, irrespective of the viewing direction.
                const float band=noise({.064f*(gx+jitterX)+13.f,5.f,.094f*(gz+jitterZ)-3.f});
                moisture=std::clamp(.59f*band+.33f*f.cover+.23f*moisture-.21f,0.f,1.f);
            }
            if(moisture<.035f || hash(objectKey*79u)>.35f+.62f*moisture)continue;

            const bool stratiform=hash(objectKey*83u)<(dense ? .28f : .27f);
            const float size=hash(objectKey*89u);
            const float radiusX=dense ? (stratiform ? 2.1f+2.1f*size : 1.08f+2.65f*size) :
                (stratiform?5.f:1.8f)+(stratiform?7.f:3.8f)*size;
            const float radiusZ=radiusX*((stratiform?.45f:.70f)+.42f*hash(objectKey*97u));
            const float base=.015f+(dense ? .10f : stratiform?.22f:.08f)*hash(objectKey*101u);
            const float thickness=hash(objectKey*103u);
            const float vertical=dense ? (stratiform ? .074f+.093f*thickness :
                .12f+.27f*thickness+.064f*smooth((moisture-.35f)/.35f)) :
                (stratiform?.16f:.34f)+(stratiform?.22f:.58f)*thickness;
            const float centreX=float(side/2+gx)+jitterX;
            const float centreZ=float(side/2+gz)+jitterZ;
            const float lean=(hash(objectKey*107u)-.5f)*radiusX*.9f;
            const float denseExtent=2.25f*std::max(radiusX,radiusZ);
            const float extentX=dense ? denseExtent+std::abs(lean) : radiusX+2.f;
            const float extentZ=dense ? denseExtent : radiusZ+2.f;
            const int xmin=std::max(0,int(std::floor(centreX-extentX)));
            const int xmax=std::min(side-1,int(std::ceil(centreX+extentX)));
            const int zmin=std::max(0,int(std::floor(centreZ-extentZ)));
            const int zmax=std::min(side-1,int(std::ceil(centreZ+extentZ)));
            const float crownMargin=dense ? .039f : 0.f;
            const int ymin=std::max(0,int(std::floor((base-crownMargin)*side)));
            const int ymax=std::min(side-1,int(std::ceil((base+vertical*(dense ? 1.12f : 1.f)+crownMargin)*side)));
            std::array<vsg::vec3,4> lobes;
            for(uint32_t lobe=0;lobe<lobes.size();++lobe) {
                const float angle=2.f*float(pi)*hash(objectKey*(113u+lobe*12u));
                const float offset=(dense ? .29f : .18f)+(dense ? .39f : .28f)*hash(objectKey*(127u+lobe*14u));
                const float scale=.42f+.24f*hash(objectKey*(131u+lobe*16u));
                lobes[lobe]={std::cos(angle)*offset,std::sin(angle)*offset,scale};
            }
            for(int z=zmin;z<=zmax;++z)for(int x=xmin;x<=xmax;++x) {
                const float dx=float(x)+.5f-centreX,dz=float(z)+.5f-centreZ;
                float lift=0.f,depth=vertical,tail=0.f;
                if(dense) {
                    const float px=dx/radiusX,pz=dz/radiusZ;
                    const float patch=noise({x*.49f,float(objectKey%23u),z*.52f});
                    lift=2.f*crownMargin*(patch-.5f);
                    // Intersect the projected thermal columns with their
                    // curved crowns. Windward bowls and taller, offset
                    // billows then share a cloud base without a level roof.
                    float crown=.78f*smooth(1.f-.72f*(px*px+pz*pz));
                    for(size_t i=0;i<lobes.size();++i) {
                        const auto& lobe=lobes[i];
                        const float lx=px-lobe.x,lz=pz-lobe.y;
                        const float dome=smooth(1.f-.55f*(lx*lx+lz*lz)/(lobe.z*lobe.z));
                        crown=std::max(crown,(.73f+.27f*hash(objectKey*(137u+6u*uint32_t(i))))*dome);
                    }
                    depth*=.30f+.72f*crown+.10f*patch;
                    const float along=(dx*downwind.x+dz*downwind.y)/radiusX;
                    const float across=(-dx*downwind.y+dz*downwind.x)/radiusZ;
                    const float age=(along-.52f)/1.67f;
                    const float bend=(hash(149u*objectKey)-.5f)*.60f*age;
                    const float feather=smooth((.68f-std::abs(across-bend))/.50f);
                    tail=age>0.f&&age<1.f ? shear*feather*smooth(age/.18f)*smooth(1.f-age)*
                        (.40f+.60f*patch) : 0.f;
                }
                for(int y=ymin;y<=ymax;++y) {
                    const float h=(float(y)+.5f)/side;
                    const float q=(h-base-lift)/depth;
                    if(tail>0.f) {
                        const float ribbon=1.f-std::abs(h-(base+lift+.57f*vertical))/(.043f+.045f*vertical);
                        if(ribbon>0.f)store(x,y,z,.047f*tail*smooth(ribbon));
                    }
                    if(q<=0.f||q>=1.f)continue;
                    const float tower=stratiform ? 1.f : .62f+.58f*std::sin(float(pi)*std::pow(q,.72f));
                    const float drift=lean*(q-.25f);
                    const float verticalShape=smooth(std::min(q/.12f,(1.f-q)/(stratiform?.16f:.24f)));
                    const float windLean=dense ? shear*.66f*radiusX*q*q : 0.f;
                    const float px=(dx-drift-downwind.x*windLean)/(radiusX*tower);
                    const float pz=(dz-downwind.y*windLean)/(radiusZ*tower);
                    float radial=px*px+pz*pz;
                    // Resolved thermal lobes produce cauliflower silhouettes
                    // without a shader-side noise shell. Their union is stored
                    // as condensate and therefore also drives shadows/sensors.
                    for(const auto& lobe:lobes) {
                        const float lx=px-lobe.x,lz=pz-lobe.y;
                        radial=std::min(radial,(lx*lx+lz*lz)/(lobe.z*lobe.z));
                    }
                    if(radial>=1.f)continue;
                    const vsg::vec3 samplePoint((x-side*.5f)*.31f,
                                                y*.14f+float(objectKey%17u),
                                                (z-side*.5f)*.31f);
                    const float resolvedBillow=.58f+.42f*noise(samplePoint);
                    const float edge=smooth((1.f-radial)/(.12f+.20f*resolvedBillow));
                    const float density=(dense ? .20f : 1.f)*(.38f+.62f*moisture)*verticalShape*edge*resolvedBillow;
                    store(x,y,z,density);
                }
            }
        }
    return result;
}

void updateIntermediateCloud(const VoxelWeatherFrame& f) {
    auto& mid=intermediateCloud();
    auto& parameters=*sparseCloud().parameters;
    const auto projection=projectWgs84(f.eye);
    const auto chart=hasCloudAnchor(f) ? projectWgs84(f.cloudAnchor) : projection;
    if(mid.pending.valid() &&
       mid.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        auto prepared=mid.pending.get();
        if(vsg::length(chart.surface-prepared.origin)<(hasCloudAnchor(f) ? 1. : 90.*prepared.horizontalCell) &&
           prepared.cloudBase==f.cloudBase && prepared.cloudHeight==f.cloudHeight &&
           prepared.cover==f.cover && prepared.dense==f.denseRegionalClouds) {
            auto atlas=cloudDetail().cast<vsg::ubyteArray3D>();
            std::copy(prepared.condensate.begin(),prepared.condensate.end(),atlas->begin());
            atlas->dirty();
            mid.origin=prepared.origin;mid.east=prepared.east;mid.north=prepared.north;
            mid.up=prepared.up;mid.cloudBase=prepared.cloudBase;mid.cloudHeight=prepared.cloudHeight;
            mid.horizontalCell=prepared.horizontalCell;mid.cover=prepared.cover;
            mid.dense=prepared.dense;mid.enabled=true;
        }
    }
    // Begin preparation well above the visual hand-off. A fast orbital
    // descent therefore has several completed frames in which to publish the
    // new texture before it contributes to rendering.
    const bool changed=!mid.enabled || vsg::length(chart.surface-mid.origin)>45.*mid.horizontalCell ||
        mid.cloudBase!=f.cloudBase || mid.cloudHeight!=f.cloudHeight || mid.cover!=f.cover ||
        mid.dense!=f.denseRegionalClouds;
    if(f.clouds && f.cover>0.f && projection.altitude<1200000. && changed && !mid.pending.valid()) {
        mid.pending=std::async(std::launch::async,[f,surface=chart.surface,up=chart.up] {
            return prepareIntermediateCloud(f,surface,up);
        });
    }
    const float altitudeAuthority=mid.enabled && f.clouds ?
        1.f-smooth(float((projection.altitude-90000.)/410000.)) : 0.f;
    parameters[7]=vsg::vec4(vsg::vec3(mid.origin),altitudeAuthority);
    parameters[8]=vsg::vec4(vsg::vec3(mid.east),mid.horizontalCell);
    parameters[9]=vsg::vec4(vsg::vec3(mid.north),float(voxelLayout.detailSide));
    parameters[10]=vsg::vec4(vsg::vec3(mid.up),mid.cloudBase);
    parameters[11]=vsg::vec4(mid.cloudHeight,
                             .5f*mid.horizontalCell*voxelLayout.detailSide,
                             12000.f,mid.dense ? 1.f : 0.f);
    sparseCloud().parameters->dirty();
}
const auto& oceanMask() {
    static const auto mask=[] {
        static constexpr uint16_t spans[]={
#include "../data/OceanSpans.inc"
        };
        auto image=vsg::ubyteArray2D::create(2048,1024,uint8_t(0));
        auto it=std::begin(spans);
        for(int y=0;y<1024;++y) {
            const int count=*it++;
            for(int n=0;n<count;++n,it+=2) std::fill(image->data()+2048*y+it[0],image->data()+2048*y+it[1],255);
        }
        image->properties.format=VK_FORMAT_R8_UNORM; return image;
    }();
    return mask;
}
vsg::vec4 advection(const VoxelWeatherFrame& f) {
    double h=-.5*std::remainder(f.seconds*f.windSpeed/a,2*pi);
    return {vsg::vec3(f.windAxis*std::sin(h)),float(std::cos(h))};
}
vsg::dvec3 advectedCloudPoint(const vsg::dvec3& point,const vsg::vec4& rotation) {
    const vsg::dvec3 axis(rotation.x,rotation.y,rotation.z);
    return point+2.*vsg::cross(axis,vsg::cross(axis,point)+double(rotation.w)*point);
}
}
vsg::ref_ptr<vsg::Data> planetCloudTexture() { return cloudVolume(); }
vsg::ref_ptr<vsg::Data> planetCloudDetailTexture() { return cloudDetail(); }
vsg::ref_ptr<vsg::Data> planetSparseCloudPageTable() { return sparseCloud().pages; }
vsg::ref_ptr<vsg::Data> planetSparseCloudAtlas() { return sparseCloud().atlas; }
vsg::ref_ptr<vsg::Data> planetSparseCloudParameters() { return sparseCloud().parameters; }
CloudVoxelLayout planetCloudVoxelLayout() { return voxelLayout; }
vsg::vec4 planetAdvection(const VoxelWeatherFrame& f) {return advection(f);}
float localWeatherDetail(double altitude) { return 1-smooth(float((altitude-2500.)/15500.)); }
float coarseClusterDensity(const VoxelWeatherFrame& f,const vsg::dvec3& ecef) {
    if(!f.clouds || f.cover<=0 || f.cloudHeight<=f.cloudBase || !std::isfinite(vsg::length(ecef)))return 0;
    const double r=vsg::length(ecef), nx=ecef.x/r,ny=ecef.y/r,nz=ecef.z/r;
    const double radius=1./std::sqrt((nx*nx+ny*ny)/(a*a)+nz*nz/(b*b));
    const double h=(r-radius-f.cloudBase)/(f.cloudHeight-f.cloudBase);
    if(h<=0 || h>=1)return 0;
    const auto advected=advectedCloudPoint(ecef,advection(f));
    const double length=vsg::length(advected),lon=std::atan2(advected.y,advected.x);
    const double u=lon/(2*pi)+.5,v=.5-std::asin(std::clamp(advected.z/length,-1.,1.))/pi;
    const float condensate=sampleVolume(*cloudVolume(),u,v,h,false);
    static bool printed=false;
    if(!printed && std::getenv("VSGRAYSIM_CLOUD_DEBUG")){printed=true;
        std::fprintf(stderr,"[cloudVolume condensate] u=%.3f v=%.3f h=%.3f -> %.4f (threshold %.3f)\n",u,v,h,condensate,.34f-.22f*f.cover);}
    const float threshold=.34f-.22f*f.cover;
    // Match the shader: continuous condensate above a soft coverage
    // threshold, so orbital cloud systems keep their internal structure.
    const float coverage=smooth((condensate-threshold)/.08f);
    const float strength=std::clamp((condensate-threshold)/std::max(1e-4f,.55f-threshold*.4f),0.f,1.f);
    return .0035f*coverage*(.45f+1.15f*strength)*smooth(float(std::min(h,1.-h)/.05));
}
float intermediateClusterDensity(const VoxelWeatherFrame& f,const vsg::dvec3& ecef,float broad) {
    const auto& mid=intermediateCloud();
    const float authority=(*sparseCloud().parameters)[7].w;
    if(!mid.enabled || authority<=0.f)return broad;
    const double r=vsg::length(ecef);
    const auto axis=ecef/r;
    const double radius=1./std::sqrt((axis.x*axis.x+axis.y*axis.y)/(a*a)+axis.z*axis.z/(b*b));
    const float half=.5f*mid.horizontalCell*voxelLayout.detailSide;
    const float vertical=std::max(mid.cloudHeight-mid.cloudBase,1.f);
    const float v=float(r-radius-mid.cloudBase)/vertical;
    auto d=ecef-mid.origin;
    if(mid.dense) {
        const auto moved=advectedCloudPoint(ecef,advection(f));
        d+=(moved-ecef)*double(.68f+.76f*smooth(v));
    }
    const vsg::vec2 local(float(vsg::dot(d,mid.east)),float(vsg::dot(d,mid.north)));
    const float u=local.x/(2.f*half)+.5f,w=local.y/(2.f*half)+.5f;
    if(u<=0.f||u>=1.f||v<=0.f||v>=1.f||w<=0.f||w>=1.f)return broad;
    auto data=cloudDetail().cast<vsg::ubyteArray3D>();
    const auto clampIndex=[](int i,int count){return std::clamp(i,0,count-1);};
    const double x=u*data->width()-.5,y=v*data->height()-.5,z=w*data->depth()-.5;
    float density=0.f;
    if(mid.dense) {
        const int ix=int(std::floor(x+.5)),iy=int(std::floor(y+.5)),iz=int(std::floor(z+.5));
        const auto wx=cloudSplineWeights(float(x-ix)),wy=cloudSplineWeights(float(y-iy)),
                   wz=cloudSplineWeights(float(z-iz));
        for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            density+=data->at(clampIndex(ix+dx,data->width()),clampIndex(iy+dy,data->height()),
                              clampIndex(iz+dz,data->depth()))*wx[dx+1]*wy[dy+1]*wz[dz+1]/255.f;
    } else {
        const int ix=int(std::floor(x)),iy=int(std::floor(y)),iz=int(std::floor(z));
        const float fx=float(x-ix),fy=float(y-iy),fz=float(z-iz);
        for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
            density+=data->at(clampIndex(ix+dx,data->width()),clampIndex(iy+dy,data->height()),
                              clampIndex(iz+dz,data->depth()))*(dx?fx:1-fx)*(dy?fy:1-fy)*(dz?fz:1-fz)/255.f;
    }
    const float edgeDistance=std::min(half-std::abs(local.x),half-std::abs(local.y));
    const float domain=smooth(edgeDistance/std::max(half*.5f,12000.f));
    // Zero condensate in a resident atlas is clear air. Blend back to the
    // measured macro field only at the edge of the resident domain.
    return broad+(density-broad)*authority*domain*smooth(std::min(v,1.f-v)/.035f);
}
float cloudDensity(const VoxelWeatherFrame& f,const vsg::dvec3& ecef) {
    const double radius=vsg::length(ecef);
    if(!f.clouds||f.cover<=0||!std::isfinite(radius)||radius<1.)return 0.f;
    if(f.cloudObjects)return sampleCloudObjects(ecef);
    const auto n=ecef/radius;
    const double altitude=radius-1./std::sqrt((n.x*n.x+n.y*n.y)/(a*a)+n.z*n.z/(b*b));
    if(altitude<=f.cloudBase||altitude>=f.cloudHeight)return 0.f;
    const auto broad=[&]{return intermediateClusterDensity(f,ecef,coarseClusterDensity(f,ecef));};
    auto& sparse=sparseCloud();
    if(!sparse.enabled)return broad();
    const auto d=ecef-sparse.origin;
    const vsg::vec3 local(float(vsg::dot(d,sparse.east)),float(vsg::dot(d,sparse.up)),
                          float(vsg::dot(d,sparse.north)));
    const float brickMetres=sparseBrickSide*voxelLayout.nearVoxelMetres;
    const vsg::vec3 minimum=vsg::vec3(float(sparse.pageOrigin.x),float(sparse.pageOrigin.y),
                                      float(sparse.pageOrigin.z))*brickMetres+
                             vsg::vec3(.5f,.5f,.5f)*voxelLayout.nearVoxelMetres;

    const vsg::vec3 maximum=minimum+vsg::vec3(sparsePageX*brickMetres,
                                              sparsePageY*brickMetres,sparsePageZ*brickMetres);
    const float authority=std::clamp((*sparse.parameters)[0].w-1.f,0.f,1.f);
    const float radial=std::hypot(local.x,local.z);
    if(authority<=0.f)return broad();
    if(local.x<minimum.x || local.y<minimum.y || local.z<minimum.z ||
       local.x>=maximum.x || local.y>=maximum.y || local.z>=maximum.z)return broad();
    const float horizontal=1.f-smooth((radial-3500.f)/1500.f);
    const float edge=std::min({local.x-minimum.x,maximum.x-local.x,local.y-minimum.y,
                               maximum.y-local.y,local.z-minimum.z,maximum.z-local.z});
    const float blend=authority*horizontal*smooth(edge/(2.f*brickMetres));
    // The local field is authoritative, including clear air. Missing bricks
    // no longer reveal a procedural shell. Each cloud object contributes only
    // its own resident voxel group.
    // Positive centred quadratic weights match the optical shaders, with
    // one halo cell on either side of the nearest stored sample.
    const auto& params=*sparse.parameters;
    const int side=int(params[2].w),stored=int(params[5].z),axis=int(params[5].y);
    const auto grid=local/voxelLayout.nearVoxelMetres-vsg::vec3(.5f,.5f,.5f);
    const int cx=int(std::floor(grid.x+.5f)),cy=int(std::floor(grid.y+.5f)),cz=int(std::floor(grid.z+.5f));
    const int bx=int(std::floor(float(cx)/side)),by=int(std::floor(float(cy)/side)),bz=int(std::floor(float(cz)/side));
    const int px=bx-sparse.pageOrigin.x,py=by-sparse.pageOrigin.y,pz=bz-sparse.pageOrigin.z;
    const int lx=cx-bx*side,ly=cy-by*side,lz=cz-bz*side;
    const auto page=[&](int x,int y,int z){return x<0||y<0||z<0||x>=sparsePageX||y>=sparsePageY||z>=sparsePageZ ? 0u : sparse.pages->at(x,y,z);};
    const auto wx=cloudSplineWeights(grid.x-cx),wy=cloudSplineWeights(grid.y-cy),
               wz=cloudSplineWeights(grid.z-cz);
    auto accumulatePage=[&](uint32_t encoded,int dx,int dy,int dz,bool padded) {
        const int slot=int(encoded)-1;
        const int i=lx-dx*side,j=ly-dy*side,k=lz-dz*side;
        const int ax=(slot%axis)*stored+1+i,ay=((slot/axis)%axis)*stored+1+j,az=(slot/(axis*axis))*stored+1+k;
        float sum=0.f;
        for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            if(!padded&&(i+x<0||i+x>=side||j+y<0||j+y>=side||k+z<0||k+z>=side))continue;
            sum+=float(sparse.atlas->at(ax+x,ay+y,az+z))*wx[x+1]*wy[y+1]*wz[z+1];
        }
        return sum/65535.f;
    };
    const uint32_t encoded=page(px,py,pz);
    float clustered=0.f;
    if(encoded) {
        clustered=accumulatePage(encoded,0,0,0,true);
    } else {
        const auto reach=[side](int c){return c==0 ? -1 : c==side-1 ? 1 : 0;};
        const int rx=reach(lx),ry=reach(ly),rz=reach(lz);
        const int mask=(rx!=0 ? 1 : 0)|(ry!=0 ? 2 : 0)|(rz!=0 ? 4 : 0);
        for(int subset=mask;subset!=0;subset=(subset-1)&mask) {
            const int dx=rx*(subset&1),dy=ry*((subset>>1)&1),dz=rz*((subset>>2)&1);
            const auto neighbour=page(px+dx,py+dy,pz+dz);
            if(neighbour)clustered+=accumulatePage(neighbour,dx,dy,dz,false);
        }
    }
    const bool dense=intermediateCloud().dense;
    if(blend>=1.f&&!dense)return clustered;
    const float regional=broad();
    if(dense)clustered=std::max(clustered,regional*(1.f-.42f*smooth((clustered-.006f)/.094f)));
    return regional+(clustered-regional)*blend;
}

void packSparseCloud(float authority) {
    auto& sparse=sparseCloud();
    std::fill(sparse.pages->begin(),sparse.pages->end(),uint32_t(0));
    std::fill(sparse.atlas->begin(),sparse.atlas->end(),uint16_t(0));
    // Cloud identity remains separate on the CPU. The GPU atlas is only a
    // render acceleration structure; overlapping cloud objects combine by
    // maximum condensate at pack time.
    using BrickKey=std::tuple<int,int,int>;
    std::map<BrickKey,CloudBrickUpload> lookup;
    for(const auto& cluster:sparse.clusters) for(const auto& brick:cluster->volume().brickSnapshot()) {
        const BrickKey k{brick.coord.x,brick.coord.y,brick.coord.z};
        auto [it,inserted]=lookup.try_emplace(k,brick);
        if(!inserted) for(size_t i=0;i<brick.density.size();++i)
            it->second.density[i]=std::max(it->second.density[i],brick.density[i]);
    }
    const auto floorDiv=[](int v,int d){int q=v/d,r=v%d;return q-(r<0);};
    const auto floorMod=[](int v,int d){int r=v%d;return r<0?r+d:r;};
    const auto densityAt=[&](int x,int y,int z) {
        CloudBrickCoord c{floorDiv(x,sparseBrickSide),floorDiv(y,sparseBrickSide),floorDiv(z,sparseBrickSide)};
        auto it=lookup.find(BrickKey{c.x,c.y,c.z});if(it==lookup.end())return uint16_t(0);
        const auto& values=it->second.density;
        const size_t i=size_t(floorMod(x,sparseBrickSide))+sparseBrickSide*(size_t(floorMod(y,sparseBrickSide))+
                         sparseBrickSide*size_t(floorMod(z,sparseBrickSide)));
        return values[i];
    };
    uint32_t slot=0;
    for(const auto& [_,brick]:lookup) {
        const int px=brick.coord.x-sparse.pageOrigin.x,py=brick.coord.y-sparse.pageOrigin.y,
                  pz=brick.coord.z-sparse.pageOrigin.z;
        if(px<0||py<0||pz<0||px>=sparsePageX||py>=sparsePageY||pz>=sparsePageZ||
           slot>=sparseAtlasAxis*sparseAtlasAxis*sparseAtlasAxis)continue;
        sparse.pages->at(px,py,pz)=slot+1;
        const int ax=int(slot%sparseAtlasAxis),ay=int((slot/sparseAtlasAxis)%sparseAtlasAxis),
                  az=int(slot/(sparseAtlasAxis*sparseAtlasAxis));
        for(int z=-1;z<=sparseBrickSide;++z)for(int y=-1;y<=sparseBrickSide;++y)
            for(int x=-1;x<=sparseBrickSide;++x) {
                const int gx=brick.coord.x*sparseBrickSide+x,gy=brick.coord.y*sparseBrickSide+y,
                          gz=brick.coord.z*sparseBrickSide+z;
                sparse.atlas->at(ax*sparseSlotSide+x+1,ay*sparseSlotSide+y+1,az*sparseSlotSide+z+1)=
                    densityAt(gx,gy,gz);
            }
        ++slot;
    }
    auto& p=*sparse.parameters;
    p[0]=vsg::vec4(vsg::vec3(sparse.origin),1.f+authority);
    p[1]=vsg::vec4(vsg::vec3(sparse.east),voxelLayout.nearVoxelMetres);
    p[2]=vsg::vec4(vsg::vec3(sparse.north),float(sparseBrickSide));
    p[3]=vsg::vec4(vsg::vec3(sparse.up),float(sparse.pageOrigin.x));
    p[4]=vsg::vec4(float(sparse.pageOrigin.y),float(sparse.pageOrigin.z),float(sparsePageX),float(sparsePageY));
    p[5]=vsg::vec4(float(sparsePageZ),float(sparseAtlasAxis),float(sparseSlotSide),float(sparseAtlasSide));
    sparse.pages->dirty();sparse.atlas->dirty();sparse.parameters->dirty();
}

SparsePlanetCloud::Prepared prepareSparseCloud(VoxelWeatherFrame f,vsg::dvec3 surface,vsg::dvec3 up) {
    SparsePlanetCloud::Prepared result;
    result.origin=surface;result.up=up;result.seconds=f.seconds;
    result.profile={f.cloudBase,f.cloudHeight,f.cover};
    result.east=vsg::cross(vsg::dvec3(0,0,1),up);
    if(vsg::length(result.east)<1e-5)result.east=vsg::cross(vsg::dvec3(1,0,0),up);
    result.east=vsg::normalize(result.east);result.north=vsg::normalize(vsg::cross(up,result.east));
    const auto tangent=vsg::cross(f.windAxis,up);
    const vsg::vec3 wind(float(vsg::dot(tangent,result.east))*f.windSpeed,0.f,
                         float(vsg::dot(tangent,result.north))*f.windSpeed);
    // The weather field triggers independent moist thermals. It does not
    // provide a rendered density shell. Every entry below becomes one cloud
    // object with its own sparse volume and lifecycle.
    static constexpr vsg::vec3 locations[]={{0,0,0},{-1750,0,-650},{650,0,900},
                                             {2100,0,-1250},{-450,0,2300},{2500,0,1750},
                                             {-3000,0,1900},{3100,0,300}};
    const int count=std::clamp(int(std::ceil(f.cover*8.f)),1,8);
    const float fullDepth=std::max(f.cloudHeight-f.cloudBase,600.f);
    const uint64_t anchorKey=uint64_t(std::llround(std::abs(surface.x)*.01+
                                                  std::abs(surface.y)*.013+
                                                  std::abs(surface.z)*.017))+1u;
    const uint32_t secondaryBudget=count>1 ? std::max(48u,300u/uint32_t(count-1)) : 0u;
    std::vector<std::future<std::shared_ptr<VoxelCloudCluster>>> cloudTasks;
    cloudTasks.reserve(count);
    for(int i=0;i<count;++i) {
        VoxelCloudClusterState state;
        state.id=anchorKey*16u+uint64_t(i+1);
        state.centre=locations[i];
        state.baseMetres=f.cloudBase+fullDepth*(.015f*float((i*5)%4));
        state.depthMetres=fullDepth*(i==0 ? .38f : .16f+.055f*float((i*7)%6));
        state.horizontalRadius=(360.f+90.f*float((i*3)%6))*(.70f+.52f*f.cover);
        state.horizontalRadius=std::max(state.horizontalRadius,.49f*state.depthMetres);
        state.vapour=std::clamp(.012f+.008f*f.cover+.0005f*float(i%3),.008f,.024f);
        state.temperatureExcess=2.4f+3.6f*f.cover+.35f*float(i%3);
        state.updraft=2.6f+5.2f*f.cover+.45f*float(i%2);
        state.wind=wind;
        const uint32_t budget=i==0 ? (count>1?390u:640u) : secondaryBudget;
        SparseCloudConfig config;
        config.voxelSizeMetres=voxelLayout.nearVoxelMetres;
        config.brickSide=sparseBrickSide;config.maxBricks=budget;
        config.minimumActiveWaterKgM3=5e-6f;
        config.referenceLiquidWaterKgM3=referenceLiquidWaterKgM3;
        cloudTasks.push_back(std::async(std::launch::async,[state,config] {
            return std::make_shared<VoxelCloudCluster>(state,config);
        }));
    }
    // Objects are prepared independently, then joined in deterministic order.
    // The caller still swaps and packs the entire revision atomically.
    for(auto& task:cloudTasks)result.clusters.push_back(task.get());
    return result;
}

void updateSparseCloud(const VoxelWeatherFrame& f) {
    auto& sparse=sparseCloud();
    const auto projection=projectWgs84(f.eye);
    const bool fixed=hasCloudAnchor(f);
    const auto chart=fixed ? projectWgs84(f.cloudAnchor) : projection;
    const double rebaseDistance=fixed ? 1. : 2500.;
    const bool active=f.clouds && f.cover>0.f && (fixed || projection.altitude<=220000.);
    const float authority=fixed ? 1.f : 1.f-smooth(float((projection.altitude-30000.)/60000.));
    const vsg::vec3 profile(f.cloudBase,f.cloudHeight,f.cover);
    if(sparse.pending.valid() &&
       sparse.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        auto prepared=sparse.pending.get();
        if(active && prepared.profile==profile &&
           vsg::length(chart.surface-prepared.origin)<=(fixed ? rebaseDistance : 6000.)) {
            sparse.clusters=std::move(prepared.clusters);sparse.origin=prepared.origin;
            sparse.east=prepared.east;sparse.north=prepared.north;sparse.up=prepared.up;
            sparse.enabled=true;sparse.profile=prepared.profile;
            sparse.lastSeconds=prepared.seconds;packSparseCloud(authority);
        }
    }
    if(!active) {
        sparse.lastSeconds=f.seconds;
        (*sparse.parameters)[0].w=0.f;sparse.parameters->dirty();return;
    }
    if(sparse.enabled) {
        (*sparse.parameters)[0].w=1.f+authority;sparse.parameters->dirty();
    }
    // Publish complete charts at region changes. The resident cloud remains
    // visible when seen from elsewhere in the same region, at any altitude.
    const bool rebase=!sparse.enabled || sparse.profile!=profile ||
                      vsg::length(chart.surface-sparse.origin)>rebaseDistance;
    if(rebase) {
        if(!sparse.pending.valid())
        {
            sparse.pending=std::async(std::launch::async,[f,chart] {
                return prepareSparseCloud(f,chart.surface,chart.up);
            });
        }
        return;
    }
    const double elapsed=f.seconds-sparse.lastSeconds;
    if(elapsed<=1e-6)return;
    if(elapsed>2.) {sparse.lastSeconds=f.seconds;return;}
    // Evolve and upload at 10 Hz. Rendering interpolates the density texture;
    // rebuilding a sparse page table for every display frame is unnecessary.
    if(elapsed<.1)return;
    CloudDynamics dynamics;
    const auto tangent=vsg::cross(f.windAxis,sparse.up);
    // Match the GPU field: gusty wind drives cluster shredding and diffusion.
    const double gustPhase=f.seconds*2.*3.141592653589793;
    const float gust=1.f+.22f*std::sin(float(gustPhase)/7.3f)+
                     .13f*std::sin(float(gustPhase)/2.9f+.7f)+
                     .07f*std::sin(float(gustPhase)/1.7f+2.1f);
    const float gustSpeed=f.windSpeed*std::max(.55f,gust*(.75f+.35f*std::min(f.windSpeed/15.f,1.f)));
    dynamics.ambientWind={float(vsg::dot(tangent,sparse.east))*gustSpeed,0.f,
                          float(vsg::dot(tangent,sparse.north))*gustSpeed};
    dynamics.ambientVapourMixingRatio=.0075f+.0045f*f.cover;
    double remaining=elapsed;
    while(remaining>1e-6) {
        const float dt=float(std::min(remaining,.1));
        for(auto& cluster:sparse.clusters)cluster->step(dt,dynamics);
        remaining-=dt;
    }
    // Publish the density and its authority together, including on a
    // dynamics tick, so this frame cannot reveal the regional fallback.
    sparse.lastSeconds=f.seconds;packSparseCloud(authority);
}
float cloudTransmittance(const VoxelWeatherFrame& f,const vsg::dvec3& origin,
                         const vsg::dvec3& direction,double distance,unsigned samples) {
    if(distance<=0 || samples==0 || !f.clouds || f.cover<=0 || f.opticalDepth<=0 ||
       f.cloudHeight<=f.cloudBase || !std::isfinite(distance))return 1;
    const double directionLength=vsg::length(direction);
    if(directionLength<1e-12 || !std::isfinite(directionLength))return 1;
    const vsg::dvec3 ray=direction/directionLength;
    // Intersect the ray with the two WGS84 cloud ellipsoids first. Sampling
    // the entire sensor range at a fixed count can miss a thin cloud layer.
    const auto interval=[&](double height) {
        const double equator=a+height,pole=b+height;
        const vsg::dvec3 e(origin.x/equator,origin.y/equator,origin.z/pole);
        const vsg::dvec3 d(ray.x/equator,ray.y/equator,ray.z/pole);
        const double aa=vsg::dot(d,d),bb=vsg::dot(e,d),cc=vsg::dot(e,e)-1.;
        const double determinant=bb*bb-aa*cc;
        if(determinant<=0)return std::pair<double,double>{distance,distance};
        const double root=std::sqrt(determinant);
        return std::pair<double,double>{(-bb-root)/aa,(-bb+root)/aa};
    };
    const auto outer=interval(f.cloudHeight),inner=interval(f.cloudBase);
    double begin=std::max(0.,outer.first),end=std::min(distance,outer.second);
    if(end<=begin)return 1;
    const auto ground=interval(0.);
    if(ground.first>0.)end=std::min(end,ground.first);
    if(end<=begin)return 1;
    double tau=0;
    const auto integrate=[&](double from,double to) {
        if(to<=from)return;
        const unsigned count=std::clamp(std::max(samples,unsigned(std::ceil((to-from)/2000.))),1u,256u);
        const double step=(to-from)/count;
        for(unsigned i=0;i<count;++i) {
            tau+=cloudExtinctionPerMetre(
                cloudDensity(f,origin+ray*(from+(i+.5)*step)),f.opticalDepth)*step;
            if(tau>16)break;
        }
    };
    if(inner.second>begin && inner.first<end) {
        integrate(begin,std::min(end,inner.first));
        if(tau<=16)integrate(std::max(begin,inner.second),end);
    } else integrate(begin,end);
    return float(std::exp(-tau));
}
float cloudFraction(const VoxelWeatherFrame& f,const vsg::dvec3& normal) {
    const double length=vsg::length(normal);
    if(length<1e-9 || f.cloudHeight<=f.cloudBase)return 0;
    const auto n=normal/length;
    const double radius=1./std::sqrt((n.x*n.x+n.y*n.y)/(a*a)+n.z*n.z/(b*b));
    const auto start=n*(radius+f.cloudBase);
    return 1.f-cloudTransmittance(f,start,n,f.cloudHeight-f.cloudBase,12);
}

struct VoxelWeather::GpuState {
    std::shared_ptr<GpuVoxelCloud> field;
    vsg::ref_ptr<vsg::vec4Array> layout=vsg::vec4Array::create(5,vsg::vec4{});
    vsg::ref_ptr<vsg::vec4Array3D> fallback=vsg::vec4Array3D::create(1,1,1,vsg::vec4{});
    vsg::dvec3 anchor{},east{1,0,0},north{0,1,0},up{0,0,1};
    CloudStepClock clock;
    float cloudBase=std::numeric_limits<float>::quiet_NaN();
    double lastSeconds=0.;
    uint32_t frameCounter=0;   ///< drives the interleaved ray-jitter cycle
    bool anchorValid=false,stepRequired=false;
    GpuState() {
        layout->properties.dataVariance=vsg::DYNAMIC_DATA;
        fallback->properties.format=VK_FORMAT_R32G32B32A32_SFLOAT;
    }
};

std::shared_ptr<CloudGpuResources> createCloudGpuResources(vsg::Device* device) {
    auto resources=std::make_shared<CloudGpuResources>();
    resources->layout=vsg::vec4Array::create(5,vsg::vec4{});
    resources->layout->properties.dataVariance=vsg::DYNAMIC_DATA;
    resources->weather=vsg::vec4Array::create(2,vsg::vec4{});
    resources->weather->properties.dataVariance=vsg::DYNAMIC_DATA;
    resources->reflectionState=vsg::vec4Array::create(2,vsg::vec4{});resources->reflectionState->properties.dataVariance=vsg::DYNAMIC_DATA;
    auto black=vsg::vec4Array2D::create(1,1,vsg::vec4{});black->properties.format=VK_FORMAT_R32G32B32A32_SFLOAT;
    resources->reflection=vsg::ImageInfo::create(vsg::Sampler::create(),black);
    resources->ownerDevice=device;
    if(device) {
        GpuVoxelCloudConfig config;
        // 24 m cells resolve the cauliflower edge and aircraft wake without
        // the eightfold cost of jumping directly from 32 m to 16 m.  The
        // slightly tighter 2.3 x 3.45 x 2.3 km laboratory keeps all nine
        // RGBA32F solver volumes within a practical RTX memory budget.
        config.width=96;config.height=144;config.depth=96;
        config.voxelSizeMetres=24.f;
        config.originMetres={-1152.f,420.f,-1152.f};
        config.pressureIterations=16;
        resources->field=std::make_shared<GpuVoxelCloud>();
        const auto result=resources->field->build(*device,config);
        if(!result.ok)throw std::runtime_error(result.error);
        resources->image=vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>(resources->field->sampler()),
            vsg::ref_ptr<vsg::ImageView>(resources->field->thermodynamicMap()),VK_IMAGE_LAYOUT_GENERAL);
    } else {
        auto fallback=vsg::vec4Array3D::create(1,1,1,vsg::vec4{});
        fallback->properties.format=VK_FORMAT_R32G32B32A32_SFLOAT;
        auto sampler=vsg::Sampler::create();sampler->maxLod=0;
        resources->image=vsg::ImageInfo::create(sampler,fallback);
    }
    return resources;
}
std::shared_ptr<CloudSceneBinding> createCloudSceneBinding(
    std::shared_ptr<CloudGpuResources> resources,const vsg::dmat4& localToECEF) {
    if(!resources)throw std::invalid_argument("missing cloud resources");
    auto result=std::make_shared<CloudSceneBinding>();result->resources=std::move(resources);
    auto transform=vsg::mat4Value::create(vsg::mat4(localToECEF));
    auto sampler=vsg::Sampler::create();sampler->maxLod=VK_LOD_CLAMP_NONE;
    sampler->addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV=sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    auto local=vsg::Sampler::create();local->maxLod=VK_LOD_CLAMP_NONE;
    local->addressModeU=local->addressModeV=local->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    auto pages=vsg::Sampler::create();pages->maxLod=0;pages->minFilter=pages->magFilter=VK_FILTER_NEAREST;
    auto& cloud=*result->resources;
    vsg::Descriptors descriptors{vsg::DescriptorBuffer::create(transform,0),
        vsg::DescriptorBuffer::create(cloud.weather,1),
        vsg::DescriptorImage::create(sampler,cloudVolume(),2),
        vsg::DescriptorImage::create(local,cloudDetail(),3),
        vsg::DescriptorImage::create(pages,sparseCloud().pages,4),
        vsg::DescriptorImage::create(local,sparseCloud().atlas,5),
        vsg::DescriptorBuffer::create(sparseCloud().parameters,6),
        vsg::DescriptorImage::create(cloud.image,7),vsg::DescriptorBuffer::create(cloud.layout,8)};
    vsg::DescriptorSetLayoutBindings bindings;
    for(uint32_t b=0;b<9;++b)
        bindings.push_back({b,(b==0||b==1||b==6||b==8)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
    result->layout=vsg::DescriptorSetLayout::create(bindings);
    result->descriptors=vsg::DescriptorSet::create(result->layout,descriptors);return result;
}
VoxelWeather::VoxelWeather(vsg::Device* device,std::shared_ptr<CloudGpuResources> cloud)
    : gpu_(std::make_unique<GpuState>()),cloudResources_(cloud?std::move(cloud):createCloudGpuResources(device)) {
    if(device && cloudResources_->ownerDevice && cloudResources_->ownerDevice!=device)
        throw std::invalid_argument("voxel cloud belongs to a different Vulkan device");
    gpu_->field=cloudResources_->field;gpu_->layout=cloudResources_->layout;
    parameters_=vsg::vec4Array::create(15,vsg::vec4{});
    parameters_->properties.dataVariance=vsg::DYNAMIC_DATA;
    node=vsg::Group::create();
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod=VK_LOD_CLAMP_NONE;
    auto detailSampler=vsg::Sampler::create();
    detailSampler->addressModeU=detailSampler->addressModeV=detailSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    detailSampler->maxLod=VK_LOD_CLAMP_NONE;
    auto airSampler=vsg::Sampler::create();
    airSampler->addressModeU=airSampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    airSampler->maxLod=0.f;
    auto transmission=referenceAtmosphereTransmittance();
    reflection_=std::make_unique<WeatherReflection>(device,cloudResources_);
    lightCache_=std::make_unique<CloudLightCache>(device,sparseCloud().parameters);
    lightField_=std::make_unique<CloudLightField>(device,sparseCloud().parameters,parameters_,*lightCache_);
    environmentCommands_=vsg::Switch::create();environmentCommands_->addChild(true,lightCache_->commands);environmentCommands_->addChild(true,lightField_->commands);environmentCommands_->addChild(true,reflection_->commands);
    temporal_=std::make_unique<CloudTemporal>(device);environmentCommands_->addChild(true,temporal_->commands);vsg::ref_ptr<vsg::Node> directCloud;auto reflectionDraw=vsg::Group::create();
    for(auto& draw:lightingDraws_)draw=vsg::Switch::create();
    for (auto name : {"planet_sea.frag","planet_cloud.frag","planet_cloud_cached.frag","planet_cloud_raw.frag","planet_cloud_raw_cached.frag","planet_sky_reflection.frag","planet_cloud_reflection.frag","planet_cloud_reflection_cached.frag"}) {
        const bool probe=std::string_view(name).find("_reflection")!=std::string_view::npos;
        const bool cached=std::string_view(name).find("_cached")!=std::string_view::npos;
        const bool raw=std::string_view(name).find("_raw")!=std::string_view::npos;
        const bool cloudPass=std::string_view(name)!="planet_sea.frag";
        auto set=vsg::ShaderSet::create(vsg::ShaderStages{
            vsgraysim::earth::earthShader(probe?"planet_cloud_reflection.vert":raw?"planet_cloud_raw.vert":"planet_weather.vert"),vsgraysim::earth::earthShader(name)});
        if(!raw&&!probe)set->addPushConstantRange("camera","",VK_SHADER_STAGE_VERTEX_BIT,0,128);
        set->addDescriptorBinding("parameters","",0,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_VERTEX_BIT,{});
        set->addDescriptorBinding("map","",0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        set->addDescriptorBinding("cloudMap","",0,2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        set->addDescriptorBinding("cloudDetail","",0,3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        set->addDescriptorBinding("sparseCloudPages","",0,4,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        set->addDescriptorBinding("sparseCloudAtlas","",0,5,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        set->addDescriptorBinding("sparseCloud","",0,6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        set->addDescriptorBinding("gpuCloudState","",0,7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        set->addDescriptorBinding("gpuCloud","",0,8,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        if(cloudPass)set->addDescriptorBinding("airTransmission","",0,9,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        if(cloudPass){
            set->addDescriptorBinding("cloudLightAtlas","",0,11,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
            set->addDescriptorBinding("cloudLightCache","",0,12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
            set->addDescriptorBinding("cloudLightKeys","",0,13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
            set->addDescriptorBinding("cloudLighting","",0,14,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
            set->addDescriptorBinding("cloudLightField","",0,15,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
        }
        if(raw)set->addDescriptorBinding("temporal","",0,10,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,{});
        auto cfg=vsg::GraphicsPipelineConfigurator::create(set);
        if(raw)cfg->assignDescriptor("temporal",temporal_->parameters);
        cfg->assignDescriptor("parameters",parameters_);
        cfg->assignTexture("map",oceanMask(),sampler);
        cfg->assignTexture("cloudMap",cloudVolume(),sampler);
        cfg->assignTexture("cloudDetail",cloudDetail(),detailSampler);
        auto sparseSampler=vsg::Sampler::create();
        sparseSampler->addressModeU=sparseSampler->addressModeV=sparseSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sparseSampler->maxLod=0;
        auto pageSampler=vsg::Sampler::create();
        pageSampler->addressModeU=pageSampler->addressModeV=pageSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pageSampler->magFilter=pageSampler->minFilter=VK_FILTER_NEAREST;pageSampler->maxLod=0;
        cfg->assignTexture("sparseCloudPages",sparseCloud().pages,pageSampler);
        cfg->assignTexture("sparseCloudAtlas",sparseCloud().atlas,sparseSampler);
        cfg->assignDescriptor("sparseCloud",sparseCloud().parameters);
        auto gpuSampler=vsg::Sampler::create();
        gpuSampler->addressModeU=gpuSampler->addressModeV=gpuSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        gpuSampler->borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;gpuSampler->maxLod=0;
        cfg->assignTexture("gpuCloudState",vsg::ImageInfoList{cloudResources_->image});
        cfg->assignDescriptor("gpuCloud",gpu_->layout);
        if(cloudPass)cfg->assignTexture("airTransmission",transmission,airSampler);
        if(cloudPass){cfg->assignTexture("cloudLightAtlas",vsg::ImageInfoList{lightCache_->image});cfg->assignDescriptor("cloudLightCache",lightCache_->state);cfg->assignDescriptor("cloudLightKeys",vsg::BufferInfoList{lightCache_->bakedKeyBuffer});cfg->assignTexture("cloudLighting",vsg::ImageInfoList{lightField_->image});cfg->assignDescriptor("cloudLightField",lightField_->state);}
        auto depth=vsg::DepthStencilState::create(); depth->depthWriteEnable=VK_FALSE;
        depth->depthCompareOp=VK_COMPARE_OP_GREATER_OR_EQUAL;
        auto blend=weatherLayerBlend();
        if(probe){depth->depthTestEnable=VK_FALSE;blend=weatherReflectionBlend();}
        if(raw){depth->depthTestEnable=VK_FALSE;blend->attachments.resize(2);for(auto& a:blend->attachments){a.blendEnable=VK_FALSE;a.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;}}
        auto raster=vsg::RasterizationState::create(); raster->cullMode=VK_CULL_MODE_NONE;
        for(auto& state:cfg->pipelineStates) {
            if(state.cast<vsg::DepthStencilState>()) state=depth;
            else if(state.cast<vsg::ColorBlendState>()) state=blend;
            else if(state.cast<vsg::RasterizationState>()) state=raster;
        }
        set->variants.emplace(cfg->shaderHints,set->stages);
        cfg->init();
        auto group=vsg::StateGroup::create(); cfg->copyTo(group);
        group->addChild(vsg::Draw::create(3,1,0,0));
        if(probe){
            if(std::string_view(name).find("planet_cloud_reflection")!=std::string_view::npos){
                lightingDraws_[2]->addChild(!cached,group);
                if(!cached){reflectionDraw->addChild(lightingDraws_[2]);reflection_->setDraw(reflectionDraw);}
            }else reflectionDraw->addChild(group);
        }else if(raw){
            lightingDraws_[1]->addChild(!cached,group);
            if(!cached){temporal_->setDraws(directCloud,lightingDraws_[1]);node->children[1]=temporal_->display;}
        }else if(cloudPass){
            lightingDraws_[0]->addChild(!cached,group);
            if(!cached){node->addChild(lightingDraws_[0]);directCloud=lightingDraws_[0];}
        }else node->addChild(group);
    }
    precipitation_=std::make_unique<Precipitation>();node->addChild(precipitation_->node);
}
VoxelWeather::~VoxelWeather()=default;
bool VoxelWeather::updateTemporal(vsg::Camera* camera,uint32_t width,uint32_t height,bool active){environmentCommands_->setAllChildren(true);auto frame=lastFrame_;if(!active){frame.cloudObjects=false;frame.clouds=false;}lightCache_->update(frame);lightField_->update(frame);const bool cached=(*lightField_->state)[1].w>.5f;for(auto& draw:lightingDraws_)draw->setSingleChildOn(cached?1:0);reflection_->update(frame);return temporal_->update(camera,width,height,frame);}
vsg::Node* VoxelWeather::temporalCommands()const{return environmentCommands_;}
vsg::vec4 VoxelWeather::temporalState()const{if(temporal_->display->children.size()<2||!temporal_->display->children[1].mask)return {};const auto& p=*temporal_->parameters;return {p[8].z,p[8].w,p[12].x,p[8].x};}
void VoxelWeather::update(const VoxelWeatherFrame& f) {
    lastFrame_=f;
    precipitation_->update(f);
    if(!std::isfinite(f.seconds) || !std::isfinite(f.period) || f.period<=0 ||
       !std::isfinite(f.windSpeed) || f.windSpeed<0 || !std::isfinite(f.cover) ||
       f.cover<0 || f.cover>1 || !std::isfinite(f.opticalDepth) || f.opticalDepth<0 ||
       !std::isfinite(f.asymmetry) || std::abs(f.asymmetry)>=1 ||
       !std::isfinite(f.cloudBase) || !std::isfinite(f.cloudHeight) ||
       !std::isfinite(f.fogVisibility)||f.fogVisibility<0||f.fogVisibility>1e8||!std::isfinite(f.fogHeight)||f.fogHeight<=0||f.fogHeight>10000||!std::isfinite(vsg::length(f.fogLight))||
       f.cloudHeight<=f.cloudBase ||
       !std::isfinite(vsg::length(f.eye)) || !std::isfinite(vsg::length(f.cloudAnchor)) ||
       vsg::length(f.windAxis)<1e-12)
        throw std::invalid_argument("invalid planetary weather state");
    updateCloudObjects(f);
    if(!f.cloudObjects) {
        updateIntermediateCloud(f);
        updateSparseCloud(f);
    }
    gpu_->stepRequired=false;
    if(gpu_->field) {
        const auto projection=projectWgs84(f.eye);
        const bool fixed=hasCloudAnchor(f);
        const auto chart=fixed ? projectWgs84(f.cloudAnchor) : projection;
        const bool active=!f.cloudObjects && f.clouds && f.cover>0.f && (fixed || projection.altitude<80000.);
        bool rebase=false;
        if(active && (!gpu_->anchorValid || vsg::length(chart.surface-gpu_->anchor)>(fixed ? 1. : 4000.) ||
                       gpu_->cloudBase!=f.cloudBase || f.seconds<gpu_->lastSeconds)) {
            gpu_->anchor=chart.surface;gpu_->up=chart.up;
            gpu_->east=vsg::cross(vsg::dvec3(0,0,1),gpu_->up);
            if(vsg::length(gpu_->east)<1e-5)gpu_->east=vsg::cross(vsg::dvec3(1,0,0),gpu_->up);
            gpu_->east=vsg::normalize(gpu_->east);gpu_->north=vsg::normalize(vsg::cross(gpu_->up,gpu_->east));
            const auto& config=gpu_->field->config();
            gpu_->field->setOriginMetres({-.5f*config.width*config.voxelSizeMetres,
                                          f.cloudBase,
                                          -.5f*config.depth*config.voxelSizeMetres});
            gpu_->field->reset();gpu_->cloudBase=f.cloudBase;
            gpu_->anchorValid=true;rebase=true;
        }
        auto& layout=*gpu_->layout;
        const float gpuAuthority=active&&gpu_->anchorValid ?
            (fixed ? 1.f : smooth(float((80000.-projection.altitude)/35000.))) : 0.f;
        layout[0]=vsg::vec4(vsg::vec3(gpu_->anchor),gpuAuthority);
        layout[1]=vsg::vec4(vsg::vec3(gpu_->east),gpu_->field->config().voxelSizeMetres);
        layout[2]=vsg::vec4(vsg::vec3(gpu_->up),float(gpu_->field->config().width));
        layout[3]=vsg::vec4(vsg::vec3(gpu_->north),float(gpu_->field->config().height));
        layout[4]=vsg::vec4(gpu_->field->config().originMetres,float(gpu_->field->config().depth));
        layout.dirty();
        const auto substeps=gpu_->clock.advance(f.seconds,active,rebase);
        if(substeps) {
            GpuVoxelCloudFrame frame;
            frame.simulationSeconds=f.seconds;
            frame.stepSeconds=float(CloudStepClock::stepSeconds);
            const auto tangent=vsg::cross(f.windAxis,gpu_->up);
            // Wind gusts: real boundary-layer wind is never steady. Superpose
            // three incommensurate sinusoids on the mean flow so cloud edges
            // shred, drift and diffuse with the wind instead of sliding as a
            // rigid body. Gust amplitude grows with mean wind speed, matching
            // the observed gustiness of strong winds.
            const double gustPhase=f.seconds*2.*3.141592653589793;
            const float gust=1.f+.22f*std::sin(float(gustPhase)/7.3f)+
                             .13f*std::sin(float(gustPhase)/2.9f+.7f)+
                             .07f*std::sin(float(gustPhase)/1.7f+2.1f);
            const float gustSpeed=f.windSpeed*std::max(.55f,gust*(.75f+.35f*std::min(f.windSpeed/15.f,1.f)));
            frame.ambientWind={float(vsg::dot(tangent,gpu_->east))*gustSpeed,0.f,
                               float(vsg::dot(tangent,gpu_->north))*gustSpeed};
            // Clear environmental air must remain below saturation. Coverage
            // controls how close it is to saturation; the seeded thermal is
            // the only initially supersaturated parcel. Using 0.009+0.009C
            // condensed the entire bounded domain into a visible white box.
            frame.ambientVapourMixingRatio=.0065f+.0035f*f.cover;
            frame.cloudCover=f.cover;
            frame.ambientTemperatureK=std::max(225.f,288.15f-.0065f*f.cloudBase);
            frame.ambientPressurePa=101325.f*std::pow(std::max(.1f,1.f-.0065f*f.cloudBase/288.15f),5.25588f);
            frame.solarHeatingKPerSecond=.002f*std::max(0.f,float(vsg::dot(gpu_->up,vsg::dvec3(f.sun))));
            frame.colliders=colliders_;
            frame.impulses=impulses_;
            gpu_->field->update(frame,substeps);gpu_->stepRequired=true;
            // Impulses are one-shot per submitted step: the host re-adds an
            // impulse every frame for a sustained blast.
            if(!impulses_.empty())impulses_.clear();
        }
        gpu_->lastSeconds=f.seconds;
    }
    auto& u=*parameters_;
    u[0]=vsg::vec4{vsg::vec3(f.eye/a),f.cover}; u[1]=advection(f);
    u[2]=vsg::vec4{f.sun,f.windSpeed}; u[3]=vsg::vec4{f.detail,f.cloudRadius,f.seaRadius,f.rain};
    // Compute the near-surface quadratic constant in double, before upload.
    auto shell=[&](double h,bool on) {
        const double r=a+h,s=b+h;
        const double c=(f.eye.x*f.eye.x+f.eye.y*f.eye.y)/(r*r)+f.eye.z*f.eye.z/(s*s)-1.;
        return vsg::vec4(r/a,r/s,c,on?1.f:0.f);
    };
    u[4]=shell(f.seaLevel+.25,f.ocean); u[5]=shell(f.cloudHeight,f.clouds);
    u[6]=vsg::vec4{float(std::sin(f.seconds*2*pi/f.period)),float(std::cos(f.seconds*2*pi/f.period)),f.waves,float(4*pi*pi/(9.80665*f.period*f.period))};
    u[7]=vsg::vec4(vsg::vec3(f.windAxis),0.f);
    u[8]=vsg::vec4(f.opticalDepth,f.asymmetry,f.cloudBase,f.cloudHeight);
    u[9]=vsg::vec4(f.moon,f.moonPhase);
    u[10]=vsg::vec4(f.exposure,f.turbidity,shell(0.,true).z,float(gpu_->frameCounter%4u));
    ++gpu_->frameCounter;
    u[11]=shell(f.cloudBase,f.clouds);
    u[12]=vsg::vec4(f.displayAtmosphere?1.f:0.f,f.overcastAmount>=0?f.overcastAmount:(f.overcast?1.f:0.f),0.f,0.f);
    if(f.cloudObjects&&(*sparseCloud().parameters)[13].w>0.f){
        const auto& p=*sparseCloud().parameters;
        const auto vector=[](const vsg::vec4& v){return vsg::dvec3(v.x,v.y,v.z);};
        const auto relative=f.eye-vector(p[12]);
        // Subtract ECEF in double before uploading the metre-scale chart eye.
        u[12].z=float(vsg::dot(relative,vector(p[13])));
        u[12].w=float(vsg::dot(relative,vector(p[14])));
        u[7].w=float(vsg::dot(relative,vector(p[15])));
    }
    u[13]=vsg::vec4(f.fogVisibility,f.fogHeight,f.seaLevel,0);u[14]=vsg::vec4(f.fogLight,0);
    u.dirty();
    (*cloudResources_->weather)[0]=advection(f);
    (*cloudResources_->weather)[1]=vsg::vec4(f.clouds?f.cover:0.f,f.opticalDepth,f.cloudBase,f.cloudHeight);
    cloudResources_->weather->dirty();
}
vsg::Command* VoxelWeather::gpuInitializationCommands()const {
    return gpu_&&gpu_->field ? gpu_->field->initializationCommands() : nullptr;
}
vsg::Command* VoxelWeather::gpuStepCommands()const {
    return gpu_&&gpu_->field ? gpu_->field->stepCommands() : nullptr;
}
bool VoxelWeather::gpuStepRequired()const {return gpu_&&gpu_->stepRequired;}
}
