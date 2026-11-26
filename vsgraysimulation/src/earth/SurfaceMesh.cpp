#include <vsgraysim/earth/SurfaceMesh.h>
#include <vsgraysim/earth/TerrainDisplay.h>
#include <vsg/utils/Builder.h>
#include <vsg/utils/GraphicsPipelineConfigurator.h>
#include <vsg/utils/ShaderSet.h>
#include <vsg/state/material.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/nodes/DepthSorted.h>
#include <vsg/utils/ComputeBounds.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Version.h>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vsg/core/Array.h>
#include <vsg/maths/transform.h>
#include <limits>

namespace vsgraysim::earth {
namespace {
vsg::ref_ptr<vsg::ShaderSet> atlasPbrShaderSet() {
#ifdef VSG_SUPPORTS_ShaderCompiler
    static const auto shaders=[] {
        auto shaderSet=vsg::createPhysicsBasedRenderingShaderSet();
        for(auto& stage:shaderSet->stages){
            if(stage->stage!=VK_SHADER_STAGE_FRAGMENT_BIT)continue;
            auto& module=*stage->module;
            constexpr std::string_view declaration="layout(location = 4) in vec2 texCoord[";
            const auto input=module.source.find(declaration);
            if(input==std::string::npos)continue;
            // With MSAA a tiny clipped triangle can cover samples without
            // covering the pixel centre. Do not extrapolate UVs into other tiles.
            module.source.insert(input+declaration.find("in vec2"),"centroid ");
            module.code.clear();
            shaderSet->variants.clear();
        }
        return shaderSet;
    }();
    return shaders;
#else
    return vsg::createPhysicsBasedRenderingShaderSet();
#endif
}

vsg::ref_ptr<vsg::vec3Array> surfaceNormals(const EarthSurfaceSnapshot& surface) {
    const auto& vertices=surface.vertices();
    const auto& indices=surface.indices();
    auto normals=vsg::vec3Array::create(vertices.size(),vsg::vec3(0,0,0));
    if(!surface.surface_normals.empty()) {
        if(surface.surface_normals.size()!=vertices.size())throw std::invalid_argument("surface normal count mismatch");
        std::copy(surface.surface_normals.begin(),surface.surface_normals.end(),normals->begin());
        return normals;
    }
    for(size_t i=0;i<indices.size();i+=3) {
        const auto a=indices[i],b=indices[i+1],c=indices[i+2];
        const auto area=vsg::cross(vertices[b]-vertices[a],vertices[c]-vertices[a]);
        if(vsg::dot(area,area)<1e-24)continue;
        for(auto vertex:{a,b,c})(*normals)[vertex]+=vsg::vec3(area);
    }
    for(auto& normal:*normals)if(vsg::dot(normal,normal)>0)normal=vsg::normalize(normal);
    return normals;
}
}

vsg::ref_ptr<vsg::VertexIndexDraw> makeSurfaceDraw(const EarthSurfaceSnapshot& surface, bool shaded) {
    const auto& vertices = surface.vertices();
    const auto& indices = surface.indices();
    auto positions = vsg::vec3Array::create(vertices.size());
    auto elements = vsg::uintArray::create(indices.size());
    for (size_t i = 0; i < vertices.size(); ++i) (*positions)[i] = vsg::vec3(vertices[i]);
    std::copy(indices.begin(), indices.end(), elements->begin());
    vsg::DataList arrays{positions};
    if (shaded) {
        arrays.push_back(surfaceNormals(surface));
        auto uv = vsg::vec2Array::create(vertices.size(), vsg::vec2(0,0));
        if (!surface.imagery_uv.empty()) std::copy(surface.imagery_uv.begin(),surface.imagery_uv.end(),uv->begin());
        arrays.push_back(uv);
        arrays.push_back(vsg::vec4Array::create(1, surface.imagery_rgba.empty() ? vsg::vec4(.54f,.49f,.37f,1.f) : vsg::vec4(1,1,1,1)));
    }
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays(arrays);
    draw->assignIndices(elements);
    draw->indexCount = static_cast<uint32_t>(indices.size());
    draw->instanceCount = 1;
    return draw;
}

vsg::ref_ptr<vsg::StateGroup> makeTerrainDisplay(const EarthSurfaceSnapshot& surface,std::optional<vsg::vec4> tint) {
    vsg::StateInfo state;
    state.lighting=true;
    if (!surface.imagery_rgba.empty()) {
        if (!surface.imagery_width || !surface.imagery_height ||
            surface.imagery_rgba.size()/4/surface.imagery_width!=surface.imagery_height ||
            surface.imagery_rgba.size()!=4ULL*surface.imagery_width*surface.imagery_height)
            throw std::invalid_argument("invalid voxel imagery dimensions");
        auto image=vsg::ubvec4Array2D::create(surface.imagery_width,surface.imagery_height);
        std::memcpy(image->dataPointer(),surface.imagery_rgba.data(),surface.imagery_rgba.size());
        image->properties.format=VK_FORMAT_R8G8B8A8_SRGB;
        state.image=image;
    }
    auto draw=makeSurfaceDraw(surface,true);
    if(tint)draw->arrays[3]->data=vsg::vec4Array::create(1,*tint);
    if(!surface.mr_rgba.empty()) {
        auto sampler=vsg::Sampler::create();sampler->addressModeU=sampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto image=[&](const std::vector<uint8_t>& bytes,VkFormat format){if(bytes.size()!=size_t(surface.imagery_width)*surface.imagery_height*4)throw std::invalid_argument("PBR atlas size mismatch");auto data=vsg::ubvec4Array2D::create(surface.imagery_width,surface.imagery_height);std::memcpy(data->dataPointer(),bytes.data(),bytes.size());data->properties.format=format;return data;};
        auto mr=image(surface.mr_rgba,VK_FORMAT_R8G8B8A8_UNORM);
        auto normal=surface.normal_rgba.empty()?nullptr:image(surface.normal_rgba,VK_FORMAT_R8G8B8A8_UNORM);
        auto emission=surface.emissive_rgba.empty()?nullptr:image(surface.emissive_rgba,VK_FORMAT_R8G8B8A8_UNORM);
        auto pass=[&](bool blend){
            auto pipeline=vsg::GraphicsPipelineConfigurator::create(atlasPbrShaderSet());
            pipeline->enableArray("vsg_Vertex",VK_VERTEX_INPUT_RATE_VERTEX,12);
            pipeline->enableArray("vsg_Normal",VK_VERTEX_INPUT_RATE_VERTEX,12);
            pipeline->enableArray("vsg_TexCoord0",VK_VERTEX_INPUT_RATE_VERTEX,8);
            pipeline->enableArray("vsg_Color",VK_VERTEX_INPUT_RATE_INSTANCE,16);
            pipeline->assignTexture("diffuseMap",state.image,sampler);
            pipeline->assignTexture("mrMap",mr,sampler);
            if(normal)pipeline->assignTexture("normalMap",normal,sampler);
            auto material=vsg::PbrMaterialValue::create();
            material->value().metallicFactor=1;material->value().roughnessFactor=1;
            if(emission){
                pipeline->assignTexture("emissiveMap",emission,sampler);
                material->value().emissiveFactor=vsg::vec4(1,1,1,1);
            }
            if(blend){
                material->value().alphaMask=0;
                for(auto& state:pipeline->pipelineStates){
                    if(auto depth=state.cast<vsg::DepthStencilState>())depth->depthWriteEnable=VK_FALSE;
                    if(auto color=state.cast<vsg::ColorBlendState>())color->configureAttachments(true);
                }
            }
            pipeline->assignDescriptor("material",material);
            pipeline->init();
            auto group=vsg::StateGroup::create();
            pipeline->copyTo(group);
            return group;
        };
        auto opaque=pass(false);
        if(!surface.hasMaterialBlend){opaque->addChild(draw);return opaque;}
        std::vector<uint32_t> solidIndices,blendIndices;
        const auto& indices=surface.indices();
        const auto& uv=surface.imagery_uv;
        if(uv.size()!=surface.vertices().size())throw std::invalid_argument("PBR atlas UV count mismatch");
        for(size_t i=0;i<indices.size();i+=3){
            const auto centre=(uv[indices[i]]+uv[indices[i+1]]+uv[indices[i+2]])/3.f;
            const auto x=std::clamp(int(centre.x*surface.imagery_width),0,int(surface.imagery_width)-1);
            const auto y=std::clamp(int(centre.y*surface.imagery_height),0,int(surface.imagery_height)-1);
            auto& batch=(*mr)(x,y).a==0?blendIndices:solidIndices;
            batch.insert(batch.end(),indices.begin()+i,indices.begin()+i+3);
        }
        if(blendIndices.empty()){opaque->addChild(draw);return opaque;}
        auto blend=pass(true);
        if(solidIndices.empty())blend->addChild(draw);
        else {
            // VSG packs indices and attributes in the same allocation for
            // VertexIndexDraw. Use two ranges of a single shared buffer.
            auto elements=vsg::uintArray::create(indices.size());
            const auto solidEnd=std::copy(solidIndices.begin(),solidIndices.end(),elements->begin());
            std::copy(blendIndices.begin(),blendIndices.end(),solidEnd);
            draw->assignIndices(elements);
            auto glass=vsg::VertexIndexDraw::create();
            glass->arrays=draw->arrays;
            glass->assignIndices(elements);
            glass->indices=draw->indices;
            glass->firstIndex=solidIndices.size();
            glass->indexCount=blendIndices.size();
            glass->instanceCount=1;
            blend->addChild(glass);
            draw->indexCount=solidIndices.size();
            opaque->addChild(draw);
        }
        auto sorted=vsg::DepthSorted::create();
        sorted->binNumber=10;
        const auto bounds=vsg::visit<vsg::ComputeBounds>(draw).bounds;
        const auto centre=(bounds.min+bounds.max)*.5;
        sorted->bound={centre,vsg::length(bounds.max-bounds.min)*.5};
        sorted->child=blend;
        opaque->addChild(sorted);
        return opaque;
    }
    // The stock Phong material has a narrow plastic specular highlight. Land
    // imagery represents diffuse surface colour; adding that white highlight
    // erases its contrast around the sun and makes mountains look polished.
    // This is a presentation material, never a change to sensor reflectance.
    auto builder=vsg::Builder::create();builder->shaderSet=vsg::createPhongShaderSet();
    auto land=vsg::PhongMaterialValue::create();land->value().diffuse=vsg::vec4(1,1,1,1);
    land->value().specular=vsg::vec4(0,0,0,1);land->value().shininess=1;
    for(auto& binding:builder->shaderSet->descriptorBindings)if(binding.name=="material")binding.data=land;
    auto group=builder->createStateGroup(state);
    if (!group) throw std::runtime_error("cannot create terrain display pipeline");
    group->addChild(draw);
    return group;
}
vsg::ref_ptr<vsg::StateGroup> makeTerrainContextDisplay(const EarthSurfaceSnapshot& source,
    const simcore::geo::GeoAnchor& anchor,const std::vector<vsg::dbox>& windows,std::function<double(double,double)> seaLevel) {
    struct Vertex {vsg::dvec3 p;vsg::vec2 uv;vsg::vec3 normal;};
    struct ContextMesh final : EarthSurfaceSnapshot {
        uint32_t vertex(const Vertex& v) {
            if(vertices_.size()>=4'000'000)throw std::runtime_error("surrounding Earth preview exceeds 4M clipped vertices");
            const auto index=uint32_t(vertices_.size());
            vertices_.push_back(v.p);imagery_uv.push_back(v.uv);surface_normals.push_back(v.normal);
            return index;
        }
        void triangle(uint32_t a,uint32_t b,uint32_t c){indices_.insert(indices_.end(),{a,b,c});}
        void polygon(const std::vector<Vertex>& polygon) {
            for(size_t i=1;i+1<polygon.size();++i) {
                if(vsg::length(vsg::cross(polygon[i].p-polygon[0].p,polygon[i+1].p-polygon[0].p))<1e-12)continue;
                const auto a=vertex(polygon[0]),b=vertex(polygon[i]),c=vertex(polygon[i+1]);
                triangle(a,b,c);
            }
        }
    } mesh;
    mesh.imagery_rgba=source.imagery_rgba;mesh.imagery_width=source.imagery_width;mesh.imagery_height=source.imagery_height;
    const auto transform=vsg::inverse(simcore::geo::makeEnuToWorldMatrix(anchor))*simcore::geo::makeEnuToWorldMatrix(source.anchor);
    auto clip=[](const std::vector<Vertex>& input,int axis,double limit,bool greater,bool boundary=true) {
        std::vector<Vertex> output;if(input.empty())return output;
        auto inside=[&](const Vertex& v){return greater?(boundary?v.p[axis]>=limit:v.p[axis]>limit):(boundary?v.p[axis]<=limit:v.p[axis]<limit);};
        auto previous=input.back();bool old=inside(previous);
        for(const auto& v:input) {
            const bool next=inside(v);
            if(next!=old){const double t=(limit-previous.p[axis])/(v.p[axis]-previous.p[axis]);output.push_back({previous.p+(v.p-previous.p)*t,previous.uv+(v.uv-previous.uv)*float(t),vsg::normalize(previous.normal+(v.normal-previous.normal)*float(t))});}
            if(next)output.push_back(v);previous=v;old=next;
        }
        return output;
    };
    const auto sourceNormals=surfaceNormals(source);
    std::vector<Vertex> points;points.reserve(source.vertices().size());
    for(size_t i=0;i<source.vertices().size();++i) {
        const auto n=transform*vsg::dvec4(vsg::dvec3((*sourceNormals)[i]),0.);
        points.push_back({transform*source.vertices()[i],source.imagery_uv.empty()?vsg::vec2{}:source.imagery_uv[i],vsg::normalize(vsg::vec3(n.x,n.y,n.z))});
    }
    constexpr auto unassigned=std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> indexedVertices(points.size(),unassigned);
    auto addShared=[&](uint32_t index) {
        auto& vertex=indexedVertices[index];
        if(vertex==unassigned)vertex=mesh.vertex(points[index]);
        return vertex;
    };
    for(size_t i=0;i<source.indices().size();i+=3) {
        const auto a=source.indices()[i],b=source.indices()[i+1],c=source.indices()[i+2];
        vsg::dbox bounds;for(const auto index:{a,b,c})bounds.add(points[index].p);
        const auto crosses=[&](const vsg::dbox& window){return bounds.min.x<=window.max.x&&bounds.max.x>=window.min.x&&bounds.min.z<=window.max.z&&bounds.max.z>=window.min.z;};
        if(!seaLevel&&std::none_of(windows.begin(),windows.end(),crosses)) {
            if(vsg::length(vsg::cross(points[b].p-points[a].p,points[c].p-points[a].p))<1e-12)continue;
            const auto aa=addShared(a),bb=addShared(b),cc=addShared(c);
            mesh.triangle(aa,bb,cc);
            continue;
        }
        std::vector<Vertex> original{points[a],points[b],points[c]};
        if(seaLevel){std::vector<Vertex> land;auto previous=original.back();double old=previous.p.y-seaLevel(previous.p.x,previous.p.z);for(const auto& v:original){const double next=v.p.y-seaLevel(v.p.x,v.p.z);if((old>0)!=(next>0)){const double t=old/(old-next);land.push_back({previous.p+(v.p-previous.p)*t,previous.uv+(v.uv-previous.uv)*float(t),vsg::normalize(previous.normal+(v.normal-previous.normal)*float(t))});}if(next>0)land.push_back(v);previous=v;old=next;}original=std::move(land);if(original.size()<3)continue;}
        std::vector<std::vector<Vertex>> pieces{original};
        for(const auto& window:windows) {
            if(!crosses(window))continue;
            std::vector<std::vector<Vertex>> next;
            for(auto interior:pieces) {
                for(int plane=0;plane<4&&!interior.empty();++plane) {
                    const int axis=plane<2?0:2;const bool greater=plane%2==0;
                    const double limit=greater?window.min[axis]:window.max[axis];
                    auto outside=clip(interior,axis,limit,!greater,false);if(outside.size()>=3)next.push_back(std::move(outside));
                    interior=clip(interior,axis,limit,greater);
                }
            }
            pieces=std::move(next);
        }
        for(const auto& piece:pieces)mesh.polygon(piece);
    }
    return makeTerrainDisplay(mesh);
}
}
