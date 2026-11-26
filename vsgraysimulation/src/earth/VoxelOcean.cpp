#include <vsgraysim/earth/VoxelOcean.h>
#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/EarthDataService.h>
#include "VoxelBuoyancy.h"
#include "VoxelTerrain.h"
#include <vsg/maths/transform.h>
#include <vsg/maths/vec4.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>
#include <array>
#include <unordered_map>
#include <cstdlib>
#include <cstdio>
namespace vsgraysim::earth {
namespace {
double number(const nlohmann::json& j,const char* key,double fallback,double lo,double hi){const double v=j.value(key,fallback);if(!std::isfinite(v)||v<lo||v>hi)throw std::invalid_argument(std::string("invalid ocean.")+key);return v;}
vsg::dvec3 vector(const nlohmann::json& j,const char* key,vsg::dvec3 fallback,double lo,double hi){if(!j.contains(key))return fallback;const auto& a=j.at(key);if(!a.is_array()||a.size()!=3)throw std::invalid_argument(std::string("invalid ocean.")+key);for(auto v:a)if(!v.is_number()||!std::isfinite(v.get<double>())||v.get<double>()<lo||v.get<double>()>hi)throw std::invalid_argument(std::string("invalid ocean.")+key);return {a[0].get<double>(),a[1].get<double>(),a[2].get<double>()};}
struct WakeValue {double height=0,vertical=0;vsg::dvec3 gradient{},verticalGradient{},hessian{};};
WakeValue wakeField(const OceanWake& w,const vsg::dvec3& current,double x,double z,double seconds,bool kinematics=false,double preparedAmplitude=-1,double preparedRate=0,bool wantHessian=false){
    WakeValue out;const double age=seconds-w.birth;if(age<=0||age>=w.lifetime)return out;
    const double qx=x-w.x-current.x*age,qz=z-w.z-current.z*age,reach=.55*w.width+w.speed*age+3*w.width;
    if(std::abs(qx)>reach||std::abs(qz)>reach)return out;
    const double r=std::sqrt(qx*qx+qz*qz+.04*w.width*w.width),d=r-(.55*w.width+w.speed*age),R=3*w.width;
    if(std::abs(d)>=R)return out;
    double A=preparedAmplitude,rate=preparedRate;
    if(A<0){const double fade=std::exp(-.38*age)*std::pow(1-age/w.lifetime,3),growth=-std::expm1(-6*age);A=w.amplitude*growth*fade;if(kinematics)rate=6*std::exp(-6*age)/growth-.38-3/(w.lifetime-age);}
    const double b=1-d*d/(R*R),E=b*b*b,Er=-6*d/(R*R)*b*b;
    const double c=std::cos(w.k*d),sn=std::sin(w.k*d),f=A*E*c,fr=A*(Er*c-w.k*E*sn);
    const vsg::dvec3 radial(qx/r,0,qz/r);out.height=f;out.gradient=radial*fr;if(!kinematics&&!wantHessian)return out;
    const double Err=-6/(R*R)*b*b+24*d*d/std::pow(R,4)*b,frr=A*((Err-w.k*w.k*E)*c-2*w.k*Er*sn);
    if(wantHessian){const double tangent=fr/r;out.hessian={frr*radial.x*radial.x+tangent*(1-radial.x*radial.x),(frr-tangent)*radial.x*radial.z,frr*radial.z*radial.z+tangent*(1-radial.z*radial.z)};}if(!kinematics)return out;
    out.vertical=rate*f-w.speed*fr-vsg::dot(current,out.gradient);
    const double tangent=fr/r;const auto hCurrent=radial*(vsg::dot(current,radial)*(frr-tangent))+vsg::dvec3(current.x,0,current.z)*tangent;
    out.verticalGradient=radial*(rate*fr-w.speed*frr)-hCurrent;return out;
}
double localWakeCurvature(const OceanWake& w,double r,double radius,double amplitude){
    const double R=3*w.width;
    const double e1=1.717301/R,e2=6/(R*R); // maxima of |(1-u^2)^3|' and |...|''
    return std::max(amplitude*(e2+2*w.k*e1+w.k*w.k),amplitude*(e1+w.k)/std::max(.2*w.width,r-radius));
}
double wakeCurvature(const OceanWake& w,double minimumRadius){const double R=3*w.width;return w.amplitude*(w.k*w.k+6/(R*R)+12*w.k/R+(3/R+w.k)/std::max(.2*w.width,minimumRadius));}

}
namespace {
// Spatial phases are independent of simulation time and immutable between
// spectrum edits. Bounded thread-local caches avoid trigonometry per leaf on
// every collision substep; no cross-thread mutable field or stale time cache.
struct SpatialWavePhases {
    struct Phase {std::array<double,16> c{},s{};};
    std::vector<OceanHarmonic> spectrum;double span=0;uint64_t generation=1;
    std::unordered_map<uint64_t,Phase> tiles;
    void configure(const VoxelOcean& o,double size){bool same=span==size&&spectrum.size()==o.harmonics.size();if(same)for(size_t n=0;n<spectrum.size();++n){const auto& a=spectrum[n];const auto& b=o.harmonics[n];if(a.amplitude!=b.amplitude||a.k!=b.k||a.dx!=b.dx||a.dz!=b.dz||a.phase!=b.phase){same=false;break;}}if(!same){++generation;span=size;spectrum=o.harmonics;tiles.clear();tiles.reserve(8192);}}
    const Phase& get(uint64_t key,double x,double z){auto found=tiles.find(key);if(found!=tiles.end())return found->second;if(tiles.size()>=65536)tiles.clear();auto& p=tiles[key];for(size_t n=0;n<spectrum.size();++n){const auto& h=spectrum[n];const double phase=h.k*(h.dx*x+h.dz*z)+h.phase;p.c[n]=h.amplitude*std::cos(phase);p.s[n]=h.amplitude*std::sin(phase);}return p;}
};
thread_local SpatialWavePhases spatialWavePhases;
}
struct OceanPressureQuery::Impl {
    const VoxelOcean& ocean;double time,span=.75,third=0;bool reference=false;size_t surfaceCalls=0,sampleCalls=0;double basis[16][6][6]{};std::array<double,16> timeCos{},timeSin{};bool spatialCache=true;uint64_t spatialGeneration=0;double exponentialSpan=.25;
    std::unordered_map<int32_t,std::array<double,16>> exponentialTiles;
    std::array<double,16> exponentials(double distance){const auto key=int32_t(std::floor(distance/exponentialSpan));auto [it,inserted]=exponentialTiles.try_emplace(key);const double centre=(key+.5)*exponentialSpan;if(inserted)for(size_t n=0;n<ocean.harmonics.size();++n)it->second[n]=std::exp(-ocean.harmonics[n].k*centre);auto value=it->second;for(size_t n=0;n<ocean.harmonics.size();++n){const double x=-ocean.harmonics[n].k*(distance-centre);value[n]*=1+x*(1+x*(.5+x*(1./6+x*(1./24+x/120))));}return value;}
    struct Tile {std::array<double,16> c{},s{};double polynomial[6][6]{};bool ready=false;};
    Tile& tile(int32_t ix,int32_t iz,bool wantPolynomial=false){
        const uint64_t key=(uint64_t(uint32_t(ix))<<32)|uint32_t(iz);auto [it,inserted]=tiles.try_emplace(key);auto& tile=it->second;
        const double cx=(ix+.5)*span,cz=(iz+.5)*span;
        if(inserted&&spatialCache){if(spatialGeneration!=spatialWavePhases.generation){spatialWavePhases.configure(ocean,span);spatialGeneration=spatialWavePhases.generation;}const auto& phase=spatialWavePhases.get(key,cx,cz);for(size_t n=0;n<ocean.harmonics.size();++n){tile.c[n]=phase.c[n]*timeCos[n]-phase.s[n]*timeSin[n];tile.s[n]=phase.s[n]*timeCos[n]+phase.c[n]*timeSin[n];}}
        else if(inserted)for(size_t n=0;n<ocean.harmonics.size();++n){const auto& h=ocean.harmonics[n];const double phase=h.k*(h.dx*cx+h.dz*cz)-h.omega*time+h.phase;tile.c[n]=h.amplitude*std::cos(phase);tile.s[n]=h.amplitude*std::sin(phase);}
        if(wantPolynomial&&!reference&&!tile.ready){for(size_t n=0;n<ocean.harmonics.size();++n){const double cycle[4]={tile.c[n],-tile.s[n],-tile.c[n],tile.s[n]};for(int i=0;i<=5;++i)for(int j=0;j<=5-i;++j)tile.polynomial[i][j]+=cycle[(i+j)%4]*basis[n][i][j];}tile.ready=true;}
        return tile;
    }std::unordered_map<uint64_t,Tile> tiles;
    struct Packet {const OceanWake* wake;double amplitude,rate,c0=0,c1=0,t0=0;};std::vector<Packet> packets;
    Impl(const VoxelOcean& o,double t):ocean(o),time(t){reference=std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO");spatialCache=!std::getenv("VSGRAYSIM_OCEAN_NO_SPATIAL_PHASE_CACHE");for(size_t n=0;n<o.harmonics.size();++n){timeCos[n]=std::cos(-o.harmonics[n].omega*t);timeSin[n]=std::sin(-o.harmonics[n].omega*t);}for(const auto& h:o.harmonics){span=std::min(span,.7/h.k);exponentialSpan=std::min(exponentialSpan,.1/h.k);third+=h.amplitude*h.k*h.k*h.k;}for(size_t n=0;n<o.harmonics.size();++n){const auto& h=o.harmonics[n];double xp=1;for(int i=0;i<=5;++i){double zp=1;for(int j=0;j<=5-i;++j){basis[n][i][j]=xp*zp;zp*=h.k*h.dz/(j+1);}xp*=h.k*h.dx/(i+1);}}tiles.reserve(256);if(spatialCache){spatialWavePhases.configure(ocean,span);spatialGeneration=spatialWavePhases.generation;}for(const auto& w:o.wakes){const double age=t-w.birth;if(age<=0||age>=w.lifetime)continue;packets.push_back({&w,w.amplitude*(-std::expm1(-6*age))*std::exp(-.38*age)*std::pow(1-age/w.lifetime,3),6*std::exp(-6*age)/(-std::expm1(-6*age))-.38-3/(w.lifetime-age)});if(!reference){auto& p=packets.back();const double R=3*w.width,k=w.k,ir=1/R,ir2=ir*ir;p.c0=p.amplitude*(k*k+6*ir2+12*k*ir);p.c1=p.amplitude*(3*ir+k);p.t0=p.amplitude*(k*k*k+9*k*k*ir+18*k*ir2+120*ir2*ir);}}}
};
OceanPressureQuery::OceanPressureQuery(const VoxelOcean& o,double seconds):impl_(std::make_unique<Impl>(o,seconds)){}
OceanPressureQuery::~OceanPressureQuery(){if(std::getenv("VSGRAYSIM_OCEAN_PROFILE"))std::fprintf(stderr,"OCEAN_QUERY surface=%zu flow=%zu tiles=%zu\n",impl_->surfaceCalls,impl_->sampleCalls,impl_->tiles.size());}
OceanSurfaceSample OceanPressureQuery::surface(double x,double z){
    auto& q=*impl_;++q.surfaceCalls;const auto ix=int32_t(std::floor(x/q.span)),iz=int32_t(std::floor(z/q.span));const double cx=(ix+.5)*q.span,cz=(iz+.5)*q.span;const auto& tile=q.tile(ix,iz,true);
    const auto mean=q.ocean.meanSurface(x,z);OceanSurfaceSample s;s.height=mean.x;double gx=mean.y,gz=mean.z;
    if(q.reference){for(size_t i=0;i<q.ocean.harmonics.size();++i){const auto& h=q.ocean.harmonics[i];const double d=h.k*(h.dx*(x-cx)+h.dz*(z-cz)),d2=d*d,c=tile.c[i],sn=tile.s[i];s.height+=c*(1-d2*.5+d2*d2/24)-sn*d*(1-d2/6+d2*d2/120);const double derivative=c*(-d+d*d2/6)-sn*(1-d2*.5+d2*d2/24);gx+=derivative*h.k*h.dx;gz+=derivative*h.k*h.dz;}}
    else {
        // The same fifth-order phase expansion, summed once per tile.
        // Horner evaluation shares height, gradient and Hessian work. This
        // changes neither the phase error budget nor waterline tolerance.
        const double dx=x-cx,dz=z-cz;double h=0,hx=0,hxx=0,hz=0,hxz=0,hzz=0;
        for(int i=5;i>=0;--i){double row=0,rz=0,rzz=0;for(int j=5-i;j>=0;--j){rzz=rzz*dz+2*rz;rz=rz*dz+row;row=row*dz+tile.polynomial[i][j];}hxx=hxx*dx+2*hx;hx=hx*dx+h;h=h*dx+row;hxz=hxz*dx+hz;hz=hz*dx+rz;hzz=hzz*dx+rzz;}
        s.height+=h;gx+=hx;gz+=hz;s.hessian={hxx,hxz,hzz};
    }
    for(const auto& packet:q.packets){const auto w=wakeField(*packet.wake,q.ocean.current,x,z,q.time,false,packet.amplitude,0,!q.reference);s.height+=w.height;s.hessian+=w.hessian;gx+=w.gradient.x;gz+=w.gradient.z;}
    s.normal=vsg::normalize(vsg::dvec3(-gx,1,-gz));return s;
}
OceanSample OceanPressureQuery::sample(double x,double z,double y){
    auto& q=*impl_;++q.sampleCalls;const auto ix=int32_t(std::floor(x/q.span)),iz=int32_t(std::floor(z/q.span));const double cx=(ix+.5)*q.span,cz=(iz+.5)*q.span;const auto& tile=q.tile(ix,iz);
    const auto mean=q.ocean.meanSurface(x,z);const double waterDepth=mean.x-q.ocean.bottom,depth=std::clamp(mean.x-std::min(y,mean.x),0.,waterDepth);
    const bool cacheExponentials=!q.reference&&!std::getenv("VSGRAYSIM_OCEAN_NO_EXPONENTIAL_CACHE");std::array<double,16> depthExp{},bedExp{};if(cacheExponentials){depthExp=q.exponentials(depth);bedExp=q.exponentials(2*waterDepth);}
    OceanSample s;s.height=mean.x;s.velocity=q.ocean.current;double gx=mean.y,gz=mean.z;
    for(size_t i=0;i<q.ocean.harmonics.size();++i){const auto& h=q.ocean.harmonics[i];const double d=h.k*(h.dx*(x-cx)+h.dz*(z-cz)),d2=d*d,c=tile.c[i],sn=tile.s[i];const double ca=c*(1-d2*.5+d2*d2/24)-sn*d*(1-d2/6+d2*d2/120),sa=sn*(1-d2*.5+d2*d2/24)+c*d*(1-d2/6+d2*d2/120);s.height+=ca;
        const double D=waterDepth,a=cacheExponentials?depthExp[i]:std::exp(-h.k*depth),b=cacheExponentials&&a>1e-150?bedExp[i]/a:std::exp(-h.k*(2*D-depth)),denominator=q.ocean.referenceSize?(a*b<.99?1./(1-a*b):1./(-std::expm1(-2*h.k*D))):h.inverseDepthDenominator,horizontal=(a+b)*denominator,vertical=(a-b)*denominator;
        s.velocity+=vsg::dvec3(h.dx*horizontal*ca,vertical*sa,h.dz*horizontal*ca)*h.omega;
        s.velocityDx+=vsg::dvec3(-h.dx*horizontal*sa,vertical*ca,-h.dz*horizontal*sa)*(h.omega*h.k*h.dx);
        s.velocityDz+=vsg::dvec3(-h.dx*horizontal*sa,vertical*ca,-h.dz*horizontal*sa)*(h.omega*h.k*h.dz);
        if(y<mean.x&&y>q.ocean.bottom)s.velocityDy+=vsg::dvec3(h.dx*vertical*ca,horizontal*sa,h.dz*vertical*ca)*(h.omega*h.k);
        const double derivative=c*(-d+d*d2/6)-sn*(1-d2*.5+d2*d2/24);gx+=derivative*h.k*h.dx;gz+=derivative*h.k*h.dz;}
    for(const auto& packet:q.packets){const auto w=wakeField(*packet.wake,q.ocean.current,x,z,q.time,true,packet.amplitude,packet.rate);const double atten=std::exp(packet.wake->k*std::min(0.,y-mean.x));s.velocity.y+=w.vertical*atten;s.velocityDx.y+=w.verticalGradient.x*atten;s.velocityDz.y+=w.verticalGradient.z*atten;if(y<mean.x)s.velocityDy.y+=packet.wake->k*w.vertical*atten;s.height+=w.height;gx+=w.gradient.x;gz+=w.gradient.z;}
    s.normal=vsg::normalize(vsg::dvec3(-gx,1,-gz));return s;
}
double OceanPressureQuery::curvature(double x,double z,double radius,const vsg::dvec3* hessian)const{
    const auto& q=*impl_;double bound=q.ocean.baseCurvatureBound,third=q.third;
    for(const auto& packet:q.packets){const auto& w=*packet.wake;const double age=q.time-w.birth,dx=x-w.x-q.ocean.current.x*age,dz=z-w.z-q.ocean.current.z*age,reach=.55*w.width+w.speed*age+3*w.width+radius;if(std::abs(dx)>reach||std::abs(dz)>reach||w.amplitude==0)continue;const double r=std::sqrt(dx*dx+dz*dz+.04*w.width*w.width);if(std::abs(r-(.55*w.width+w.speed*age))>3*w.width+radius)continue;if(q.reference||!hessian){bound+=wakeCurvature(w,r-radius)*(packet.amplitude/w.amplitude);continue;}
        const double inverse=1/std::max(.2*w.width,r-radius),scaled=packet.c1*inverse;bound+=std::min(packet.c0+scaled,localWakeCurvature(w,r,radius,packet.amplitude));third+=packet.t0+3*(packet.c0+scaled)*inverse;}

    if(!hessian||q.reference)return bound;
    const auto h=*hessian;const double norm=.5*(std::abs(h.x+h.z)+std::sqrt((h.x-h.z)*(h.x-h.z)+4*h.y*h.y));
    // Hessian Lipschitz bound, with analytic third derivatives. Taking the
    // smaller of two valid bounds avoids refining against unrelated phases.
    return std::min(bound,norm+third*radius+q.ocean.baseCurvatureBound*.003+(q.ocean.referenceSize?1e-6:0));
}
double OceanPressureQuery::amplitudeBound()const{const auto& q=*impl_;double a=0;for(const auto& h:q.ocean.harmonics)a+=h.amplitude;for(const auto& p:q.packets)a+=p.amplitude;return a;}
std::shared_ptr<const VoxelOcean> VoxelOcean::load(const nlohmann::json& j,const simcore::geo::GeoAnchor* anchor,const std::string& documentPath){
    if(!j.is_object())throw std::invalid_argument("ocean must be an object");if(!j.value("enabled",true))return {};
    const std::set<std::string> allowed{"enabled","sea_level_m","bottom_m","half_extent_m","cell_size_m","density_kg_m3","drag_coefficient","linear_drag_mps","temperature_k","wind_speed_mps","wind_azimuth_deg","wave_height_m","wave_length_m","swell_height_m","swell_length_m","current_mps","absorption_rgb_per_m","scattering_rgb_per_m","harmonics","wakes","reference_surface","provenance","sea_fog","splashes","foam"};for(const auto& item:j.items())if(!allowed.count(item.key()))throw std::invalid_argument("unknown ocean field: "+item.key());
    auto o=std::make_shared<VoxelOcean>();
    o->seaLevel=number(j,"sea_level_m",0,-10000,10000);o->bottom=number(j,"bottom_m",-40,-10000,10000);if(o->bottom>=o->seaLevel)throw std::invalid_argument("ocean bottom must be below sea level");
    o->halfExtent=number(j,"half_extent_m",4096,8,80000);o->cellSize=number(j,"cell_size_m",.5,.01,32);o->density=number(j,"density_kg_m3",1025,900,1300);o->drag=number(j,"drag_coefficient",.85,0,4);o->temperature=number(j,"temperature_k",291,250,330);
    o->windSpeed=number(j,"wind_speed_mps",6,0,50);o->windAzimuth=number(j,"wind_azimuth_deg",70,-360,360);o->waveHeight=number(j,"wave_height_m",.65,0,12);o->waveLength=number(j,"wave_length_m",24,2,1000);o->swellHeight=number(j,"swell_height_m",.25,0,10);o->swellLength=number(j,"swell_length_m",80,10,1500);
    o->current=vector(j,"current_mps",o->current,-10,10);o->absorption=vector(j,"absorption_rgb_per_m",o->absorption,0,100);o->scattering=vector(j,"scattering_rgb_per_m",o->scattering,0,100);o->provenance=j.value("provenance",nlohmann::json::object());
    o->linearDrag=number(j,"linear_drag_mps",.2,0,5);
    if(j.contains("sea_fog")){const auto& fog=j.at("sea_fog");if(!fog.is_object())throw std::invalid_argument("sea_fog must be an object");for(const auto& a:fog.items())if(a.key()!="visibility_m"&&a.key()!="height_m")throw std::invalid_argument("unknown sea_fog field");o->fogVisibility=number(fog,"visibility_m",0,0,200000);if(o->fogVisibility>0&&o->fogVisibility<50)throw std::invalid_argument("marine visibility must be zero or >=50m");o->fogHeight=number(fog,"height_m",45,1,1000);}
    if(j.contains("foam")){const auto& events=j.at("foam");if(!events.is_array()||events.size()>maximumFoam)throw std::invalid_argument("invalid ocean foam table");for(const auto& e:events){OceanFoam p;p.x=number(e,"x",0,-o->halfExtent,o->halfExtent);p.z=number(e,"z",0,-o->halfExtent,o->halfExtent);p.birth=number(e,"birth_s",0,0,1e12);p.length=number(e,"length_m",1,.05,1000);p.width=number(e,"width_m",1,.05,100);p.dirX=number(e,"direction_x",0,-1,1);p.dirZ=number(e,"direction_z",-1,-1,1);if(std::abs(p.dirX*p.dirX+p.dirZ*p.dirZ-1)>1e-5)throw std::invalid_argument("foam direction must be normalized");p.strength=number(e,"strength",0,0,1);p.lifetime=number(e,"lifetime_s",24,.1,60);p.seed=uint32_t(number(e,"seed",0,0,4294967295.));o->foam.push_back(p);}}
    if(j.contains("splashes")){const auto& events=j.at("splashes");if(!events.is_array()||events.size()>maximumSplashes)throw std::invalid_argument("invalid ocean splash table");for(const auto& e:events){OceanSplash p;p.x=number(e,"x",0,-o->halfExtent,o->halfExtent);p.z=number(e,"z",0,-o->halfExtent,o->halfExtent);p.birth=number(e,"birth_s",0,0,1e12);p.height=number(e,"height_m",0,-20000,20000);p.speed=number(e,"speed_mps",0,0,20);p.radius=number(e,"radius_m",1,.05,20);p.lifetime=number(e,"lifetime_s",2,.1,6);p.seed=uint32_t(number(e,"seed",0,0,4294967295.));p.drift=vector(e,"drift_mps",{},-60.,60.);o->splashes.push_back(p);}}

    if(!o->provenance.is_object())throw std::invalid_argument("ocean provenance must be an object");
    if(j.contains("reference_surface")){
        const auto& ref=j.at("reference_surface");
        if(ref.contains("map")){if(!anchor)throw std::invalid_argument("ocean reference map needs a scene anchor");const auto map=EarthDataService::resolveUri(ref.at("map").get<std::string>(),documentPath);const auto surface=EarthDataService::loadMap(map,*anchor,ref.value("earth",nlohmann::json::object()));o->referenceSize=ref.value("grid_size",257u);if(o->referenceSize<2||o->referenceSize>513)throw std::invalid_argument("ocean reference grid must be 2..513");o->referenceX=surface->min_x;o->referenceZ=surface->min_z;o->referenceStepX=(surface->max_x-surface->min_x)/(o->referenceSize-1);o->referenceStepZ=(surface->max_z-surface->min_z)/(o->referenceSize-1);for(uint32_t z=0;z<o->referenceSize;++z)for(uint32_t x=0;x<o->referenceSize;++x){auto h=surface->sampleLocal(o->referenceX+x*o->referenceStepX,o->referenceZ+z*o->referenceStepZ);if(h.status==SurfaceStatus::NoData)throw std::invalid_argument("ocean reference datum has NoData");o->referenceHeights.push_back(float(h.height));}}
        else{o->referenceSize=ref.at("grid_size").get<uint32_t>();o->referenceX=number(ref,"min_x",0,-1e6,1e6);o->referenceZ=number(ref,"min_z",0,-1e6,1e6);o->referenceStepX=number(ref,"step_x",1,.001,1e6);o->referenceStepZ=number(ref,"step_z",1,.001,1e6);o->referenceHeights=ref.at("heights_m").get<std::vector<float>>();}
        if(o->referenceSize<2||o->referenceSize>513||o->referenceHeights.size()!=size_t(o->referenceSize)*o->referenceSize)throw std::invalid_argument("invalid ocean reference datum grid");for(float h:o->referenceHeights)if(!std::isfinite(h))throw std::invalid_argument("nonfinite ocean reference datum");o->referenceMin=*std::min_element(o->referenceHeights.begin(),o->referenceHeights.end());o->referenceMax=*std::max_element(o->referenceHeights.begin(),o->referenceHeights.end());const auto working=o->meanBounds(-o->halfExtent,-o->halfExtent,o->halfExtent,o->halfExtent);o->referenceMin=working.first-o->seaLevel;o->referenceMax=working.second-o->seaLevel;for(uint32_t z=0;z<o->referenceSize;++z)for(uint32_t x=0;x<o->referenceSize;++x){const auto at=[&](uint32_t a,uint32_t b){return double(o->referenceHeights[size_t(b)*o->referenceSize+a]);};if(x+1<o->referenceSize)o->referenceSlope=std::max(o->referenceSlope,std::abs((at(x+1,z)-at(x,z))/o->referenceStepX));if(z+1<o->referenceSize)o->referenceSlope=std::max(o->referenceSlope,std::abs((at(x,z+1)-at(x,z))/o->referenceStepZ));if(x&&x+1<o->referenceSize)o->referenceJumpX=std::max(o->referenceJumpX,std::abs(at(x+1,z)-2*at(x,z)+at(x-1,z))/o->referenceStepX);if(z&&z+1<o->referenceSize)o->referenceJumpZ=std::max(o->referenceJumpZ,std::abs(at(x,z+1)-2*at(x,z)+at(x,z-1))/o->referenceStepZ);}if(o->referenceMin+o->seaLevel<=o->bottom)throw std::invalid_argument("ocean reference is below declared water volume bottom");
    }
    if(j.contains("harmonics")) {
        const auto& waves=j.at("harmonics");if(!waves.is_array()||waves.size()>16)throw std::invalid_argument("invalid ocean harmonic table");
        for(const auto& w:waves){OceanHarmonic h;h.amplitude=number(w,"amplitude_m",0,0,20);h.k=number(w,"k",1,.0001,100);h.dx=number(w,"dx",1,-1,1);h.dz=number(w,"dz",0,-1,1);h.omega=number(w,"omega",1,0,100);h.phase=number(w,"phase",0,-1e6,1e6);if(std::abs(h.dx*h.dx+h.dz*h.dz-1)>1e-5)throw std::invalid_argument("ocean wave direction must be normalized");o->harmonics.push_back(h);}
    } else {
        const double az=o->windAzimuth*3.141592653589793/180.;
        auto normalize=[&](size_t first,double hs){double energy=0;for(size_t i=first;i<o->harmonics.size();++i)energy+=o->harmonics[i].amplitude*o->harmonics[i].amplitude;if(energy>0){const double scale=hs/std::sqrt(8*energy);for(size_t i=first;i<o->harmonics.size();++i)o->harmonics[i].amplitude*=scale;}};
        // Log-spaced wind bands and a deterministic broad directional spread.
        // The old spectrum put almost all variance into two parallel modes.
        if(o->waveHeight>0){for(int i=0;i<13;++i){const double u=double(i)/12,ratio=std::exp(std::log(.6)+u*std::log(7./.6));const double angle=az+std::sin(i*2.399963229728653)*(.45+.38*u);OceanHarmonic h;h.k=6.283185307179586/o->waveLength*ratio;h.amplitude=std::exp(-.5*std::pow(std::log(ratio)/.74,2))*std::pow(ratio,-.3);h.dx=std::sin(angle);h.dz=-std::cos(angle);h.phase=std::remainder(i*4.5783+.83,6.283185307179586);o->harmonics.push_back(h);}normalize(0,o->waveHeight);}
        if(o->swellHeight>0){const auto first=o->harmonics.size();for(int i=0;i<3;++i){const double angle=az+.55+(i-1)*.22;OceanHarmonic h;h.k=6.283185307179586/o->swellLength*(i==0?.8:i==1?1.:1.35);h.amplitude=i==1?1.:.55;h.dx=std::sin(angle);h.dz=-std::cos(angle);h.phase=.184+i*2.731;o->harmonics.push_back(h);}normalize(first,o->swellHeight);}
    }
    if(j.contains("wakes")){const auto& wakes=j.at("wakes");if(!wakes.is_array()||wakes.size()>maximumWakes)throw std::invalid_argument("invalid ocean wake table");for(const auto& w:wakes){OceanWake q;q.x=number(w,"x",0,-o->halfExtent,o->halfExtent);q.z=number(w,"z",0,-o->halfExtent,o->halfExtent);q.birth=number(w,"birth_s",0,0,1e12);q.amplitude=number(w,"amplitude_m",0,0,.08);q.width=number(w,"width_m",1,.5,20);q.speed=number(w,"speed_mps",2,.1,10);q.lifetime=number(w,"lifetime_s",8,1,12);q.k=number(w,"k",1,.01,8);q.dirX=number(w,"direction_x",0,-1,1);q.dirZ=number(w,"direction_z",-1,-1,1);q.bodySpeed=number(w,"body_speed_mps",0,0,1e8);if(std::abs(q.dirX*q.dirX+q.dirZ*q.dirZ-1)>1e-5)throw std::invalid_argument("wake direction must be normalized");o->wakes.push_back(q);}}

    for(auto& h:o->harmonics){const double kd=h.k*(o->seaLevel-o->bottom);if(!j.contains("harmonics"))h.omega=std::sqrt(9.80665*h.k*std::tanh(kd));h.inverseDepthDenominator=1./(-std::expm1(-2*kd));}
    o->refreshBounds();
    double baseAmplitude=0,baseSlope=0;for(const auto& h:o->harmonics){baseAmplitude+=h.amplitude;baseSlope+=h.k*h.amplitude;}
    if(baseAmplitude>(o->seaLevel-o->bottom)*.45||baseSlope>1.5)throw std::invalid_argument("ocean spectrum exceeds nonbreaking wave/depth validity; reduce wave height");
    return o;
}
double VoxelOcean::meanKinkError(double x,double z,double r)const{
    if(!referenceSize)return 0;auto jumps=[&](double c,double low,double step){const double a=(c-r-low)/step,b=(c+r-low)/step;const int interior=std::max(0,std::min(int(referenceSize)-2,int(std::floor(b)))-std::max(1,int(std::ceil(a)))+1);const int edges=(a<0&&b>=0)+(a<=referenceSize-1&&b>referenceSize-1);return std::pair<int,int>(interior,edges);};const auto a=jumps(x,referenceX,referenceStepX),b=jumps(z,referenceZ,referenceStepZ);return 2*r*(a.first*referenceJumpX+b.first*referenceJumpZ+(a.second+b.second)*referenceSlope);
}
std::pair<double,double> VoxelOcean::meanBounds(double lowX,double lowZ,double highX,double highZ)const{
    if(!referenceSize)return {seaLevel,seaLevel};std::vector<double> xs{lowX,highX},zs{lowZ,highZ};const int xa=std::max(0,int(std::ceil((lowX-referenceX)/referenceStepX))),xb=std::min(int(referenceSize)-1,int(std::floor((highX-referenceX)/referenceStepX))),za=std::max(0,int(std::ceil((lowZ-referenceZ)/referenceStepZ))),zb=std::min(int(referenceSize)-1,int(std::floor((highZ-referenceZ)/referenceStepZ)));for(int i=xa;i<=xb;++i)xs.push_back(referenceX+i*referenceStepX);for(int i=za;i<=zb;++i)zs.push_back(referenceZ+i*referenceStepZ);double lo=1e30,hi=-1e30;for(double z:zs)for(double x:xs){const double h=meanSurface(x,z).x;lo=std::min(lo,h);hi=std::max(hi,h);}return {lo,hi};
}
vsg::dvec3 VoxelOcean::meanSurface(double x,double z)const{
    if(!referenceSize)return {seaLevel,0,0};const double fx=std::clamp((x-referenceX)/referenceStepX,0.,double(referenceSize-1)),fz=std::clamp((z-referenceZ)/referenceStepZ,0.,double(referenceSize-1));const int ix=std::min(int(fx),int(referenceSize)-2),iz=std::min(int(fz),int(referenceSize)-2);const double u=fx-ix,v=fz-iz;const double a=referenceHeights[size_t(iz)*referenceSize+ix],b=referenceHeights[size_t(iz)*referenceSize+ix+1],c=referenceHeights[size_t(iz+1)*referenceSize+ix],d=referenceHeights[size_t(iz+1)*referenceSize+ix+1];return {seaLevel+(a*(1-u)+b*u)*(1-v)+(c*(1-u)+d*u)*v,(x<referenceX||x>referenceX+referenceStepX*(referenceSize-1))?0:((b-a)*(1-v)+(d-c)*v)/referenceStepX,(z<referenceZ||z>referenceZ+referenceStepZ*(referenceSize-1))?0:((c-a)*(1-u)+(d-b)*u)/referenceStepZ};
}
void VoxelOcean::refreshBounds(){
    amplitudeBound=0;slopeBound=0;baseCurvatureBound=referenceSize?1e-6:0;minimumWavelength=1e30;
    for(const auto& h:harmonics){amplitudeBound+=h.amplitude;slopeBound+=h.k*h.amplitude;baseCurvatureBound+=h.amplitude*h.k*h.k;minimumWavelength=std::min(minimumWavelength,6.283185307179586/h.k);}
    curvatureBound=baseCurvatureBound;
    for(const auto& w:wakes){amplitudeBound+=w.amplitude;curvatureBound+=wakeCurvature(w,0);slopeBound+=w.amplitude*(w.k+1/w.width);minimumWavelength=std::min(minimumWavelength,6.283185307179586/w.k);}
    integrationSpan=std::min(2.,std::sqrt(.005/std::max(1e-12,curvatureBound)));
}
OceanSurfaceSample VoxelOcean::surface(double x,double z,double t,bool includeWakes)const{
    if(!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(t))throw std::invalid_argument("nonfinite ocean surface query");
    const auto mean=meanSurface(x,z);OceanSurfaceSample s;s.height=mean.x;vsg::dvec3 gradient(mean.y,0,mean.z);
    for(const auto& h:harmonics){const double phase=h.k*(h.dx*x+h.dz*z)-h.omega*t+h.phase;const double sine=std::sin(phase);s.height+=h.amplitude*std::cos(phase);gradient.x-=h.k*h.amplitude*h.dx*sine;gradient.z-=h.k*h.amplitude*h.dz*sine;}
    if(includeWakes)for(const auto& w:wakes){const auto q=wakeField(w,current,x,z,t);s.height+=q.height;gradient+=q.gradient;}
    s.normal=vsg::normalize(vsg::dvec3(-gradient.x,1,-gradient.z));return s;
}
std::vector<OceanPropellerJet> oceanPropellerJets(const VoxelSceneSnapshot& scene){
    std::vector<OceanPropellerJet> result;
    if(!scene.ocean)return result;
    const auto& water=*scene.ocean;
    const double time=scene.timeSeconds();
    for(const auto& [id,part]:scene.objects){
        if(!part.rotor||!part.rotor->waterJet||!part.volume||!part.volume->occupiedCount())continue;
        const auto& drive=*part.rotor;
        const double spin=drive.rpmAt(time)*vsg::PI/30.;
        if(std::abs(spin)<.01)continue;
        const auto pose=part.pose(scene.tick,scene.fixedStep);
        const auto hub=pose*vsg::dvec3{};
        if(water.densityAt(hub,time)<=0.)continue;
        const auto axis=pose*vsg::dvec4(drive.axis,0.);
        const auto bounds=part.localBounds();
        const double radius=.5*std::max(bounds.max.y-bounds.min.y,bounds.max.z-bounds.min.z);
        const vsg::dvec3 aft{-axis.x,-axis.y,-axis.z};
        const double age=std::clamp(time-drive.startSeconds,0.,3.2);
        const double speed=std::clamp(.21*std::abs(spin)*radius,0.,8.);
        const double birth=time-age;
        const double rampEnd=std::clamp(drive.startSeconds+drive.spinUpSeconds,birth,time);
        // RPM is cubic during spin-up and then constant. Integrate it before phase wrapping.
        const double ramp=(rampEnd-birth)/6.*(drive.rpmAt(birth)+4.*drive.rpmAt((birth+rampEnd)*.5)+drive.rpmAt(rampEnd));
        const double revolutions=(ramp+(time-rampEnd)*drive.rpm)/60.;
        const double path=std::min(8.*age,.21*radius*std::abs(revolutions)*2.*vsg::PI);
        // Water emitted earlier has moved with the current while the hub continued along its route.
        const auto plume=aft*path+(water.current-part.velocity)*age;
        if(vsg::length(plume)<.04)continue;
        result.push_back({hub,aft,plume,radius,speed,spin,age,std::remainder(drive.angleAt(time),2.*vsg::PI)});
        if(result.size()==maximumOceanPropellerJets)break;
    }
    return result;
}
namespace {
void depositHullSpray(VoxelOcean& ocean,const VoxelSceneSnapshot& scene,const VoxelSceneObject& hull) {
    if(!hull.volume->occupiedCount())return;
    const double time=scene.timeSeconds();
    const auto pose=hull.pose(scene.tick,scene.fixedStep);
    const auto bounds=hull.localBounds();
    const auto size=bounds.max-bounds.min;
    const auto center=(bounds.max+bounds.min)*.5;
    const int lengthAxis=size.z>=size.x?2:0;
    const int beamAxis=2-lengthAxis;
    const vsg::dvec3 forward{pose[lengthAxis][0],0.,pose[lengthAxis][2]};
    const vsg::dvec3 beam{pose[beamAxis][0],0.,pose[beamAxis][2]};
    const double beamLength=vsg::length(beam);
    const double ahead=vsg::dot(hull.velocity-ocean.current,forward);
    if(std::abs(ahead)<.55||beamLength<.1)return;
    const auto origin=pose*vsg::dvec3{};
    uint32_t seed=2166136261u;
    for(unsigned char c:hull.id)seed=(seed^c)*16777619u;

    for(int side:{-1,1}){
        auto shoulder=center;
        shoulder[lengthAxis]+=std::copysign(.31*size[lengthAxis],ahead);
        auto ray=pose*shoulder;
        const auto outward=beam*(double(side)/beamLength);
        const double reach=.78*size[beamAxis]+.6;
        ray+=outward*reach;
        if(!ocean.contains(ray.x,ray.z))continue;
        ray.y=ocean.surface(ray.x,ray.z,time).height;
        std::optional<VoxelSceneHit> contact;
        for(int i=0;i<2;++i){
            contact=raycastVoxelScene(scene,ray,-outward,reach,false,hull.id);
            if(!contact)break;
            ray.y=ocean.surface(contact->position.x,contact->position.z,time).height;
        }
        if(!contact)continue;
        const auto point=contact->position;
        const auto water=ocean.sample(point.x,point.z,time,point.y);
        if(std::abs(point.y-water.height)>.12)continue;
        const auto relative=hull.velocity+vsg::cross(hull.angularVelocity,point-origin)-water.velocity;
        const double closing=-vsg::dot(relative,water.normal);
        if(closing<.055)continue;

        OceanSplash burst;
        burst.x=point.x;burst.z=point.z;burst.height=water.height;burst.birth=time;
        burst.speed=std::clamp(1.32+1.8*closing+.14*std::abs(ahead),1.4,4.4);
        burst.radius=std::clamp(.66+.42*closing+.12*std::abs(ahead),.7,1.6);
        burst.lifetime=.64;
        burst.seed=seed^uint32_t(scene.tick+side);
        burst.drift=hull.velocity*.42+outward*(.8+1.3*closing);
        burst.drift.y=0.;
        burst.drift.x=std::clamp(burst.drift.x,-40.,40.);
        burst.drift.z=std::clamp(burst.drift.z,-40.,40.);
        ocean.splashes.push_back(burst);
    }
}
}
double VoxelOcean::localCurvature(double x,double z,double seconds,double radius)const{
    double bound=baseCurvatureBound;
    for(const auto& w:wakes){const double age=seconds-w.birth;if(age<0||age>=w.lifetime)continue;const double dx=x-w.x-current.x*age,dz=z-w.z-current.z*age,r=std::sqrt(dx*dx+dz*dz+.04*w.width*w.width);if(std::abs(r-(.55*w.width+w.speed*age))>3*w.width+radius)continue;bound+=wakeCurvature(w,r-radius)*(-std::expm1(-6*age))*std::exp(-.38*age)*std::pow(1-age/w.lifetime,3);}
    return bound;
}
void VoxelOcean::advanceWakes(VoxelSceneSnapshot& scene,const VoxelSceneSnapshot* previous){
    if(!scene.ocean)return;auto next=std::make_shared<VoxelOcean>(*scene.ocean);const double time=scene.timeSeconds();
    next->foam.erase(std::remove_if(next->foam.begin(),next->foam.end(),[&](const OceanFoam& f){return time-f.birth>=f.lifetime;}),next->foam.end());
    next->splashes.erase(std::remove_if(next->splashes.begin(),next->splashes.end(),[&](const OceanSplash& s){return time-s.birth>=s.lifetime;}),next->splashes.end());
    next->wakes.erase(std::remove_if(next->wakes.begin(),next->wakes.end(),[&](const OceanWake& w){return time-w.birth>=w.lifetime;}),next->wakes.end());
    if(previous&&previous->ocean)for(const auto& [id,body]:scene.objects){
        auto old=previous->objects.find(id);if(body.body.motion!="dynamic"||!body.volume||!body.volume->occupiedCount()||old==previous->objects.end())continue;
        const auto bounds=body.localBounds();const auto before=old->second.pose(previous->tick,previous->fixedStep),after=body.pose(scene.tick,scene.fixedStep);vsg::dvec3 a,b;double ay=1e30,by=1e30;
        for(int k=0;k<8;++k){const vsg::dvec3 local(k&1?bounds.max.x:bounds.min.x,k&2?bounds.max.y:bounds.min.y,k&4?bounds.max.z:bounds.min.z);const auto p=before*local,q=after*local;if(p.y<ay){a=p;ay=p.y;}if(q.y<by){b=q;by=q.y;}}
        if(!next->contains(b.x,b.z))continue;const auto water=next->sample(b.x,b.z,time,b.y);const auto oldWater=previous->ocean->surface(a.x,a.z,previous->timeSeconds());const double entry=(ay-by)/scene.fixedStep+water.velocity.y;
        if(ay>oldWater.height&&by<=water.height&&entry>.65){const auto size=bounds.max-bounds.min;OceanSplash p;p.x=b.x;p.z=b.z;p.height=water.height;p.birth=time;p.radius=std::clamp(std::sqrt(size.x*size.z)*.22,.12,3.);p.speed=std::clamp(entry*.48,.8,9.);p.lifetime=std::clamp(2*p.speed/9.80665+.7,1.,3.);uint32_t seed=2166136261u;for(unsigned char c:id)seed=(seed^c)*16777619u;p.seed=seed^uint32_t(scene.tick);next->splashes.push_back(p);
            OceanFoam f;f.x=p.x;f.z=p.z;f.birth=time;f.length=f.width=p.radius*1.4;f.strength=std::clamp(entry*.12,.1,.8);f.lifetime=18.;f.seed=p.seed;next->foam.push_back(f);
            OceanWake w;w.x=p.x;w.z=p.z;w.birth=time;w.width=std::clamp(p.radius,.5,3.);w.k=1.8/w.width;w.speed=std::sqrt(9.80665/w.k)*.65;w.amplitude=std::min(.06,.008*entry*std::sqrt(std::max(.001,body.massKg()/next->density)));w.lifetime=5.;next->wakes.push_back(w);
        }
    }
    // Use the transformed, occupied bow shoulder and the current wave normal.
    // These short spray bursts do not feed back into the wave or collision solver.
    const uint64_t sprayCadence=std::max<uint64_t>(1,std::llround(.12/scene.fixedStep));
    if(scene.tick%sprayCadence==0)
        for(const auto& [id,body]:scene.objects)
            if(body.motionResponse==VoxelMotionResponse::Waves)depositHullSpray(*next,scene,body);
    if(next->splashes.size()>maximumSplashes)next->splashes.erase(next->splashes.begin(),next->splashes.end()-maximumSplashes);
    // Fixed simulation-time cadence, independent of display frames and caches.
    const uint64_t cadence=std::max<uint64_t>(1,std::llround(.4/scene.fixedStep));
    if(scene.tick%cadence==0)for(const auto& [id,body]:scene.objects){
        if(body.body.motion=="static"||!body.volume||body.massKg()<=0)continue;
        const auto pose=body.pose(scene.tick,scene.fixedStep);const auto centre=pose*body.localCentreOfMass();if(!next->contains(centre.x,centre.z))continue;
        const auto bounds=body.localBounds();const auto size=bounds.max-bounds.min;double verticalRadius=0;for(int a=0;a<3;++a)verticalRadius+=std::abs(pose[a][1])*size[a]*.5;
        const auto water=next->surface(centre.x,centre.z,time);const double depth=water.height-centre.y;
        if(std::abs(depth)>verticalRadius+.1)continue;
        const auto offset=centre-pose*vsg::dvec3{};const auto velocity=body.velocity+vsg::cross(body.angularVelocity,offset)-next->current;
        const double speed=std::hypot(velocity.x,velocity.z),heave=std::abs(velocity.y);if(speed<.12&&heave<.1)continue;
        OceanWake w;w.x=centre.x;w.z=centre.z;w.birth=time;w.width=std::clamp(std::min(size.x,size.z)*.6,.6,5.);w.k=1.8/w.width;w.speed=std::sqrt(9.80665/w.k)*.65;w.amplitude=std::clamp(.012*speed+.01*heave,0.,std::min(.06,(next->seaLevel-next->bottom)*.01))*std::clamp(1.-std::abs(depth)/(verticalRadius+.1),0.,1.);
        if(speed>.12){w.dirX=velocity.x/speed;w.dirZ=velocity.z/speed;w.bodySpeed=speed;double along=0;for(int a=0;a<3;++a)along+=std::abs(w.dirX*pose[a][0]+w.dirZ*pose[a][2])*size[a]*.5;w.x-=w.dirX*along*.85;w.z-=w.dirZ*along*.85;}
        if(w.amplitude>.0005&&next->contains(w.x,w.z))next->wakes.push_back(w);
        // Deposit side aeration and stern foam at the same fixed-time cadence.
        // Bounds approximate the waterline silhouette; they are not CFD hull flow.
        if(speed>.15||heave>.15){const auto centreShape=pose*((bounds.min+bounds.max)*.5);const bool longZ=size.z>=size.x;
            auto axis=pose*vsg::dvec4(longZ?0.:1.,0.,longZ?1.:0.,0.);const double axisLength=std::hypot(axis.x,axis.z);
            if(axisLength>.01){const double dx=axis.x/axisLength,dz=axis.z/axisLength,along=std::max(size.x,size.z)*.48,across=std::min(size.x,size.z)*.48;
                uint32_t seed=2166136261u;for(unsigned char c:id)seed=(seed^c)*16777619u;
                for(int side:{-1,1}){OceanFoam f;f.x=centreShape.x-dz*across*side;f.z=centreShape.z+dx*across*side;f.birth=time;f.length=std::clamp(along,.1,1000.);f.width=std::clamp(.2+speed*.12+heave*.3,.15,2.);f.dirX=dx;f.dirZ=dz;f.strength=std::clamp(speed*.045+heave*.12,0.,.45);f.seed=seed^uint32_t(scene.tick+side);if(next->contains(f.x,f.z))next->foam.push_back(f);}
                OceanFoam f;f.x=w.x;f.z=w.z;f.birth=time;f.length=std::max(.3,speed*.4);f.width=std::clamp(across*.5,.2,12.);f.dirX=w.dirX;f.dirZ=w.dirZ;f.strength=std::clamp(speed*.15+heave*.1,0.,.85);f.lifetime=30.;f.seed=seed^uint32_t(scene.tick);if(next->contains(f.x,f.z))next->foam.push_back(f);
            }
        }
    }
    if(next->wakes.size()>maximumWakes)next->wakes.erase(next->wakes.begin(),next->wakes.end()-maximumWakes);
    if(next->foam.size()>maximumFoam)next->foam.erase(next->foam.begin(),next->foam.end()-maximumFoam);
    next->refreshBounds();scene.ocean=std::move(next);
}
nlohmann::json VoxelOcean::encode()const{
    auto v=[](vsg::dvec3 a){return nlohmann::json::array({a.x,a.y,a.z});};nlohmann::json w=nlohmann::json::array();for(const auto& h:harmonics)w.push_back({{"amplitude_m",h.amplitude},{"k",h.k},{"dx",h.dx},{"dz",h.dz},{"omega",h.omega},{"phase",h.phase}});
    nlohmann::json packets=nlohmann::json::array();for(const auto& q:wakes)packets.push_back({{"x",q.x},{"z",q.z},{"birth_s",q.birth},{"amplitude_m",q.amplitude},{"width_m",q.width},{"speed_mps",q.speed},{"lifetime_s",q.lifetime},{"k",q.k},{"direction_x",q.dirX},{"direction_z",q.dirZ},{"body_speed_mps",q.bodySpeed}});
    nlohmann::json spray=nlohmann::json::array();
    for(const auto& p:splashes){
        spray.push_back({{"x",p.x},{"z",p.z},{"birth_s",p.birth},{"height_m",p.height},
            {"speed_mps",p.speed},{"radius_m",p.radius},{"lifetime_s",p.lifetime},{"seed",p.seed}});
        if(vsg::length(p.drift)>0.)spray.back()["drift_mps"]={p.drift.x,p.drift.y,p.drift.z};
    }
    nlohmann::json deposits=nlohmann::json::array();for(const auto& p:foam)deposits.push_back({{"x",p.x},{"z",p.z},{"birth_s",p.birth},{"length_m",p.length},{"width_m",p.width},{"direction_x",p.dirX},{"direction_z",p.dirZ},{"strength",p.strength},{"lifetime_s",p.lifetime},{"seed",p.seed}});
    nlohmann::json result={{"foam",deposits},{"splashes",spray},{"sea_fog",{{"visibility_m",fogVisibility},{"height_m",fogHeight}}},{"wakes",packets},{"enabled",true},{"sea_level_m",seaLevel},{"bottom_m",bottom},{"half_extent_m",halfExtent},{"cell_size_m",cellSize},{"density_kg_m3",density},{"drag_coefficient",drag},{"linear_drag_mps",linearDrag},{"temperature_k",temperature},{"wind_speed_mps",windSpeed},{"wind_azimuth_deg",windAzimuth},{"wave_height_m",waveHeight},{"wave_length_m",waveLength},{"swell_height_m",swellHeight},{"swell_length_m",swellLength},{"current_mps",v(current)},{"absorption_rgb_per_m",v(absorption)},{"scattering_rgb_per_m",v(scattering)},{"harmonics",w},{"provenance",provenance}};if(referenceSize)result["reference_surface"]={{"grid_size",referenceSize},{"min_x",referenceX},{"min_z",referenceZ},{"step_x",referenceStepX},{"step_z",referenceStepZ},{"heights_m",referenceHeights}};return result;
}
bool VoxelOcean::contains(double x,double z)const{return std::abs(x)<=halfExtent&&std::abs(z)<=halfExtent;}
OceanSample VoxelOcean::sample(double x,double z,double t,double y)const{
    if(!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(t)||!std::isfinite(y))throw std::invalid_argument("nonfinite ocean sample");
    const auto mean=meanSurface(x,z);OceanSample s;s.height=mean.x;s.velocity=current;double sx=mean.y,sz=mean.z;
    for(const auto& h:harmonics){const double phase=h.k*(h.dx*x+h.dz*z)-h.omega*t+h.phase,c=std::cos(phase),sn=std::sin(phase);s.height+=h.amplitude*c;sx-=h.k*h.amplitude*h.dx*sn;sz-=h.k*h.amplitude*h.dz*sn;
        const double waterDepth=mean.x-bottom,depth=std::clamp(mean.x-std::min(y,mean.x),0.,waterDepth),a=std::exp(-h.k*depth),b=std::exp(-h.k*(2*waterDepth-depth));
        const double denominator=referenceSize?1./(-std::expm1(-2*h.k*waterDepth)):h.inverseDepthDenominator,horizontal=(a+b)*denominator,vertical=(a-b)*denominator,aw=h.amplitude*h.omega;
        s.velocity+=vsg::dvec3(h.dx*horizontal*c,vertical*sn,h.dz*horizontal*c)*aw;
        s.velocityDx+=vsg::dvec3(-h.dx*horizontal*sn,vertical*c,-h.dz*horizontal*sn)*(aw*h.k*h.dx);
        s.velocityDz+=vsg::dvec3(-h.dx*horizontal*sn,vertical*c,-h.dz*horizontal*sn)*(aw*h.k*h.dz);
        if(y<mean.x&&y>bottom)s.velocityDy+=vsg::dvec3(h.dx*vertical*c,horizontal*sn,h.dz*vertical*c)*(aw*h.k);
    }
    for(const auto& w:wakes){const auto q=wakeField(w,current,x,z,t,true);s.height+=q.height;sx+=q.gradient.x;sz+=q.gradient.z;const double attenuation=std::exp(w.k*std::min(0.,y-mean.x));s.velocity.y+=q.vertical*attenuation;s.velocityDx.y+=q.verticalGradient.x*attenuation;s.velocityDz.y+=q.verticalGradient.z*attenuation;if(y<mean.x)s.velocityDy.y+=w.k*q.vertical*attenuation;}
    s.normal=vsg::normalize(vsg::dvec3(-sx,1,-sz));return s;
}
double VoxelOcean::densityAt(const vsg::dvec3& p,double t)const{return contains(p.x,p.z)&&p.y>=bottom&&p.y<=surface(p.x,p.z,t).height?density:0.;}
OceanVoxelState VoxelOcean::voxel(int64_t x,int64_t y,int64_t z,double t)const{
    OceanVoxelState out;out.temperature=temperature;out.absorption=absorption;out.scattering=scattering;
    const vsg::dvec3 low(std::max(-halfExtent,x*cellSize),bottom+y*cellSize,std::max(-halfExtent,z*cellSize)),high(std::min(halfExtent,(x+1.)*cellSize),bottom+(y+1.)*cellSize,std::min(halfExtent,(z+1.)*cellSize));
    if(high.x<=low.x||high.z<=low.z||y<0)return out;const auto p=(low+high)*.5;const auto s=sample(p.x,p.z,t,p.y);out.velocity=s.velocity;
    out.waterFraction=clipBuoyancyBox(high-low,vsg::translate(low),s.normal,vsg::dot(s.normal,vsg::dvec3(p.x,s.height,p.z))).fraction*(high.x-low.x)*(high.z-low.z)/(cellSize*cellSize);out.density=density*out.waterFraction;return out;
}
vsg::dvec3 VoxelOcean::transmission(double length)const{if(!std::isfinite(length)||length<0)throw std::invalid_argument("invalid underwater optical path");vsg::dvec3 out;for(int i=0;i<3;++i)out[i]=std::exp(-(absorption[i]+scattering[i])*length);return out;}
bool VoxelOcean::intersectSurface(const vsg::dvec3& p,const vsg::dvec3& direction,double t,double limit,double& distance)const{
    if(!std::isfinite(vsg::length(p))||!std::isfinite(vsg::length(direction))||vsg::length(direction)<1e-12||!std::isfinite(t)||!std::isfinite(limit)||limit<0)throw std::invalid_argument("invalid ocean ray");
    const auto d=vsg::normalize(direction);double first=0,last=limit;
    const vsg::dvec3 low(-halfExtent,seaLevel+referenceMin-amplitudeBound-1e-6,-halfExtent),high(halfExtent,seaLevel+referenceMax+amplitudeBound+1e-6,halfExtent);
    for(int i=0;i<3;++i){if(std::abs(d[i])<1e-14){if(p[i]<low[i]||p[i]>high[i])return false;}else{double a=(low[i]-p[i])/d[i],b=(high[i]-p[i])/d[i];if(a>b)std::swap(a,b);first=std::max(first,a);last=std::min(last,b);}}if(first>last)return false;
    auto f=[&](double r){const auto q=p+d*r;return q.y-surface(q.x,q.z,t).height;};
    double a=first,fa=f(a);const double step=(harmonics.empty()&&wakes.empty()&&!referenceSize)?last-first+1:std::min(2.,minimumWavelength/16.);
    for(int iteration=0;iteration<32768&&a<=last;++iteration){if(std::abs(fa)<1e-7){distance=a;return true;}const double b=std::min(last,a+step),fb=f(b);if(fa*fb<=0){double lo=a,hi=b;for(int k=0;k<32;++k){const double m=(lo+hi)*.5;if(fa*f(m)<=0)hi=m;else lo=m;}distance=(lo+hi)*.5;return true;}if(b==last)return false;a=b;fa=fb;}throw std::runtime_error("ocean surface ray exceeded its bounded query budget");
}
}
