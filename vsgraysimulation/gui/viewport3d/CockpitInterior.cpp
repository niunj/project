#include "CockpitInterior.h"
#include <vsg/all.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace vsgraysim::viewport3d {
CockpitInterior cockpitInterior(std::string_view category) {
    CockpitInterior s;
    if (category == "helicopter") {
        s.eye={0,1.65,.24}; s.halfWidth=1.03f; s.brow=-.34f;
        s.windscreenDepth=1.53f; s.glassHalfWidth=.31f; s.glassDepth=1.12f;
    } else if (category == "airliner") {
        s.eye={-.52,1.55,0}; s.halfWidth=1.37f; s.brow=-.27f;
        s.windscreenDepth=1.45f; s.glassCentreY=-.02f; s.widePanel=true; s.splitWindscreen=true;
    } else if (category == "uav") {
        s.interior=false; s.eye={0,.24,-.55};
    }
    return s;
}

namespace {
using P=vsg::vec3;
using C=vsg::vec4;
struct Mesh {
    std::vector<P> points;
    std::vector<C> colors;
    void tri(P a,P b,P c,C shade) {
        points.insert(points.end(),{a,b,c}); colors.insert(colors.end(),3,shade);
    }
    void quad(P a,P b,P c,P d,C shade) { tri(a,b,c,shade); tri(a,c,d,shade); }
    void rail(P a,P b,float radius,C color) {
        const auto axis=vsg::normalize(b-a);
        const auto u=vsg::normalize(vsg::cross(axis,std::abs(axis.y)<.85f?P(0,1,0):P(1,0,0)));
        const auto v=vsg::cross(axis,u);
        constexpr int sides=8;
        for(int i=0;i<sides;++i) {
            const auto ring=[&](int k){const float angle=6.28318530718f*k/sides;return (u*std::cos(angle)+v*std::sin(angle))*radius;};
            const auto p=ring(i),q=ring(i+1);
            const float light=.62f+.38f*std::max(0.f,vsg::normalize(p+q).y);
            const C shaded(color.r*light,color.g*light,color.b*light,1);
            quad(a+p,a+q,b+q,b+p,shaded);
            tri(a,a+p,a+q,shaded); tri(b,b+q,b+p,shaded);
        }
    }
    void disc(P center,float radius,C color) {
        constexpr int sides=24;
        for(int i=0;i<sides;++i) {
            const auto rim=[&](int k){const float angle=6.28318530718f*k/sides;return center+P(std::cos(angle)*radius,std::sin(angle)*radius,0);};
            tri(center,rim(i),rim(i+1),color);
        }
    }
    void plate(P center,float hx,float hy,C color) {
        quad(center+P(-hx,-hy,0),center+P(hx,-hy,0),center+P(hx,hy,0),center+P(-hx,hy,0),color);
    }
    vsg::ref_ptr<vsg::Node> node() const {
        if(points.empty())return {};
        auto shader=vsg::createFlatShadedShaderSet();
        auto pipeline=vsg::GraphicsPipelineConfigurator::create(shader);
        auto raster=vsg::RasterizationState::create(); raster->cullMode=VK_CULL_MODE_NONE;
        pipeline->pipelineStates.push_back(raster);
        if(const auto& material=shader->getDescriptorBinding("material"))
            pipeline->assignDescriptor("material",material.data?material.data:vsg::ref_ptr<vsg::Data>(vsg::PhongMaterialValue::create()));
        auto vertex=vsg::vec3Array::create(points.size());
        auto normal=vsg::vec3Array::create(points.size(),P(0,1,0));
        auto uv=vsg::vec2Array::create(points.size(),vsg::vec2(0,0));
        auto color=vsg::vec4Array::create(points.size());
        std::copy(points.begin(),points.end(),vertex->begin());
        std::copy(colors.begin(),colors.end(),color->begin());
        vsg::DataList arrays;
        pipeline->assignArray(arrays,"vsg_Vertex",VK_VERTEX_INPUT_RATE_VERTEX,vertex);
        pipeline->assignArray(arrays,"vsg_Normal",VK_VERTEX_INPUT_RATE_VERTEX,normal);
        pipeline->assignArray(arrays,"vsg_TexCoord0",VK_VERTEX_INPUT_RATE_VERTEX,uv);
        pipeline->assignArray(arrays,"vsg_Color",VK_VERTEX_INPUT_RATE_VERTEX,color);
        pipeline->init();
        auto state=vsg::StateGroup::create(); pipeline->copyTo(state);
        auto geometry=vsg::Geometry::create(); geometry->assignArrays(arrays);
        geometry->commands.push_back(vsg::Draw::create(uint32_t(points.size()),1,0,0));
        state->addChild(geometry);
        return state;
    }
};
}

