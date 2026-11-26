#include "AviationParticles.h"
#include "FlightTrailCurve.h"
#include "ViewportKit.h"

#include <vsg/all.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace vsgraysim::viewport3d {
namespace {
using V=vsg::vec3;
using Q=vsg::vec4;
constexpr unsigned capacity=1152, maxTracks=64, maxHistory=960;
float unit(unsigned x) {
    x=(x^(x>>16))*0x7feb352du;x=(x^(x>>15))*0x846ca68bu;x^=x>>16;
    return float(x&0xffff)/65535.f;
}
bool finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool exhaustSmoke(float kind){return kind==2 || kind==8 || kind==9;}
float windTransfer(float kind){return exhaustSmoke(kind)?.84f:kind==6?.74f:.57f;}
struct Item { Q pos,axis,shape; float depth; };
// Small, reusable 3D turbulence grid. Periodic knots avoid seams when
// adjacent puffs share chart-space texture coordinates.
vsg::ref_ptr<vsg::Data> turbulenceTexture() {
    constexpr unsigned side=48;
    auto image=vsg::ubyteArray3D::create(side,side,side);
    image->properties.format=VK_FORMAT_R8_UNORM;
    auto octave=[](unsigned x,unsigned y,unsigned z,unsigned period) {
        const auto cell=[&](unsigned k) {
            const float q=(float(k)+.5f)*float(period)/float(side);
            return std::pair<unsigned,float>{unsigned(q),q-std::floor(q)};
        };
        const auto [i,fx]=cell(x);const auto [j,fy]=cell(y);const auto [k,fz]=cell(z);
        const auto fade=[](float t){return t*t*(3.f-2.f*t);};
        const auto sample=[&](unsigned dx,unsigned dy,unsigned dz){
            return unit((i+dx)%period+((j+dy)%period)*131u+((k+dz)%period)*18013u+97u*period);
        };
        float result=0;
        const float a=fade(fx),b=fade(fy),c=fade(fz);
        for(unsigned iz=0;iz<2;++iz)for(unsigned iy=0;iy<2;++iy)for(unsigned ix=0;ix<2;++ix)
            result+=sample(ix,iy,iz)*(ix?a:1-a)*(iy?b:1-b)*(iz?c:1-c);
        return result;
    };
    for(unsigned z=0;z<side;++z)for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
        const float n=.66f*octave(x,y,z,5)+.24f*octave(x,y,z,11)+.10f*octave(x,y,z,20);
        image->set(x,y,z,uint8_t(std::lround(n*255.f)));
    }
    return image;
}
}
AviationParticles::AviationParticles()=default;
AviationParticles::~AviationParticles()=default;
vsg::Node* AviationParticles::node() const{return root_.get();}
unsigned AviationParticles::visibleCount() const{return draw_?draw_->instanceCount:0;}

