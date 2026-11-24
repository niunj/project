#include "AircraftExterior.h"
#include <vsg/all.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vsgraysim/core/ObjLoader.h>

namespace vsgraysim::viewport3d {
namespace {
vsg::ref_ptr<vsg::Node> texturedObj(const std::string& path,vsg::ref_ptr<vsg::Options> options){
    std::vector<vsgraysim::ObjMaterialSlot> slots;auto source=vsgraysim::loadObjNode(path,options,&slots);auto group=source.cast<vsg::Group>();if(!group)return source;
    auto result=vsg::Group::create();auto builder=vsg::Builder::create();builder->options=options;
    for(size_t index=0;index<group->children.size();++index){auto material=group->children[index].cast<vsg::StateGroup>();if(!material){result->addChild(group->children[index]);continue;}
        const auto slot=index<slots.size()?slots[index]:vsgraysim::ObjMaterialSlot{};vsg::StateInfo state;state.lighting=true;
        if(!slot.map_kd.empty())state.image=vsg::read_cast<vsg::Data>((std::filesystem::path(path).parent_path()/slot.map_kd).string(),options);
        if(state.image&&state.image->properties.format==VK_FORMAT_R8G8B8A8_UNORM)state.image->properties.format=VK_FORMAT_R8G8B8A8_SRGB;
        auto surface=builder->createStateGroup(state);
        for(const auto& child:material->children){auto old=child.cast<vsg::VertexIndexDraw>();if(!old||old->arrays.size()<2||!old->indices||!old->arrays[0]->data||!old->arrays[1]->data){surface->addChild(child);continue;}
            auto vertices=old->arrays[0]->data,normals=old->arrays[1]->data;auto uv=vsg::vec2Array::create(vertices->valueCount());
            if(old->arrays.size()>2)if(auto sourceUV=old->arrays[2]->data.cast<vsg::vec2Array>()){for(size_t i=0;i<std::min(uv->size(),sourceUV->size());++i){(*uv)[i]=(*sourceUV)[i];if(state.image&&state.image->properties.origin==vsg::TOP_LEFT)(*uv)[i].y=1-(*uv)[i].y;}}
            auto color=vsg::vec4Array::create({slot.diffuse});auto draw=vsg::VertexIndexDraw::create();draw->assignArrays({vertices,normals,uv,color});draw->assignIndices(old->indices->data);draw->indexCount=old->indexCount;draw->instanceCount=1;surface->addChild(draw);
        }result->addChild(surface);
    }return result;
}
vsg::ref_ptr<vsg::Node> representativeJet(){
    auto root=vsg::Group::create();auto builder=vsg::Builder::create();
    vsg::StateInfo state;state.lighting=true;state.two_sided=true;
    auto shape=[&](vsg::vec3 position,vsg::vec3 size,vsg::vec4 color,int type){vsg::GeometryInfo g;g.position=position;g.dx={size.x,0,0};g.dy={0,size.y,0};g.dz={0,0,size.z};g.color=color;
        root->addChild(type==0?builder->createSphere(g,state):type==1?builder->createCylinder(g,state):builder->createBox(g,state));};
    const vsg::vec4 white(.83,.87,.89,1),grey(.43,.48,.5,1),blue(.07,.23,.38,1);
    shape({0,0,0},{3.8f,3.8f,37},white,0);
    auto wing=[&](vsg::vec3 centre,vsg::vec3 span,vsg::vec3 chord,vsg::vec4 color){vsg::GeometryInfo g;g.position=centre;g.dx=span;g.dy=chord;g.color=color;root->addChild(builder->createQuad(g,state));};
    wing({-8,-.4f,1},{-16,0,8},{0,0,5},grey);wing({8,-.4f,1},{16,0,8},{0,0,5},grey);
    wing({-3,.6f,14},{-6,0,2},{0,0,3},white);wing({3,.6f,14},{6,0,2},{0,0,3},white);
    wing({0,3.2f,13},{0,5,2},{0,0,5},blue);
    for(float side:{-1.f,1.f}){shape({side*5,-2,-.5f},{2.2f,2.2f,4.5f},white,1);shape({side*5,-2,-2.77f},{1.7f,1.7f,.06f},{.025f,.035f,.04f,1},0);}
    shape({0,1.1f,-14},{2.9f,.3f,2.1f},{.025f,.07f,.1f,1},2);return root;
}
}
vsg::ref_ptr<vsg::Node> loadAircraftExterior(const std::string& path,const std::string& upAxis,
        const std::string&,double maximumDimension,vsg::ref_ptr<vsg::Options> options,bool& representative,double headingDegrees){
    vsg::ref_ptr<vsg::Node> model;if(!path.empty())try{model=std::filesystem::path(path).extension()==".obj"?texturedObj(path,options):vsg::read_cast<vsg::Node>(path,options);}catch(const vsg::Exception&){}catch(const std::exception&){}
    representative=!model;if(!model)model=representativeJet();
    auto oriented=vsg::MatrixTransform::create();oriented->matrix=representative?vsg::dmat4(1):vsg::rotate(vsg::radians(headingDegrees),vsg::dvec3(0,1,0))*(upAxis=="z"?vsg::rotate(vsg::radians(-90.),vsg::dvec3(1,0,0)):vsg::dmat4(1));oriented->addChild(model);
    vsg::ComputeBounds bounds;oriented->accept(bounds);
    if(!bounds.bounds.valid())return oriented;
    const auto size=bounds.bounds.max-bounds.bounds.min;const auto centre=(bounds.bounds.min+bounds.bounds.max)*.5;
    const double span=std::max({size.x,size.y,size.z});const double scale=span>1e-9?maximumDimension/span:1.;
    auto normalized=vsg::MatrixTransform::create();normalized->matrix=vsg::scale(scale,scale,scale)*vsg::translate(-centre);normalized->addChild(oriented);return normalized;
}
}
