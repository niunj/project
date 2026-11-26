#include "CockpitSpatial.h"
#include "ViewportKit.h"
#include <vsg/all.h>
#include <limits>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace vsgraysim::viewport3d {
namespace {
using P=vsg::vec3;
using C=vsg::vec4;
constexpr float pi=3.141592653589793f;
constexpr std::array<float,4> panelSeams{0,.155f,.845f,1};
struct Vertex { P p,n; vsg::vec2 uv; C color; };
struct CabinMesh {
    std::vector<Vertex> vertices;
    void quad(P a,P b,P c,P d,C color, vsg::vec2 ab={},vsg::vec2 bb={},vsg::vec2 cb={},vsg::vec2 db={}) {
        const auto normal=vsg::normalize(vsg::cross(b-a,d-a));
        vertices.insert(vertices.end(),{{a,normal,ab,color},{b,normal,bb,color},{c,normal,cb,color},
                                        {a,normal,ab,color},{c,normal,cb,color},{d,normal,db,color}});
    }
    void beam(P a,P b,float across,float thick,C color,int sides=12,float tipScale=1.f,bool cap=false) {
        const auto axis=vsg::normalize(b-a);
        const auto u=vsg::normalize(vsg::cross(axis,std::abs(axis.y)<.88f?P{0,1,0}:P{1,0,0}));
        const auto v=vsg::cross(axis,u);
        const auto p=[&](int i){
            const auto angle=(i+.5f)*2*pi/sides;
            return u*(across*std::cos(angle))+v*(thick*std::sin(angle));
        };
        for(int i=0;i<sides;++i) {
            quad(a+p(i),a+p(i+1),b+p(i+1)*tipScale,b+p(i)*tipScale,color);
            if(cap) {
                const auto addFace=[&](P pos,P e,P f,P n) {
                    vertices.insert(vertices.end(),{{pos,n,{},color},{e,n,{},color},{f,n,{},color}});
                };
                addFace(a,a+p(i+1),a+p(i),-axis);addFace(b,b+p(i)*tipScale,b+p(i+1)*tipScale,axis);
            }
        }
    }
    // Beveled housings, key caps and contacts. Each perimeter is a real
    // face, so the edges respond to view and sun without a baked highlight.
    void bevel(P c,P r,float e,C color) {
        e=std::min({e,r.x*.72f,r.y*.72f,r.z*.82f});
        const auto ring=[&](int level,int i) {
            const float x=r.x-((level==0||level==3)?e:0);
            const float y=r.y-((level==0||level==3)?e:0);
            const float k=std::min(e,x*.42f);
            const std::array<P,8> outline{{{-x+k,-y,0},{x-k,-y,0},{x,-y+k,0},{x,y-k,0},
                                         {x-k,y,0},{-x+k,y,0},{-x,y-k,0},{-x,-y+k,0}}};
            const float z=std::array<float,4>{-r.z,-r.z+e,r.z-e,r.z}[level];
            return c+outline[i%8]+P(0,0,z);
        };
        for(int j=0;j<3;++j)for(int i=0;i<8;++i)quad(ring(j,i),ring(j,i+1),ring(j+1,i+1),ring(j+1,i),color);
        for(int i=1;i<7;++i)for(int j:{0,3}) {
            const P n(0,0,j==0?-1.f:1.f);
            vertices.insert(vertices.end(),{{ring(j,0),n,{},color},{ring(j,i),n,{},color},{ring(j,i+1),n,{},color}});
        }
    }
    void box(P p,P r,C color) {
        const P a=p+P(-r.x,-r.y,-r.z), b=p+P(r.x,-r.y,-r.z), c=p+P(r.x,r.y,-r.z), d=p+P(-r.x,r.y,-r.z);
        const P z(0,0,2*r.z);
        quad(a,b,c,d,color);quad(b+z,a+z,d+z,c+z,color);
        quad(a,d,d+z,a+z,color);quad(b,b+z,c+z,c,color);
        quad(d,c,c+z,d+z,color);quad(a+z,b+z,b,a,color);
    }
};

vsg::ref_ptr<vsg::StateGroup> surface(const vsg::ShaderStages& stages, const vsg::ref_ptr<vsg::Data>& tex,
                                      const vsg::ref_ptr<vsg::vec4Array>& light,bool glass) {
    auto vertex=vsg::VertexInputState::create();
    vertex->vertexBindingDescriptions={{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}};
    vertex->vertexAttributeDescriptions={{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,p)},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,n)}, {2,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,uv)},
        {3,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,color)}};
    auto raster=vsg::RasterizationState::create();raster->cullMode=VK_CULL_MODE_NONE;
    auto blend=vsg::ColorBlendState::create();
    if(glass) {
        auto& b=blend->attachments.front();
        b.blendEnable=VK_TRUE;b.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
        b.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    auto depth=vsg::DepthStencilState::create();depth->depthWriteEnable=glass?VK_FALSE:VK_TRUE;
    auto dsl=vsg::DescriptorSetLayout::create();
    dsl->addBinding(0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT);
    dsl->addBinding(1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT);
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT,0,128}});
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=sampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler->maxLod=8;
    auto state=vsg::StateGroup::create();
    state->add(vsg::BindGraphicsPipeline::create(vsg::GraphicsPipeline::create(layout,stages,
        vsg::GraphicsPipelineStates{vertex,vsg::InputAssemblyState::create(),raster,blend,depth})));
    state->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,
        vsg::Descriptors{vsg::DescriptorImage::create(sampler,tex,0),vsg::DescriptorBuffer::create(light,1)}));
    return state;
}
void add(vsg::Group& group,const CabinMesh& mesh) {
    if(mesh.vertices.empty())return;
    auto data=vsg::ubyteArray::create(mesh.vertices.size()*sizeof(Vertex));
    std::memcpy(data->data(),mesh.vertices.data(),data->size());
    auto geom=vsg::Geometry::create();geom->assignArrays({data});
    geom->commands.push_back(vsg::Draw::create(uint32_t(mesh.vertices.size()),1,0,0));
    group.addChild(geom);
}
}