bool AviationParticles::initialize() {
    if(root_)return true;
    const auto path=gui::ViewportKit::resolveAssetPath("shaders/viewport");
    vsg::ShaderStages stages{
        vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT,"main",path+"/aviation_particles.vert"),
        vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT,"main",path+"/aviation_particles.frag")};
    auto compiler=vsg::ShaderCompiler::create();
    if(!stages[0]||!stages[1]||!compiler||!compiler->supported()||!compiler->compile(stages))return false;
    auto shader=vsg::ShaderSet::create(stages);
    shader->addPushConstantRange("pc","",VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,128);
    shader->addDescriptorBinding("particles","",0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,{});
    shader->addDescriptorBinding("light","",0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shader->addDescriptorBinding("turbulence","",0,2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    particles_=vsg::vec4Array::create(3*capacity,Q{});
    lighting_=vsg::vec4Array::create(4,Q{});
    particles_->properties.dataVariance=lighting_->properties.dataVariance=vsg::DYNAMIC_DATA;
    auto cfg=vsg::GraphicsPipelineConfigurator::create(shader);
    cfg->assignDescriptor("particles",particles_);
    cfg->assignDescriptor("light",lighting_);
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=sampler->addressModeV=sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->minFilter=sampler->magFilter=VK_FILTER_LINEAR;
    cfg->assignTexture("turbulence",turbulenceTexture(),sampler);
    for(auto& state:cfg->pipelineStates) {
        if(auto d=state.cast<vsg::DepthStencilState>()) {d->depthWriteEnable=VK_FALSE;d->depthCompareOp=VK_COMPARE_OP_GREATER_OR_EQUAL;}
        if(auto r=state.cast<vsg::RasterizationState>())r->cullMode=VK_CULL_MODE_NONE;
        if(auto b=state.cast<vsg::ColorBlendState>())for(auto& a:b->attachments) {
            a.blendEnable=VK_TRUE;a.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;a.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            a.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;a.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
    }
    shader->variants.emplace(cfg->shaderHints,shader->stages);
    cfg->init();
    auto group=vsg::StateGroup::create();cfg->copyTo(group);
    draw_=vsg::Draw::create(6,0,0,0);group->addChild(draw_);
    // Blend after the opaque globe, sky and aircraft have recorded, without
    // writing depth. A default-order transparent draw was overwritten by sky.
    auto layer=vsg::Layer::create();
    layer->binNumber=2;layer->child=group;root_=layer;
    return true;
}
void AviationParticles::clear() {
    tracks_.clear();smoke_.clear();pending_.clear();last_=-1;seed_=1;visibleJetFlames_=0;
    if(draw_)draw_->instanceCount=0;
}
void AviationParticles::burst(V pos,Burst kind,V direction,float scale) {
    if(finite(pos)&&finite(direction)&&std::isfinite(scale)&&pending_.size()<32)
        pending_.push_back({pos,kind,direction,std::clamp(scale,.1f,4.f)});
}
void AviationParticles::update(double now,const std::vector<Source>& sources,V wind,V eye,V sunDir,V sky,V sun,V haze,float visibility) {
    if(!draw_||!std::isfinite(now))return;
    if(last_>=0&&now<last_)clear();
    const double dt=last_<0?0:now-last_;
    last_=now;
    smoke_.erase(std::remove_if(smoke_.begin(),smoke_.end(),[&](const Puff& p){return now-p.born>=p.life;}),smoke_.end());
    auto addPuff=[&](V p,V velocity,double t,float life,float radius,float kind,float opacity=1.f,float spread=1.f) {
        if(smoke_.size()>=maxHistory)smoke_.pop_front();
        smoke_.push_back({p,velocity,V{},t,life,radius,kind,unit(++seed_),opacity,spread});
    };
    for(const auto& b:pending_) {
        if(b.kind==Burst::StageFlash) {
            const auto direction=vsg::length(b.direction)>.01f?vsg::normalize(b.direction):V(0,1,0);
            const auto lead=direction*b.scale;
            addPuff(b.origin+lead*.22f,lead*2.2f,now,.13f,b.scale*.70f,5);
            addPuff(b.origin+lead*.64f,lead*4.2f,now,.17f,b.scale*.47f,4);
            for(unsigned i=0;i<5;++i) {
                const auto s=++seed_;
                const V swell(unit(s*3)-.5f,.25f+unit(s*17)*.66f,unit(s*13)-.5f);
                addPuff(b.origin+lead*(.40f+.43f*i),lead*(.54f+unit(s*7)*.55f)+swell,
                    now+.055+.022*i,.85f+.54f*unit(s*11),b.scale*(.25f+unit(s*21)*.32f),2,.46f);
            }
            continue;
        }
        const bool fracture=b.kind==Burst::Fracture,large=b.kind==Burst::Impact;
        addPuff(b.origin,{},now,fracture?.13f:.22f,fracture?2.3f:large?6.f:2.f,5);
        const unsigned n=fracture?4:large?16:5;
        for(unsigned j=0;j<n;++j) {
            const unsigned s=++seed_;
            const float a=unit(s*3)*6.2831853f,y=.34f+unit(s*11)*.82f;
            const V direction{std::cos(a),y,std::sin(a)};
            if(fracture) {
                addPuff(b.origin,direction*(1.7f+1.8f*unit(s*7)),now,
                        .21f+.32f*unit(s*9),.60f+.61f*unit(s*5),4);
            } else if(large&&j<12) {
                const V bias{.75f,0,-.32f};
                addPuff(b.origin,direction*(5.f+12.f*unit(s*7))+bias*(3.f+2.f*unit(s*21)),now,
                        .45f+unit(s*9)*.66f,1.25f+unit(s*5)*2.3f,4);
            }
            if(!fracture)addPuff(b.origin,direction*(1.2f+2.8f*unit(s*19)),now,
                                 (large?10.f:6.f)+unit(s*17)*7.f,1.35f+unit(s*23)*1.9f,large?3:2);
        }
    }
    pending_.clear();
    if(dt>0)for(auto& p:smoke_)if(p.kind<4||p.kind==6||exhaustSmoke(p.kind)) {
        const float move=float(std::min(dt,std::max(0.,now-p.born)));
        p.drift+=wind*(move*windTransfer(p.kind));
    }

    std::vector<Item> batch;
    batch.reserve(std::min(size_t(capacity),smoke_.size()+maxTracks));
    visibleJetFlames_=0;
    const auto pack=[&](V p,V axis,float width,float length,Q shape) {
        const V d=p-eye;const float sq=vsg::dot(d,d);
        if(batch.size()>=capacity||!std::isfinite(sq)||sq>14000.f*14000.f)return;
        batch.push_back({Q(p,width),Q(axis,length),shape,sq});
        if(shape.x==0.f)++visibleJetFlames_;
    };
    std::set<std::string> live;
    for(const auto& source:sources) {
        if(live.size()>=maxTracks)break;
        if(!finite(source.nozzle)||!finite(source.forward)||!std::isfinite(source.radius)||!std::isfinite(source.strength)||
           vsg::length(source.forward)<.01f||source.strength<=.01f||!live.insert(source.id).second)continue;
        const auto back=-vsg::normalize(source.forward);
        const auto nozzle=source.nozzle;
        const float amount=std::clamp(source.strength,0.f,1.f), r=std::clamp(source.radius,.1f,4.f);
        const bool jet=source.style==Style::Jet;
        const bool launch=source.style==Style::LaunchExhaust;
        const bool condensation=source.style==Style::Condensation;
        if(source.style==Style::Soot||source.style==Style::BurningDebris) {
            const bool burning=source.style==Style::BurningDebris;
            auto [it,first]=tracks_.try_emplace(source.id,Track{nozzle,now,0});
            auto& tr=it->second;
            const double step=.30-.16*amount;
            const double elapsed=now-tr.time;
            const float travel=vsg::length(nozzle-tr.nozzle);
            const float spacing=std::clamp(r*.88f,.48f,1.05f);
            // Match smoke spacing to the moving socket. A fixed emission
            // cadence leaves visible gaps as a piece accelerates downward.
            if(first||elapsed>=step||(burning&&elapsed>0.&&travel>=spacing)) {
                const auto velocity=finite(source.velocity)?source.velocity:V{};
                const auto carry=velocity*(.10f/std::max(1.f,vsg::length(velocity)/22.f));
                const auto sootVelocity=carry+V(0,(burning?.56f:1.4f)+amount,0);
                const float lifetime=burning?4.4f+2.2f*amount:7.f+3.f*amount;
                const float sootRadius=r*(burning?.48f:1.f);
                const float kind=burning?6.f:3.f;
                if(!first&&elapsed<.65&&travel<75.f) {
                    const int timed=std::clamp(int(elapsed/step),1,4);
                    const int n=burning?std::clamp(int(std::ceil(travel/spacing)),timed,9):timed;
                    for(int j=1;j<=n;++j) {
                        const float k=float(j)/n;
                        const double born=tr.time+elapsed*k;
                        addPuff(tr.nozzle+(nozzle-tr.nozzle)*k,sootVelocity,born,lifetime,sootRadius,kind);
                        smoke_.back().drift=wind*(float(now-born)*(burning?.74f:.57f));
                    }
                } else addPuff(nozzle,sootVelocity,now,lifetime,sootRadius,kind);
                tr={nozzle,now,0};
            }
            if(burning) {
                const auto air=source.velocity-wind;
                const float speed=finite(air)?std::min(36.f,vsg::length(air)):0.f;
                const auto axis=speed>.05f?vsg::normalize(V(0,8.f,0)-air*.30f):V(0,1,0);
                const float pulse=.87f+.13f*std::sin(float(now*17.)+unit(unsigned(std::hash<std::string>{}(source.id)))*6.28f);
                const float extent=r*(.84f+.023f*speed);
                pack(nozzle+axis*(extent*.53f),axis,r*.60f*pulse,extent,{4,amount*pulse,.12f,float(std::fmod(now*.67,1.))});
            }
            continue;
        }
        const float pulse=float(std::fmod(now*19.,6.2831853));
        const float phase=pulse+unit(unsigned(std::hash<std::string>{}(source.id)));
        if(!condensation) {
            const float lengthFactor=jet?2.65f+4.15f*amount:launch?3.6f+8.2f*amount:2.9f+5.6f*amount;
            const float size=r*lengthFactor;
            const float width=r*(launch?1.8f:1.15f);
            pack(nozzle+back*(size*.92f),back,width,size,{jet?0.f:launch?7.f:1.f,
                amount*(.65f+.32f*amount),0,phase});
        }
        const float opacity=std::clamp(source.exhaust.smokeOpacity.value_or(jet?0.f:1.f),0.f,1.f);
        if(opacity<=.01f)continue;
        const float lifetime=std::clamp(source.exhaust.lifetimeScale,.15f,3.f);
        const float spread=std::clamp(source.exhaust.spreadScale,.15f,3.f);
        const float smokeKind=launch?8.f:condensation?9.f:2.f;
        const float smokeAlpha=(condensation?.34f*amount:.33f+.67f*amount)*opacity;
        // Start the opaque part of each wake behind the luminous near field.
        // The offset uses nozzle scale, so it stays fixed through throttle changes.
        const V wake=nozzle+back*(r*(launch?15.8f:condensation?0.f:8.0f));
        const V forward=-back;
        auto [it,newTrack]=tracks_.try_emplace(source.id,Track{wake,now,0,forward});
        auto& tr=it->second;
        if(newTrack)addPuff(wake,back*1.2f,now,12.f*lifetime,r*1.20f,smokeKind,smokeAlpha,spread);
        const V displacement=wake-tr.nozzle;
        const float delta=vsg::length(displacement);
        const double elapsed=now-tr.time;
        if(elapsed>0.&&delta>.005f) {
            if(elapsed<=1.5&&delta<1800.f) {
                // Estimate arc length between observed nozzles. This leaves
                // smoke on the bend instead of drawing a chord across it.
                const V chord=displacement/delta;
                const auto endpointVelocity=[&](V direction) {
                    const V tangent=vsg::dot(direction,chord)>.15f?direction:chord;
                    return vsg::dvec3(tangent)*(delta/elapsed);
                };
                const FlightTrailCurve curve(vsg::dvec3(tr.nozzle),vsg::dvec3(wake),
                    endpointVelocity(tr.forward),endpointVelocity(forward),elapsed);
                std::array<float,9> lengths{};
                auto previous=vsg::dvec3(tr.nozzle);
                const double intervals=double(lengths.size()-1);
                for(size_t k=1;k<lengths.size();++k) {
                    const auto p=curve.at(double(k)/intervals);
                    lengths[k]=lengths[k-1]+float(vsg::length(p-previous));
                    previous=p;
                }
                const float spacing=std::max(r*2.2f,launch||condensation?1.40f:.55f);
                const float length=lengths.back();
                const int steps=std::clamp(int((length+tr.remainder)/spacing),0,48);
                size_t edge=1;
                if(steps>0)for(int j=0;j<steps;++j) {
                    const float distance=std::min(length,steps==48?length*float(j+1)/steps:spacing*(j+1)-tr.remainder);
                    while(edge+1<lengths.size()&&distance>lengths[edge])++edge;
                    const float offset=(distance-lengths[edge-1])/std::max(.00001f,lengths[edge]-lengths[edge-1]);
                    const double t=(double(edge-1)+offset)/intervals;
                    const double born=tr.time+elapsed*t;
                    const auto direction=curve.tangent(t);
                    const V tangent=vsg::length(direction)>.001?vsg::vec3(vsg::normalize(direction)):chord;
                    // The new plume starts narrow and expands continuously after emission.
                    const float scale=launch?1.22f:condensation?.94f:1.52f;
                    addPuff(vsg::vec3(curve.at(t)),-tangent*(1.f+2.f*unit(seed_)),born,(12.f+6.f*unit(seed_+33))*lifetime,
                         r*(scale+.34f*unit(seed_+3))*(steps==48?1.30f:1.f),smokeKind,smokeAlpha,spread);
                    smoke_.back().drift=wind*(float(now-born)*windTransfer(smokeKind));
                }
                tr.remainder=steps==48?0.f:std::fmod(length+tr.remainder,spacing);
            } else {
                tr.remainder=0.f;
                addPuff(wake,back*1.2f,now,12.f*lifetime,r*1.20f,smokeKind,smokeAlpha,spread);
            }
            tr.nozzle=wake;tr.time=now;tr.forward=forward;
        }
    }
    for(auto it=tracks_.begin();it!=tracks_.end();)
        if(!live.count(it->first))it=tracks_.erase(it);else ++it;
    for(const auto& p:smoke_) {
        if(now<p.born)continue;
        const float age=float(std::max(0.,now-p.born)), t=age/p.life;
        const bool debris=p.kind==6,exhaust=exhaustSmoke(p.kind),gas=p.kind<4||debris||exhaust;
        const float expansion=p.kind==8?1.72f:p.kind==9?.28f:debris?.47f:exhaust?.80f:1.16f;
        const float radius=gas?p.radius+p.spread*(expansion*age+(debris?.24f:.36f)*std::sqrt(age)):
                                  p.radius*(.42f+2.0f*std::sqrt(std::min(1.f,t)));
        const float fade=std::pow(std::max(0.f,1.f-t),gas?1.7f:1.5f);
        const float eddyAge=std::max(0.f,age-.18f);
        const float eddy=exhaust?(.33f+.13f*std::sqrt(eddyAge))*eddyAge:.30f*age;
        const V lift{eddy*std::sin(p.seed*43.f+age*.7f),
                     gas?(exhaust?.29f:1.05f)*age+eddy*.61f*std::cos(p.seed*71.f):0.f,
                     eddy*std::cos(p.seed*62.f+age*.62f)};
        const float move=gas?std::min(age,3.0f):age;
        const float speed=vsg::length(p.velocity);
        const bool trail=exhaust&&speed>.01f;
        const V axis=(trail||p.kind==4)&&speed>.01f?p.velocity/speed:V(0,1,0);
        const float stretch=trail?(p.kind==8?2.08f-.46f*t:1.58f)+.30f*std::sin(p.seed*38.f):
            p.kind==4?1.24f:debris?1.36f:p.kind==3?1.13f:1.f;
        const float cooling=exhaust&&p.kind!=9 ? .65f+.35f*std::min(1.f,age*2.f) : 1.f;
        pack(p.origin+p.velocity*move+p.drift+lift,axis,radius,radius*stretch,
             {p.kind,fade*p.opacity*cooling*(trail?.47f:p.kind==3?.63f:debris?.36f:1.f),t,p.seed});
    }
    std::stable_sort(batch.begin(),batch.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});
    draw_->instanceCount=uint32_t(batch.size());
    if(batch.empty())return;
    for(size_t i=0;i<batch.size();++i) {
        (*particles_)[i*3]=batch[i].pos;(*particles_)[i*3+1]=batch[i].axis;(*particles_)[i*3+2]=batch[i].shape;
    }
    particles_->dirty();
    (*lighting_)[0]=Q(sky,0);(*lighting_)[1]=Q(sun,0);(*lighting_)[2]=Q(sunDir,0);(*lighting_)[3]=Q(haze,visibility);
    lighting_->dirty();
}
}
