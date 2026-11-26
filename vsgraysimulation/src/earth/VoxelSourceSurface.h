#pragma once
#include "VoxelSurfaceAppearance.h"
#include <vsg/core/Array2D.h>
#include <algorithm>
#include <future>
#include <thread>
#include <limits>
#include <stdexcept>
namespace vsgraysim::earth::detail {
inline uint8_t atlasOpacity(float alpha,bool blend,float cutoff) {
    if(blend)return uint8_t(std::lround(std::clamp(alpha,0.f,1.f)*255.f));
    return cutoff>=0.f&&alpha<cutoff?0:255;
}
struct SourcePatch {uint32_t cell;size_t triangle;std::vector<vsg::dvec3> points;};
inline std::vector<vsg::dvec3> clipSourceTriangle(vsg::dvec3 a,vsg::dvec3 b,vsg::dvec3 c,const vsg::dvec3& low,double cell) {
    std::vector<vsg::dvec3> polygon{a,b,c};
    for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side){
        const double plane=low[axis]+side*cell;std::vector<vsg::dvec3> out;
        if(polygon.empty())return {};
        auto previous=polygon.back();double pd=(previous[axis]-plane)*(side?-1:1);
        for(const auto& p:polygon){double d=(p[axis]-plane)*(side?-1:1);
            if((d>=0)!=(pd>=0)){auto q=previous+(p-previous)*(pd/(pd-d));q[axis]=plane;out.push_back(q);}
            if(d>=0)out.push_back(p);previous=p;pd=d;
        }
        polygon=std::move(out);
    }
    // Remove clipping duplicates and zero-area fans, without snapping vertices.
    std::vector<vsg::dvec3> clean;for(const auto& p:polygon)if(clean.empty()||vsg::length(p-clean.back())>cell*1e-11)clean.push_back(p);
    if(clean.size()>1&&vsg::length(clean.front()-clean.back())<=cell*1e-11)clean.pop_back();
    if(clean.size()<3)return {};
    vsg::dvec3 area{};for(size_t j=1;j+1<clean.size();++j)area+=vsg::cross(clean[j]-clean[0],clean[j+1]-clean[0]);
    if(vsg::length(area)<cell*cell*1e-15)return {};
    return clean;
}
template<class Collector>
std::shared_ptr<VoxelSurfaceAppearance> sourceAppearance(const Collector& source,std::vector<SourcePatch> patches,
 const vsg::dvec3& origin,double cell,std::array<int,3> dimensions,const std::vector<bool>& occupied,unsigned workers){
    if(patches.empty())throw std::runtime_error("source surface has no clipped triangles");
    std::stable_sort(patches.begin(),patches.end(),[](const auto& a,const auto& b){return a.cell<b.cell;});
    size_t vertices=0;for(const auto& p:patches)vertices+=p.points.size();
    if(vertices>3'900'000)throw std::runtime_error("clipped source surface requires "+std::to_string(vertices)+" vertices; exceeds 4M detail budget");
    auto out=std::make_shared<VoxelSurfaceAppearance>();out->origin=origin;out->cellSize=cell;out->dimensions=dimensions;out->originalOccupancy=occupied;
    int tile=10;for(;tile>=4;--tile){const size_t columns=size_t(std::ceil(std::sqrt(patches.size()+1.)));const size_t rows=(patches.size()+columns)/columns;if(columns*rows*tile*tile<=16'777'216)break;}
    if(tile<4)throw std::runtime_error("source surface exceeds 64 MiB atlas budget");
    const unsigned columns=unsigned(std::ceil(std::sqrt(patches.size()+1.)));out->width=columns*tile;out->height=unsigned((patches.size()+columns)/columns)*tile;
    const size_t bytes=size_t(out->width)*out->height*4;out->rgba.resize(bytes);out->mr_rgba.resize(bytes);
    const bool normal=std::any_of(source.attributes.begin(),source.attributes.end(),[](const auto& v){return bool(v.normalImage);});
    const bool emission=std::any_of(source.attributes.begin(),source.attributes.end(),[](const auto& v){return vsg::length(v.emissive)>0;});
    if(normal)out->normal_rgba.resize(bytes);if(emission)out->emissive_rgba.resize(bytes);
    for(size_t i=0;i<bytes;i+=4){out->rgba[i]=74;out->rgba[i+1]=77;out->rgba[i+2]=80;out->rgba[i+3]=255;out->mr_rgba[i]=255;out->mr_rgba[i+1]=204;out->mr_rgba[i+2]=178;out->mr_rgba[i+3]=255;if(normal){out->normal_rgba[i]=128;out->normal_rgba[i+1]=128;out->normal_rgba[i+2]=255;out->normal_rgba[i+3]=255;}}
    std::array<double,256> linear{};for(int i=0;i<256;++i){double c=i/255.;linear[i]=c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);}
    auto sample=[&](const vsg::Data* data,vsg::vec2 uv,bool srgb){const auto* image=data?data->template cast<vsg::ubvec4Array2D>():nullptr;if(!image)return vsg::vec4(1,1,1,1);
        const double px=(uv.x-std::floor(uv.x))*image->width()-.5,py=(uv.y-std::floor(uv.y))*image->height()-.5;const int ix=int(std::floor(px)),iy=int(std::floor(py));vsg::vec4 result{};
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto c=(*image)((ix+dx+int(image->width()))%image->width(),(iy+dy+int(image->height()))%image->height());const float w=float((dx?px-ix:1-px+ix)*(dy?py-iy:1-py+iy));for(int j=0;j<4;++j)result[j]+=float(srgb&&j<3?linear[c[j]]:c[j]/255.)*w;}return result;};
    out->patches.resize(patches.size());
    auto bake=[&](size_t begin,size_t end){for(size_t k=begin;k<end;++k){const auto& p=patches[k];auto& destination=out->patches[k];destination.cell=p.cell;const auto i=p.triangle;const auto a=source.triangles_[i],e0=source.triangles_[i+1]-a,e1=source.triangles_[i+2]-a;const auto geometric=vsg::normalize(vsg::cross(e0,e1));
        int axis=0;for(int d=1;d<3;++d)if(std::abs(geometric[d])>std::abs(geometric[axis]))axis=d;const int u=(axis+1)%3,v=(axis+2)%3;
        double loU=1e30,hiU=-1e30,loV=1e30,hiV=-1e30;for(const auto& q:p.points){loU=std::min(loU,q[u]);hiU=std::max(hiU,q[u]);loV=std::min(loV,q[v]);hiV=std::max(hiV,q[v]);}
        const double width=std::max(cell*1e-12,hiU-loU),height=std::max(cell*1e-12,hiV-loV);const double den=e0[u]*e1[v]-e0[v]*e1[u];
        auto weights=[&](double pu,double pv){const double du=pu-a[u],dv=pv-a[v];const double b=(du*e1[v]-dv*e1[u])/den,c=(e0[u]*dv-e0[v]*du)/den;return vsg::dvec3(1-b-c,b,c);};
        auto nAt=[&](const vsg::dvec3& w){auto n=vsg::dvec3(source.attributes[i].normal)*w.x+vsg::dvec3(source.attributes[i+1].normal)*w.y+vsg::dvec3(source.attributes[i+2].normal)*w.z;return vsg::length(n)>1e-9?vsg::normalize(n):geometric;};
        const unsigned slot=unsigned(k+1);const double tileU=slot%columns*tile+1.5,tileV=slot/columns*tile+1.5,span=tile-3.;
        for(const auto& q:p.points){const auto w=weights(q[u],q[v]);destination.vertices.push_back({q,vsg::vec3(nAt(w)),vsg::vec2(float((tileU+(q[u]-loU)/width*span)/out->width),float((tileV+(q[v]-loV)/height*span)/out->height))});}
        for(int y=0;y<tile;++y)for(int x=0;x<tile;++x){const auto w0=weights(loU+std::clamp((x-1.)/span,0.,1.)*width,loV+std::clamp((y-1.)/span,0.,1.)*height);auto w=vsg::dvec3(std::max(0.,w0.x),std::max(0.,w0.y),std::max(0.,w0.z));w/=w.x+w.y+w.z;vsg::vec2 uv{};vsg::vec4 color{};vsg::vec3 emit{};for(int j=0;j<3;++j){uv+=source.attributes[i+j].uv*float(w[j]);color+=source.attributes[i+j].color*float(w[j]);emit+=source.attributes[i+j].emissive*float(w[j]);}
            const auto& attr=source.attributes[i];const auto texel=sample(attr.image.get(),uv,true),mr=sample(attr.mrImage.get(),uv,false);const size_t d=4*(size_t(slot/columns*tile+y)*out->width+slot%columns*tile+x);
            for(int c=0;c<3;++c){const double z=std::clamp(double(texel[c]*color[c]),0.,1.);out->rgba[d+c]=uint8_t(std::lround(255*(z<=.0031308?12.92*z:1.055*std::pow(z,1/2.4)-.055)));}
            out->rgba[d+3]=atlasOpacity(texel.a*color.a,attr.blend,attr.alphaCutoff);
            // The spare MR alpha tags blend tiles without changing the volume format.
            out->mr_rgba[d]=255;out->mr_rgba[d+1]=uint8_t(std::clamp(attr.roughness*mr.g,.04f,1.f)*255+.5f);out->mr_rgba[d+2]=uint8_t(std::clamp(attr.metallic*mr.b,0.f,1.f)*255+.5f);out->mr_rgba[d+3]=attr.blend?0:255;
            if(emission){
                const auto texture=sample(attr.emissionImage.get(),uv,true);
                for(int c=0;c<3;++c)out->emissive_rgba[d+c]=uint8_t(std::clamp(emit[c]*texture[c],0.f,1.f)*255+.5f);
                out->emissive_rgba[d+3]=255;
            }
            if(normal){vsg::dvec3 encoded(0,0,1);if(attr.normalImage){const auto tex=sample(attr.normalImage.get(),uv,false);const auto n=nAt(w);const auto du=source.attributes[i+1].uv-attr.uv,dv=source.attributes[i+2].uv-attr.uv;const double determinant=double(du.x)*dv.y-double(du.y)*dv.x;auto t=e0*double(dv.y)-e1*double(du.y);if(determinant<0)t=-t;t-=n*vsg::dot(t,n);if(vsg::length(t)<1e-10)t=e0-n*vsg::dot(e0,n);if(vsg::length(t)>1e-10){t=vsg::normalize(t);const auto b=vsg::cross(n,t)*(determinant<0?-1.:1.);const auto world=vsg::normalize(t*(tex.x*2-1.)+b*(tex.y*2-1.)+n*(tex.z*2-1.));vsg::dvec3 atlasT{};atlasT[u]=1;atlasT[axis]=-geometric[u]/geometric[axis];atlasT-=n*vsg::dot(atlasT,n);atlasT=vsg::normalize(atlasT);encoded=vsg::dvec3(vsg::dot(world,atlasT),vsg::dot(world,vsg::normalize(vsg::cross(n,atlasT))),vsg::dot(world,n));}}
                for(int c=0;c<3;++c)out->normal_rgba[d+c]=uint8_t(std::clamp(encoded[c]*.5+.5,0.,1.)*255+.5);out->normal_rgba[d+3]=255;}
        }
    }};
    const size_t jobs=std::min<size_t>(std::max(1u,workers),std::max<size_t>(1,patches.size()/128));std::vector<std::future<void>> futures;
    for(size_t j=1;j<jobs;++j)futures.push_back(std::async(std::launch::async,bake,patches.size()*j/jobs,patches.size()*(j+1)/jobs));bake(0,patches.size()/jobs);for(auto& f:futures)f.get();
    return out;
}
}