vsg::vec3 cockpitPanelPoint(float u,float v) {
    const float x=(u-.5f)*1.53f;
    const float wrap=std::max(0.f,std::abs(u-.5f)-.345f)/.155f;
    return {x,-.127f-v*.62f-wrap*.045f,-1.16f+v*.19f+wrap*.19f};
}

const std::array<CabinControl,12>& fighterCabinControls() {
    static const auto controls=[] {
        std::array<CabinControl,12> a{};
        constexpr std::array<CabinAction,6> actions{CabinAction::LeftFlight,CabinAction::LeftControl,CabinAction::LeftEngine,
                                                   CabinAction::RightFlight,CabinAction::RightControl,CabinAction::RightEngine};
        constexpr std::array<const char*,6> labels{"LEFT MFD / FLT","LEFT MFD / CTRL","LEFT MFD / ENG","RIGHT MFD / FLT","RIGHT MFD / CTRL","RIGHT MFD / ENG"};
        for(size_t i=0;i<6;++i) {
            const float u=(i<3?.215f:.665f)+float(i%3)*.030f;
            a[i]={actions[i],cockpitPanelPoint(u,.481f)+P(0,0,.018f),{0,.292f,.956f},.019f,labels[i]};
        }
        a[6]={CabinAction::PanelLight, cockpitPanelPoint(.291f,.608f)+P(0,0,.034f),{0,.292f,.956f},.023f,"INST LIGHT / DIM"};
        a[7]={CabinAction::Hud, cockpitPanelPoint(.329f,.609f)+P(0,0,.033f),{0,.292f,.956f},.022f,"HUD / ON - OFF"};
        a[8]={CabinAction::FlightHold,cockpitPanelPoint(.702f,.609f)+P(0,0,.034f),{0,.292f,.956f},.023f,"ALT / HDG HOLD"};
        a[9]={CabinAction::LandingGear,cockpitPanelPoint(.171f,.560f)+P(0,0,.086f),{0,.292f,.956f},.033f,"LDG GEAR / UP - DOWN"};
        a[10]={CabinAction::NavLights,cockpitPanelPoint(.163f,.391f)+P(0,0,.045f),{0,.292f,.956f},.019f,"POSITION LIGHTS / ON - OFF"};
        a[11]={CabinAction::LandingLight,cockpitPanelPoint(.171f,.467f)+P(0,0,.045f),{0,.292f,.956f},.019f,"LANDING LIGHT / ON - OFF"};
        return a;
    }();
    return controls;
}