vsg::ref_ptr<vsg::Node> makeCockpitInterior(const CockpitInterior& s) {
    if(!s.interior)return {};
    Mesh m;
    const float w=s.halfWidth,y=s.brow,z=-s.windscreenDepth;
    const C hood(.052f,.065f,.074f,1), dash(.096f,.111f,.12f,1), rim(.135f,.155f,.16f,1);
    const C trim(.24f,.27f,.28f,1), fastener(.42f,.45f,.43f,1), rubber(.026f,.031f,.035f,1);
    const C screen(.016f,.041f,.048f,1), button(.21f,.26f,.25f,1), glassRim(.17f,.23f,.23f,1);
    const P left(-w,y,z),right(w,y,z),insideL(-w*.92f,y-.045f,-.97f),insideR(w*.92f,y-.045f,-.97f);
    // Raised central glare shield with softer, lighter edge bevels.
    const float raised=s.widePanel?.024f:.043f;
    const P lt(-w*.29f,y+raised,z+.01f),rt(w*.29f,y+raised,z+.01f);
    const P li(-w*.34f,y+raised-.033f,-.98f),ri(w*.34f,y+raised-.033f,-.98f);
    m.quad(left,lt,li,insideL,hood);
    m.quad(lt,rt,ri,li,C(.068f,.078f,.083f,1));
    m.quad(rt,right,insideR,ri,hood);
    m.rail(left,lt,.020f,rim); m.rail(lt,rt,.018f,rim); m.rail(rt,right,.020f,rim);
    m.quad(insideL,li,ri,insideR,rim);
    const float lower=y-.51f;
    m.quad(insideL,insideR,{insideR.x,lower,-.97f},{insideL.x,lower,-.97f},dash);
    m.rail(insideL,insideR,.022f,rim);
    for(float side:{-1.f,1.f}) {
        const P a(w*side,y,z),b(w*side*.82f,.34f,z+.24f),c(w*side*.49f,.57f,z+.35f),d(0,.64f,z+.40f);
        m.rail(a,b,.039f,trim); m.rail(b,c,.035f,trim); m.rail(c,d,.033f,trim);
        m.rail(a+P(-side*.039f,0,.005f),b+P(-side*.034f,0,.005f),.010f,rubber);
        for(int i=1;i<=3;++i) {
            const auto bolt=a+(b-a)*(i*.225f)+P(0,0,.036f);
            m.disc(bolt,.006f,fastener);
        }
        const P sill(w*side*.78f,y-.09f,.17f);
        m.rail(a,sill,.050f,rim);
        m.quad(a,sill,{sill.x,y-.62f,sill.z},{a.x,y-.67f,a.z},hood);
        m.rail(b,{sill.x,.48f,.17f},.034f,rubber);
        const P approach(side*.42f,y-.52f,-.47f), handle(side*.42f,y-.30f,-.50f);
        m.rail(approach,handle,.033f,rim);
        m.rail(handle,handle+P(.064f,0,-.016f),.041f,rubber);
    }
    if(s.splitWindscreen)m.rail({w*.52f,y,z},{w*.48f,.56f,z+.34f},.051f,trim);

    // HUD combiner support and rim are solid geometry; the opening lets the same
    // Earth camera see the scene directly. Symbology projects into this opening.
    const auto glass=[&](float x,float gy){return P(x*s.glassHalfWidth,s.glassCentreY+gy*s.glassHalfHeight,-s.glassDepth);};
    m.rail(glass(-1,-1),glass(1,-1),.015f,rubber);
    for(float side:{-1.f,1.f}) {
        m.rail({side*s.glassHalfWidth,y-.05f,-s.glassDepth+.025f},glass(side,-1),.028f,rim);
        m.rail(glass(side,-1),glass(side,.76f),.009f,glassRim);
        m.rail(glass(side,.76f),glass(side*.78f,1),.009f,glassRim);
    }
    m.rail(glass(-.78f,1),glass(.78f,1),.007f,glassRim);
    const float level=y-.17f;
    for(float side:{-1.f,1.f}) {
        const float x=side*(s.widePanel?.42f:.315f),hx=s.widePanel?.208f:.163f;
        m.plate({x,level,-.96f},hx,.169f,rubber);
        m.plate({x,level,-.955f},hx-.013f,.154f,rim);
        m.plate({x,level,-.95f},hx-.040f,.126f,screen);
        // Glass bevel, keys and screws. No baked telemetry on the displays.
        m.rail({x-hx+.04f,level+.114f,-.947f},{x+hx-.04f,level+.114f,-.947f},.0029f,glassRim);
        for(float dx:{-1.f,1.f})for(int i=-2;i<=2;++i)
            m.plate({x+dx*(hx-.025f),level+i*.047f,-.944f},.007f,.009f,button);
        for(float dx:{-1.f,1.f})for(float dy:{-1.f,1.f})
            m.plate({x+dx*(hx+.025f),level+dy*.15f,-.955f},.005f,.005f,fastener);
        const float dx=side*w*.655f;
        for(float gy : {.04f,-.135f}) {
            const P dial(dx,level+gy,-.957f);
            m.disc(dial,.076f,rim);
            m.disc(dial+P(0,0,.003f),.061f,rubber);
            m.disc(dial+P(0,0,.004f),.054f,screen);
            for(int i=0;i<=8;++i) {
                const float a=3.9269908f-i*.5890486f;
                const P radial(std::cos(a),std::sin(a),0),tangent(-radial.y,radial.x,0),centre=dial+P(0,0,.005f);
                m.quad(centre+radial*.050f-tangent*.0016f,centre+radial*.050f+tangent*.0016f,
                       centre+radial*.041f+tangent*.0016f,centre+radial*.041f-tangent*.0016f,button);
            }
        }
        for(int i=0;i<4;++i)m.plate({side*(w*.50f+.032f*i),level+.152f,-.954f},.009f,.005f,button);
    }
    m.plate({0,level+.062f,-.952f},.092f,.046f,rubber);
    for(int i=-2;i<=2;++i)m.plate({i*.029f,level+.062f,-.947f},.006f,.008f,i==0?glassRim:button);
    return m.node();
}
}