CabinAction SpatialCockpit::pick(P ray) const {
    float best=std::numeric_limits<float>::max();
    CabinAction action=CabinAction::None;
    for(const auto& item:fighterCabinControls()) {
        const float d=vsg::dot(item.normal,ray);
        if(d>=-.001f)continue;
        const float t=vsg::dot(item.normal,item.center)/d;
        if(t>.09f&&t<best&&vsg::length(ray*t-item.center)<=item.radius){best=t;action=item.action;}
    }
    if(action==CabinAction::None)return action;
    // Reject controls hidden behind a cockpit part. The small margin allows
    // for the actual key cap profile around the registered interaction plane.
    for(size_t i=0;i<occluders.size();i+=3) {
        const auto a=occluders[i],e1=occluders[i+1]-a,e2=occluders[i+2]-a;
        const auto h=vsg::cross(ray,e2);const float det=vsg::dot(e1,h);
        if(std::abs(det)<1e-8f)continue;
        const auto s=-a;
        const float u=vsg::dot(s,h)/det;
        if(u<0||u>1)continue;
        const auto q=vsg::cross(s,e1);
        const float v=vsg::dot(ray,q)/det;
        if(v<0||u+v>1)continue;
        const float t=vsg::dot(e2,q)/det;
        if(t>.09f&&t<best-.028f)return CabinAction::None;
    }
    return action;
}

void SpatialCockpit::updateLight(P ambient,P sun,P localSun,float panelLamp) const {
    if(!light)return;
    const std::array<C,3> values{{{ambient.x,ambient.y,ambient.z,panelLamp},{sun.x,sun.y,sun.z,0},
                                 {localSun.x,localSun.y,localSun.z,0}}};
    if(!std::equal(values.begin(),values.end(),light->begin())) {
        std::copy(values.begin(),values.end(),light->begin());light->dirty();
    }
}

SpatialCockpit makeFighterCabin(const CockpitInterior& s, vsg::ref_ptr<vsg::Data> artwork,
                              vsg::ref_ptr<vsg::MatrixTransform>& stick, vsg::ref_ptr<vsg::MatrixTransform>& throttle) {
    if(!artwork)return {};
    const auto dir=gui::ViewportKit::resolveAssetPath("shaders/viewport");
    auto vert=vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT,"main",dir+"/cockpit_surface.vert");
    auto frag=vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT,"main",dir+"/cockpit_surface.frag");
    if(!vert||!frag)return {};
    vsg::ShaderStages stages{vert,frag};
    const auto compiler=vsg::ShaderCompiler::create();
    if(!compiler||!compiler->supported()||!compiler->compile(stages))return {};
    auto group=vsg::Group::create();
    auto light=vsg::vec4Array::create(3);
    light->properties.dataVariance=vsg::DYNAMIC_DATA;
    SpatialCockpit result{group,light};
    result.updateLight({.40f,.43f,.48f},{.8f,.77f,.70f},{.32f,.87f,-.38f},.45f);
    auto white=vsg::ubvec4Array2D::create(1,1,vsg::ubvec4(255,255,255,255),vsg::Data::Properties{VK_FORMAT_R8G8B8A8_SRGB});
    const C coating(.042f,.048f,.053f,.66f), top(.071f,.081f,.091f,.72f), rubber(.022f,.024f,.026f,.51f);
    const C dark(.023f,.028f,.033f,.63f), seam(.10f,.116f,.125f,.82f), rivet(.27f,.29f,.30f,.97f);

    auto panel=surface(stages,artwork,light,false);
    CabinMesh face;
    for(size_t i=1;i<panelSeams.size();++i) {
        const auto a=panelSeams[i-1], b=panelSeams[i];
        face.quad(cockpitPanelPoint(a,0),cockpitPanelPoint(b,0),cockpitPanelPoint(b,1),cockpitPanelPoint(a,1),
                  {1,1,1,.57f},{a,0},{b,0},{b,1},{a,1});
    }
    add(*panel,face);group->addChild(panel);

    auto shell=surface(stages,white,light,false);
    CabinMesh hull;
    const auto brow=[&](float u) {auto p=cockpitPanelPoint(u,0);p.y+=.035f*(1.f-std::abs(u*2.f-1.f));return p;};
    const auto forward=[&](float u) {
        auto p=brow(u);p.z-=.28f;p.y-=.075f;p.x*=.89f;return p;
    };
    for(int i=0;i<20;++i) {
        const float a=i/20.f,b=(i+1)/20.f;
        hull.quad(forward(a),forward(b),brow(b),brow(a),coating);
        hull.quad(brow(a),brow(b),cockpitPanelPoint(b,0),cockpitPanelPoint(a,0),dark);
        hull.beam(brow(a),brow(b),.013f,.018f,rubber);
    }
    const auto front=[&](float u,float v,float depth=.013f){return cockpitPanelPoint(u,v)+P(0,0,depth);};
    const auto frame=[&](float x,float y,float w,float h) {
        const float t=.0072f;
        for(float u:{x,x+w})hull.beam(front(u,y),front(u,y+h),t,t,rubber);
        for(float v:{y,y+h})hull.beam(front(x,v),front(x+w,v),t,t,rubber);
    };
    for(float center:{.273f,.725f}) {
        frame(center-.076f,.173f,.152f,.284f);
        for(int i=0;i<5;++i) {
            const float u=center-.061f+i*.030f;
            for(float v:{.151f,.481f}) {
                const auto key=front(u,v,.017f);
                hull.bevel(key,{.012f,.0084f,.013f},.0031f,coating);
                if(i<3&&v>.4f) {
                    const float w=.0035f+i*.0015f;
                    hull.beam(key+P(-w,-.0005f,.0132f),key+P(w,-.0005f,.0132f),.00085f,.00085f,rivet,7);
                }
            }
            for(float edge:{center-.093f,center+.093f})
                hull.bevel(front(edge,.198f+i*.054f,.017f),{.009f,.011f,.012f},.003f,coating);
        }
        for(float dx:{-.095f,.095f})for(float v:{.162f,.482f}) {
            const auto p=front(center+dx,v,.015f);
            hull.beam(p,p+P(0,0,.004f),.0043f,.0043f,rivet,14,1.f,true);
            hull.beam(p+P(-.003f,0,.0045f),p+P(.003f,0,.0045f),.0011f,.0011f,dark,8,1.f,true);
        }
    }
    for(int j=0;j<4;++j)for(int i=0;i<3;++i){
        const auto p=front(.445f+i*.053f,.122f+j*.061f,.011f);
        // A narrow edge lets the texture retain the printed glyph on the key top.
        hull.beam(p-P(.015f,.008f,0),p+P(.015f,-.008f,0),.0018f,.004f,coating);
    }
    for(float u:{.4225f,.5005f,.581f})for(float v:{.472f,.642f}) {
        const float radius=v<.5f?.045f:.049f;
        for(int i=0;i<36;++i) {
            const auto rimPoint=[&](int k) {
                const float a=2*pi*k/36;
                return front(u+radius/1.53f*std::cos(a),v+radius/.62f*std::sin(a),.010f);
            };
            hull.beam(rimPoint(i),rimPoint(i+1),.0026f,.0031f,coating,8);
        }
    }
    // Side rails extend past the seat. Turning one's head shows the depth of the side consoles.
    for(float side:{-1.f,1.f}) {
        const P a=cockpitPanelPoint(side>0?1:0,0), b(side*.90f,-.295f,-.30f), c(side*.86f,-.32f,.38f);
        hull.beam(a,b,.028f,.027f,top); hull.beam(b,c,.028f,.027f,top);
        hull.beam(a+P(0,.027f,-.014f),b+P(0,.026f,0),.020f,.014f,rubber);
        hull.beam(b+P(0,.026f,0),c+P(0,.026f,0),.020f,.014f,rubber);
        const P ia=a+P(-side*.15f,-.22f,.08f),ib=b+P(-side*.33f,-.25f,0),ic=c+P(-side*.35f,-.27f,0);
        hull.quad(a,b,ib,ia,coating);hull.quad(b,c,ic,ib,dark);
        const auto inset=[&](float u,float v) {
            const auto outer=a+(b-a)*v, inner=ia+(ib-ia)*v;
            return outer+(inner-outer)*u+P(-side*.023f,.030f,0);
        };
        const auto dial=[&](P p,float radius) {
            const P axis(side*.45f,.89f,-.05f);
            hull.beam(p,p+axis*.012f,radius,radius,rubber,24,.94f,true);
            hull.beam(p+axis*.012f,p+axis*.023f,radius*.76f,radius*.78f,dark,22,.88f,true);
            for(int i=0;i<12;++i) {
                const float ang=2*pi*i/12;
                const P offset(radius*.79f*std::cos(ang), -side*radius*.37f*std::cos(ang),radius*.87f*std::sin(ang));
                hull.beam(p+axis*.010f+offset,p+axis*.021f+offset*.94f,.0011f,.0011f,seam,6);
            }
            const auto mark=p+axis*.0235f;
            hull.beam(mark+P(0,0,-radius*.50f),mark+P(0,0,radius*.36f),.0016f,.0016f,rivet);
        };
        for(int i=0;i<3;++i) {
            const float v=.10f+.27f*i;
            hull.quad(inset(.22f,v),inset(.76f,v),inset(.76f,v+.21f),inset(.22f,v+.21f),
                      {.038f,.043f,.052f,.61f});
            for(float u:{.25f,.73f}) {
                const auto p=inset(u,v+.025f);
                hull.beam(p,p+P(0,.003f,0),.0032f,.0032f,rivet);
            }
            for(int tick=0;tick<5;++tick) {
                const auto p=inset(.27f+.09f*tick,v+.166f);
                const auto length=tick==2?.009f:.005f;
                hull.beam(p,p+P(0,0,length),.0008f,.0008f,rivet,7);
            }
            dial(inset(.47f,v+.108f),.016f);
            if(i!=2) {
                auto p=inset(.66f,v+.14f);
                hull.beam(p,p+P(-side*.016f,.021f,.007f),.0035f,.0035f,seam);
            }
        }
        hull.bevel({side*.735f,-.374f,.13f},{.067f,.015f,.092f},.010f,rubber);
        for(int i=0;i<3;++i) {
            const P p{side*(.76f-.035f*i),-.352f,.132f};
            hull.box(p,{.008f,.020f,.025f},seam);
        }
        hull.quad(ia,ib,ib-P(0,.40f,0),ia-P(0,.40f,0),dark);
        hull.quad(ib,ic,ic-P(0,.40f,0),ib-P(0,.40f,0),coating);
        hull.beam(ia,ib,.017f,.018f,seam);hull.beam(ib,ic,.015f,.018f,rubber);
        for(int i=0;i<7;++i) {
            const auto p=b+(c-b)*(float(i)+.5f)/7.f-P(side*.03f,.006f,0);
            hull.box(p,{.007f,.007f,.009f},rivet);
        }
        // Aft edge of the bubble canopy. No central windscreen pillar in the F-16.
        const P aft(side*.73f,.40f,.43f);
        hull.beam(c,aft,.048f,.035f,coating);
        hull.beam(aft,{side*.32f,.69f,.48f},.045f,.030f,coating);
    }
    const auto floorLine=[](P top) {return P(top.x,-.91f,top.z);};
    for(size_t i=1;i<panelSeams.size();++i) {
        const auto a=cockpitPanelPoint(panelSeams[i-1],1), b=cockpitPanelPoint(panelSeams[i],1);
        hull.quad(a,b,floorLine(b),floorLine(a),dark);
        hull.quad(floorLine(a),floorLine(b),{b.x,-.91f,.56f},{a.x,-.91f,.56f},rubber);
    }
    for(float side:{-1.f,1.f}) {
        const auto s=cockpitPanelPoint(side>0?1:0,1);
        const P back(side*.84f,-.57f,.54f), middle(side*.89f,-.53f,-.31f);
        hull.quad(s,middle,floorLine(middle),floorLine(s),dark);
        hull.quad(middle,back,floorLine(back),floorLine(middle),dark);
        // Join the far sill to the lower bulkhead without exposing terrain through the side.
        const P a=cockpitPanelPoint(side>0?1:0,0), b(side*.90f,-.295f,-.30f), c(side*.86f,-.32f,.38f);
        hull.quad(a,b,middle,s,coating);
        hull.quad(b,c,back,middle,coating);
    }

    const auto glass=[&](float x,float y) {return P(x*s.glassHalfWidth,s.glassCentreY+y*s.glassHalfHeight,-s.glassDepth);};
    hull.box({0,-.183f,-s.glassDepth-.026f},{.194f,.022f,.045f},dark);
    hull.beam({-.189f,-.164f,-s.glassDepth+.015f},{.189f,-.164f,-s.glassDepth+.015f},.008f,.012f,rubber);
    for(float side:{-1.f,1.f}) {
        hull.beam(glass(side,-1)-P(0,.062f,-.008f),glass(side,-1),.032f,.019f,top);
        hull.beam(glass(side,-1),glass(side,.74f),.006f,.004f,seam);
        hull.beam(glass(side,.74f),glass(side*.78f,1),.005f,.003f,seam);
    }
    hull.beam(glass(-1,-1),glass(1,-1),.012f,.014f,rubber);
    hull.beam(glass(-.78f,1),glass(.78f,1),.004f,.003f,seam);
    for(const auto& v:hull.vertices)result.occluders.push_back(v.p);
    result.triangleCount=(hull.vertices.size()+face.vertices.size())/3;
    add(*shell,hull);
    const auto mountSwitch=[&](size_t index,auto& transform) {
        CabinMesh base,lever;
        const auto p=fighterCabinControls()[index].center;
        if(index==9) {
            base.bevel(p-P(0,0,.066f),{.037f,.047f,.014f},.008f,dark);
            for(float side:{-.022f,.022f})
                base.beam(p+P(side,-.028f,-.055f),p+P(side,.020f,-.055f),.0027f,.0027f,seam,12);
            lever.beam({0,0,-.062f},{0,0,0},.010f,.009f,rivet,20,.83f,true);
            lever.beam({-.019f,0,0},{.019f,0,0},.018f,.018f,{.46f,.39f,.30f,.69f},28,.9f,true);
            for(int n=0;n<6;++n) {
                const float x=-.015f+n*.006f;
                lever.beam({x,-.011f,.012f},{x,.011f,.012f},.0011f,.0011f,rubber,8);
            }
        } else {
        const float offset=index>=10?.021f:0.f;
        base.beam(p-P(0,0,.023f+offset),p-P(0,0,.006f),.019f,.019f,coating,24,.89f,true);
        base.beam(p-P(0,0,.010f),p-P(0,0,.003f),.013f,.013f,rivet,24,1.f,true);
        if(index==6) {
            lever.beam({0,0,-.010f},{0,0,.007f},.014f,.014f,dark,30,.90f,true);
            for(int i=0;i<24;++i) {
                const float a=i*2*pi/24;
                const P offset(.014f*std::cos(a),.014f*std::sin(a),0);
                lever.beam(offset-P(0,0,.010f),offset*.91f+P(0,0,.006f),.0011f,.0011f,seam,6);
            }
            lever.beam({0,0,.0076f},{0,.010f,.0076f},.0017f,.0017f,rivet,8,1.f,true);
        } else {
            lever.beam({0,0,-.008f},{0,0,.022f},.0045f,.0045f,rivet,18,.7f,true);
            lever.bevel({0,0,.022f},{.007f,.005f,.008f},.003f,rubber);
        }
        }
        add(*shell,base);
        transform=vsg::MatrixTransform::create();add(*transform,lever);shell->addChild(transform);
        result.triangleCount+=(base.vertices.size()+lever.vertices.size())/3;
    };
    mountSwitch(6,result.lightKnob);mountSwitch(7,result.hudToggle);mountSwitch(8,result.holdToggle);
    mountSwitch(9,result.gearHandle);mountSwitch(10,result.navToggle);mountSwitch(11,result.landingToggle);
    {
        CabinMesh grip;
        grip.bevel({0,0,0},{.071f,.038f,.075f},.015f,rubber);
        for(int i=0;i<7;++i) {
            const float y=.018f+i*.0067f, r=.061f-.004f*i;
            grip.beam({0,y,0},{0,y+.004f,0},r,r*.84f,coating,30,.84f,true);
        }
        grip.beam({0,.050f,0},{-.019f,.170f,.022f},.029f,.033f,dark,30,1.21f,true);
        grip.beam({-.019f,.165f,.022f},{-.031f,.218f,.019f},.035f,.036f,coating,32,.77f,true);
        grip.bevel({-.019f,.188f,.028f},{.036f,.036f,.031f},.014f,rubber);
        grip.beam({.013f,.192f,.005f},{.020f,.192f,.005f},.013f,.013f,coating,24,.92f,true);
        grip.beam({.0205f,.192f,-.003f},{.0205f,.192f,.013f},.0012f,.0012f,seam,8);
        grip.beam({-.028f,.114f,.027f},{.006f,.162f,.048f},.0018f,.0018f,seam,10);
        grip.beam({-.027f,.207f,.049f},{-.030f,.207f,.066f},.012f,.012f,{.22f,.030f,.017f,.71f},24,.83f,true);
        grip.beam({.006f,.178f,.046f},{.006f,.179f,.065f},.010f,.010f,top,18,.9f,true);
        grip.bevel({.006f,.180f,.066f},{.010f,.0033f,.006f},.002f,dark);
        grip.bevel({.006f,.180f,.067f},{.0033f,.010f,.006f},.002f,dark);
        grip.beam({-.024f,.227f,.016f},{-.024f,.240f,.015f},.011f,.010f,rubber,16,.64f,true);
        for(int i=0;i<12;++i) {
            const float y=.072f+.007f*i;
            grip.beam({-.027f,y,.026f},{.009f,y+.002f,.031f},.0014f,.0015f,coating,7);
        }
        stick=vsg::MatrixTransform::create();add(*stick,grip);shell->addChild(stick);
        result.triangleCount+=grip.vertices.size()/3;
    }
    {
        CabinMesh grip,track;
        track.bevel({-.60f,-.571f,-.432f},{.095f,.018f,.204f},.014f,coating);
        track.bevel({-.60f,-.554f,-.432f},{.037f,.009f,.190f},.009f,rubber);
        for(float x:{-.64f,-.56f})track.beam({x,-.548f,-.603f},{x,-.548f,-.264f},.003f,.003f,seam,12);
        grip.bevel({0,0,0},{.056f,.026f,.056f},.013f,rubber);
        grip.beam({0,.010f,0},{.021f,.094f,-.026f},.035f,.034f,coating,28,1.1f,true);
        grip.beam({-.069f,.111f,-.025f},{.058f,.113f,-.036f},.041f,.044f,dark,32,.88f,true);
        grip.bevel({.048f,.122f,.001f},{.021f,.017f,.011f},.006f,coating);
        grip.beam({.050f,.129f,.007f},{.050f,.129f,.019f},.009f,.009f,seam,16,.82f,true);
        grip.beam({-.062f,.111f,.012f},{-.062f,.111f,.021f},.009f,.009f,rubber,20,.85f,true);
        for(int i=0;i<12;++i) {
            const float x=-.052f+i*.009f;
            grip.beam({x,.136f,-.054f},{x,.145f,-.011f},.00165f,.00165f,coating,7);
        }
        add(*shell,track);
        throttle=vsg::MatrixTransform::create();add(*throttle,grip);shell->addChild(throttle);
        result.triangleCount+=(grip.vertices.size()+track.vertices.size())/3;
    }
    group->addChild(shell);
    auto combiner=surface(stages,white,light,true);
    CabinMesh pane;
    const C glassTint(1,1,1,.026f);
    pane.quad(glass(-1,-1),glass(1,-1),glass(1,.74f),glass(-1,.74f),glassTint);
    pane.quad(glass(-1,.74f),glass(1,.74f),glass(.78f,1),glass(-.78f,1),glassTint);
    result.triangleCount+=pane.vertices.size()/3;
    add(*combiner,pane);group->addChild(combiner);
    return result;
}
}
