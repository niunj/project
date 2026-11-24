#include "VoxelModelVoxelizer.h"
#include "VoxelTerrain.h"
#include "VoxelSourceSurface.h"
#include "AssimpVoxelImport.h"
#include <filesystem>
#include <vsg/all.h>
#include <map>
#include <cctype>
#include <fstream>
#include <vsg/nodes/Geometry.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/io/VSG.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/LOD.h>
#include <vsg/nodes/PagedLOD.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/io/FileSystem.h>
#include <vsg/io/Logger.h>
#include <set>
#include <vsgraysim/core/ObjLoader.h>

#include <vsg/nodes/Node.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/nodes/Transform.h>
#include <vsg/io/read.h>
#include <vsg/utils/SharedObjects.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace vsgraysim::earth {

namespace {

/// Collect every triangle of a VSG subtree as world-space vertex triples.
struct MeshTriangleCollector : public vsg::Visitor {
    using vsg::Visitor::apply;

    void apply(vsg::Transform& t) override {
        vsg::dmat4 prev = stack_.empty() ? vsg::dmat4() : stack_.back();
        stack_.push_back(t.transform(prev));
        t.traverse(*this);
        stack_.pop_back();
    }
    void apply(vsg::Node& n) override { n.traverse(*this); }
    void apply(vsg::StateGroup& group) override {
        const auto oldImage=image_,oldNormal=normalImage_,oldMr=mrImage_;const float oldMetal=metallic_,oldRough=roughness_;const auto oldDiffuse=diffuse_;const auto oldEmission=emissive_;const bool oldAppearance=hasAppearance_;
        auto descriptors=[&](const vsg::DescriptorSet* set) {
            if(!set)return;
            for(const auto& d:set->descriptors) {
                if(const auto* tex=d->cast<vsg::DescriptorImage>();tex&&tex->dstBinding==0&&!tex->imageInfoList.empty()) {
                    const auto& info=tex->imageInfoList.front();
                    if(info&&info->imageView&&info->imageView->image){image_=info->imageView->image->data;hasAppearance_=true;}
                }
                if(const auto* tex=d->cast<vsg::DescriptorImage>();tex&&(tex->dstBinding==2||tex->dstBinding==6)&&!tex->imageInfoList.empty()) {
                    const auto& info=tex->imageInfoList.front();if(info&&info->imageView&&info->imageView->image)(tex->dstBinding==2?normalImage_:mrImage_)=info->imageView->image->data;
                }
                if(const auto* buffer=d->cast<vsg::DescriptorBuffer>())for(const auto& info:buffer->bufferInfoList)if(info&&info->data) {
                    if(const auto* mat=info->data->cast<vsg::PhongMaterialValue>()){diffuse_=mat->value().diffuse;emissive_=vsg::vec3(mat->value().emissive.x,mat->value().emissive.y,mat->value().emissive.z);roughness_=std::clamp(std::sqrt(2.f/(mat->value().shininess+2.f)),.08f,1.f);metallic_=0;hasAppearance_=true;}
                    if(const auto* mat=info->data->cast<vsg::PbrMaterialValue>()){diffuse_=mat->value().baseColorFactor;emissive_=vsg::vec3(mat->value().emissiveFactor.x,mat->value().emissiveFactor.y,mat->value().emissiveFactor.z);metallic_=mat->value().metallicFactor;roughness_=mat->value().roughnessFactor;hasAppearance_=true;}
                }
            }
        };
        for(const auto& command:group.stateCommands) {
            if(const auto* bind=command->cast<vsg::BindDescriptorSet>())descriptors(bind->descriptorSet);
            if(const auto* bind=command->cast<vsg::BindDescriptorSets>())for(const auto& set:bind->descriptorSets)descriptors(set);
        }
        const auto previous=topology_;
        for(const auto& command:group.stateCommands)if(const auto* bind=command->cast<vsg::BindGraphicsPipeline>())
            for(const auto& state:bind->pipeline->pipelineStates)if(const auto* input=state->cast<vsg::InputAssemblyState>())topology_=input->topology;
        group.traverse(*this);topology_=previous;image_=oldImage;normalImage_=oldNormal;mrImage_=oldMr;metallic_=oldMetal;roughness_=oldRough;diffuse_=oldDiffuse;emissive_=oldEmission;hasAppearance_=oldAppearance;
    }
    void apply(vsg::LOD& lod) override {
        if(lod.children.empty())return;
        const auto best=std::max_element(lod.children.begin(),lod.children.end(),[](const auto& a,const auto& b){return a.minimumScreenHeightRatio<b.minimumScreenHeightRatio;});
        if(best->node)best->node->accept(*this);
    }
    void apply(vsg::Switch& node) override {for(const auto& child:node.children)if(child.mask&&child.node)child.node->accept(*this);}
    void apply(vsg::PagedLOD& lod) override {
        if(lod.children[0].node){lod.children[0].node->accept(*this);return;}
        if(!lod.filename){if(lod.children[1].node)lod.children[1].node->accept(*this);return;}
        const auto options=lod.options?lod.options:vsg::Options::create();const auto filename=vsg::findFile(lod.filename,options);
        if(!filename)throw std::runtime_error("missing finest VSG PagedLOD asset: "+lod.filename.string());
        const auto canonical=std::filesystem::weakly_canonical(filename.string()).string();
        if(reading_.size()>=32||!reading_.insert(canonical).second)throw std::runtime_error("cyclic/excessively deep VSG paged asset");
        auto child=vsg::read_cast<vsg::Node>(filename,options);if(!child)throw std::runtime_error("cannot import finest VSG PagedLOD asset");
        child->accept(*this);reading_.erase(canonical);
    }
    void apply(vsg::VertexIndexDraw& vid) override {
        if (vid.arrays.empty()) return;
        auto* pos = dynamic_cast<vsg::vec3Array*>(vid.arrays[0]->data.get());
        auto* doubles = dynamic_cast<vsg::dvec3Array*>(vid.arrays[0]->data.get());
        if (!pos&&!doubles)throw std::invalid_argument("voxel importer requires vec3/dvec3 positions");
        const size_t size=pos?pos->size():doubles->size();
        const vsg::dmat4 m = stack_.empty() ? vsg::dmat4() : stack_.back();
        const auto* uv=vid.getObject<vsg::vec2Array>("voxel.import.uv");
        const auto* normals=vid.getObject<vsg::vec3Array>("voxel.import.normal");
        if(!normals&&vid.arrays.size()>1)normals=vid.arrays[1]->data->cast<vsg::vec3Array>();
        const auto normalMatrix=vsg::transpose(vsg::inverse(conversion*m));
        const auto* colors=vid.getObject<vsg::vec4Array>("voxel.import.color");
        if(!uv&&vid.arrays.size()>2)uv=vid.arrays[2]->data->cast<vsg::vec2Array>();
        if(!colors&&vid.arrays.size()>3)colors=vid.arrays[3]->data->cast<vsg::vec4Array>();
        vsg::ref_ptr<vsg::Data> texture(vid.getObject<vsg::Data>("voxel.import.image"));if(!texture)texture=image_;
        bool appearance=hasAppearance_||bool(colors)||bool(texture);vid.getValue("voxel.import.appearance",appearance);
        auto add = [&](uint32_t index) {
            if(triangles_.size()>=6'000'000)throw std::runtime_error("voxel importer exceeds two million triangles");
            const auto v=pos?vsg::dvec3((*pos)[index]):(*doubles)[index];
            const vsg::dvec3 w = conversion * (m * v);
            if(!std::isfinite(vsg::length(w)))throw std::invalid_argument("nonfinite voxel model vertex");
            bounds.min.x = std::min(bounds.min.x, w.x);
            bounds.min.y = std::min(bounds.min.y, w.y);
            bounds.min.z = std::min(bounds.min.z, w.z);
            bounds.max.x = std::max(bounds.max.x, w.x);
            bounds.max.y = std::max(bounds.max.y, w.y);
            bounds.max.z = std::max(bounds.max.z, w.z);
            triangles_.push_back(w);
            VertexSurface attr;attr.emissive=emissive_;vid.getValue("voxel.import.emissive",attr.emissive);attr.metallic=metallic_;attr.roughness=roughness_;vid.getValue("voxel.import.metallic",attr.metallic);vid.getValue("voxel.import.roughness",attr.roughness);attr.normalImage=normalImage_;attr.mrImage=mrImage_;attr.image=texture;
            if(auto* data=vid.getObject<vsg::Data>("voxel.import.normal_image"))attr.normalImage=data;
            if(auto* data=vid.getObject<vsg::Data>("voxel.import.mr_image"))attr.mrImage=data;attr.color=diffuse_;attr.present=appearance;
            vid.getValue("voxel.import.diffuse",attr.color);
            if(colors&&!colors->empty()){const auto& c=(*colors)[colors->size()==1?0:index];for(int j=0;j<4;++j)attr.color[j]*=c[j];}
            if(normals&&!normals->empty()) {const auto n=normalMatrix*vsg::dvec4(vsg::dvec3((*normals)[normals->size()==1?0:index]),0.);attr.normal=vsg::normalize(vsg::vec3(n.x,n.y,n.z));}
            if(uv&&index<uv->size())attr.uv=(*uv)[index];
            attributes.push_back(attr);
        };
        std::vector<uint32_t> sequence;
        if (vid.indices && vid.indices->data) {
            const auto* u16=vid.indices->data->cast<vsg::ushortArray>();const auto* u32=vid.indices->data->cast<vsg::uintArray>();const auto* u8=vid.indices->data->cast<vsg::ubyteArray>();
            const size_t n=u16?u16->size():u32?u32->size():u8?u8->size():0;const auto end=uint64_t(vid.firstIndex)+vid.indexCount;
            if(end>n)throw std::invalid_argument("voxeliser index buffer out of range");
            for(uint64_t i=vid.firstIndex;i<end;++i){const int64_t index=int64_t(u16?(*u16)[i]:u32?(*u32)[i]:(*u8)[i])+vid.vertexOffset;
                if(index<0||uint64_t(index)>=size)throw std::invalid_argument("invalid voxeliser mesh index");sequence.push_back(uint32_t(index));}
        } else {sequence.resize(size);for(size_t i=0;i<size;++i)sequence[i]=uint32_t(i);}
        if(topology_==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
            if(sequence.size()%3)throw std::invalid_argument("voxelizer requires complete triangles");
            for(auto index:sequence)add(index);
        } else if(topology_==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP||topology_==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) {
            for(size_t i=2;i<sequence.size();++i) {
                const uint32_t a=topology_==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN?sequence[0]:sequence[i-2];
                if(topology_==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP&&i%2){add(sequence[i-1]);add(a);}else{add(a);add(sequence[i-1]);}add(sequence[i]);
            }
        } else throw std::invalid_argument("voxelizer requires a triangle surface topology");
    }

    void apply(vsg::Geometry& geometry) override {
        for(const auto& command:geometry.commands) {
            auto draw=vsg::VertexIndexDraw::create();draw->arrays=geometry.arrays;draw->indices=geometry.indices;
            if(const auto* indexed=command->cast<vsg::DrawIndexed>()) {
                if(indexed->instanceCount!=1)throw std::invalid_argument("bake instancing before voxel import");
                draw->indexCount=indexed->indexCount;draw->firstIndex=indexed->firstIndex;draw->vertexOffset=indexed->vertexOffset;
            } else if(const auto* arrays=command->cast<vsg::Draw>()) {
                if(arrays->instanceCount!=1)throw std::invalid_argument("bake instancing before voxel import");
                auto indices=vsg::uintArray::create(arrays->vertexCount);
                for(uint32_t j=0;j<arrays->vertexCount;++j)(*indices)[j]=arrays->firstVertex+j;
                draw->assignIndices(indices);draw->indexCount=arrays->vertexCount;
            } else throw std::invalid_argument("unsupported VSG geometry draw command");
            apply(*draw);
        }
    }
    struct VertexSurface {vsg::vec3 normal{},emissive{};vsg::vec2 uv{};vsg::vec4 color{1,1,1,1};vsg::ref_ptr<vsg::Data> image,normalImage,mrImage;float metallic=0,roughness=.65;bool present=false;};
    std::vector<VertexSurface> attributes;
    vsg::dmat4 conversion;
    std::vector<vsg::dvec3> triangles_;  ///< world-space vertex triples (one triangle per 3 entries)
    vsg::dbox bounds;
private:
    std::vector<vsg::dmat4> stack_;
    std::set<std::string> reading_;
    vsg::ref_ptr<vsg::Data> image_,normalImage_,mrImage_;float metallic_=0,roughness_=.65;vsg::vec4 diffuse_{1,1,1,1};vsg::vec3 emissive_{};bool hasAppearance_=false;
    VkPrimitiveTopology topology_=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

// Closest point barycentrics, including all triangle edge/vertex regions.
vsg::dvec3 closestWeights(const vsg::dvec3& p,const vsg::dvec3& a,const vsg::dvec3& b,const vsg::dvec3& c) {
    const auto ab=b-a,ac=c-a,ap=p-a;const double d1=vsg::dot(ab,ap),d2=vsg::dot(ac,ap);
    if(d1<=0&&d2<=0)return {1,0,0};
    const auto bp=p-b;const double d3=vsg::dot(ab,bp),d4=vsg::dot(ac,bp);if(d3>=0&&d4<=d3)return {0,1,0};
    const double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0){double v=d1/(d1-d3);return {1-v,v,0};}
    const auto cp=p-c;const double d5=vsg::dot(ab,cp),d6=vsg::dot(ac,cp);if(d6>=0&&d5<=d6)return {0,0,1};
    const double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0){double w=d2/(d2-d6);return {1-w,0,w};}
    const double va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){double w=(d4-d3)/((d4-d3)+(d5-d6));return {0,1-w,w};}
    const double den=1/(va+vb+vc),v=vb*den,w=vc*den;return {1-v-w,v,w};
}
/// Complete 13-axis separating-axis triangle/box test.
bool triangleAabb(const vsg::dvec3& a, const vsg::dvec3& b, const vsg::dvec3& c,
                  const vsg::dvec3& boxMin, const vsg::dvec3& boxMax) {
    // Early reject: triangle completely outside the box on any axis.
    const double amin = std::min({a.x,b.x,c.x}), amax = std::max({a.x,b.x,c.x});
    const double bmin = std::min({a.y,b.y,c.y}), bmax = std::max({a.y,b.y,c.y});
    const double cmin = std::min({a.z,b.z,c.z}), cmax = std::max({a.z,b.z,c.z});
    if (amax < boxMin.x || amin > boxMax.x) return false;
    if (bmax < boxMin.y || bmin > boxMax.y) return false;
    if (cmax < boxMin.z || cmin > boxMax.z) return false;
    // Early accept: any vertex inside the box.
    for (const auto& p : {a, b, c})
        if (p.x >= boxMin.x && p.x <= boxMax.x &&
            p.y >= boxMin.y && p.y <= boxMax.y &&
            p.z >= boxMin.z && p.z <= boxMax.z) return true;
    // Separating-axis test on the 13 SAT axes.
    const vsg::dvec3 centre = (boxMin + boxMax) * 0.5;
    const vsg::dvec3 half = (boxMax - boxMin) * 0.5;
    const vsg::dvec3 v0 = a - centre, v1 = b - centre, v2 = c - centre;
    const vsg::dvec3 e0 = v1 - v0, e1 = v2 - v1, e2 = v0 - v2;
    const vsg::dvec3 axes[13] = {
        {1,0,0},{0,1,0},{0,0,1},
        vsg::cross(e0,{1,0,0}),vsg::cross(e0,{0,1,0}),vsg::cross(e0,{0,0,1}),
        vsg::cross(e1,{1,0,0}),vsg::cross(e1,{0,1,0}),vsg::cross(e1,{0,0,1}),
        vsg::cross(e2,{1,0,0}),vsg::cross(e2,{0,1,0}),vsg::cross(e2,{0,0,1}),
        vsg::cross(e0,e1),
    };
    for (const auto& axis : axes) {
        const double l = vsg::length(axis);
        if (l < 1e-12) continue;
        const vsg::dvec3 n = axis / l;
        const double r = half.x*std::abs(n.x) + half.y*std::abs(n.y) + half.z*std::abs(n.z);
        const double p0 = vsg::dot(v0, n), p1 = vsg::dot(v1, n), p2 = vsg::dot(v2, n);
        const double mn = std::min({p0,p1,p2}), mx = std::max({p0,p1,p2});
        if (mn > r || mx < -r) return false;
    }
    return true;
}

vsg::ref_ptr<vsg::Node> loadMeshFile(const std::string& path) {
    auto extension=std::filesystem::path(path).extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if (extension==".osg" || extension==".osgb" || extension==".ive" || extension==".osgt") {
        auto converted = std::filesystem::path(path).replace_extension(".vsgb");
        if (std::filesystem::is_regular_file(converted)) return loadMeshFile(converted.string());
        throw std::runtime_error("Convert legacy OSG/IVE to GLB, OBJ or VSG before importing: "+path);
    }
    if (extension==".gltf" || extension==".glb" || extension==".fbx" || extension==".dae" ||
        extension==".ply" || extension==".stl" || extension==".3ds") return loadAssimpVoxelMesh(path);
    // Preserve the native OBJ loader's material-slot contract.
    if (extension == ".obj") {
        std::vector<vsgraysim::ObjMaterialSlot> materials;
        auto node=vsgraysim::loadObjNode(path,vsg::Options::create(),&materials).cast<vsg::Group>();
        if(!node||materials.size()!=node->children.size())throw std::runtime_error("failed to load OBJ geometry and material slots: "+path);
        std::map<std::filesystem::path,vsg::ref_ptr<vsg::Data>> textures;
        for(size_t i=0;i<materials.size();++i) {
            const auto group=node->children[i].cast<vsg::StateGroup>();
            const auto draw=group&&!group->children.empty()?group->children.front().cast<vsg::VertexIndexDraw>():nullptr;
            if(!draw)throw std::runtime_error("OBJ material slot has no triangle geometry: "+path);
            const auto& material=materials[i];
            draw->setValue("voxel.import.diffuse",material.diffuse);
            draw->setValue("voxel.import.appearance",true);
            if(material.map_kd.empty())continue;
            const auto texture=(std::filesystem::path(path).parent_path()/material.map_kd).lexically_normal();
            auto [entry,inserted]=textures.try_emplace(texture);
            if(inserted) {
                entry->second=loadVoxelTexture(texture.string());
                if(!entry->second)vsg::warn("voxel OBJ uses diffuse colour because it could not read ",texture.string());
            }
            if(entry->second)draw->setObject("voxel.import.image",entry->second);
        }
        return node;
    }
    auto opts = vsg::Options::create();
    opts->sharedObjects = vsg::SharedObjects::create();
    opts->paths.push_back(std::filesystem::absolute(path).parent_path().string());
    vsg::ref_ptr<vsg::Node> node;
    if(extension==".vsg") {
        opts->extensionHint=".vsgt";
        auto object=vsg::VSG::create()->read(path,opts);if(object)node=object.cast<vsg::Node>();
    } else node=vsg::read_cast<vsg::Node>(path,opts);
    if (!node) throw std::runtime_error("failed to load mesh: " + path);
    return node;
}

} // namespace

VoxelModel voxelizeMesh(vsg::Node* mesh, const VoxelModelConfig& config) {
    if (!mesh) throw std::invalid_argument("voxelizeMesh: null mesh");
    if(config.maxDimension<1||config.maxDimension>512||!std::isfinite(config.voxelSize)||config.voxelSize<0||
       !std::isfinite(config.margin)||config.margin<0||!std::isfinite(config.unitsToMetres)||config.unitsToMetres<=0||
       (config.upAxis!="y"&&config.upAxis!="z")||config.workers<1||config.workers>8)throw std::invalid_argument("invalid voxel model configuration");
    MeshTriangleCollector collector;
    collector.conversion=vsg::scale(config.unitsToMetres,config.unitsToMetres,config.unitsToMetres);
    if(config.upAxis=="z")collector.conversion=collector.conversion*vsg::rotate(-vsg::PI*.5,vsg::dvec3(1,0,0));
    mesh->accept(collector);
    if (collector.triangles_.empty() || collector.bounds.min.x > collector.bounds.max.x)
        throw std::runtime_error("voxelizeMesh: mesh has no geometry");

    const vsg::dvec3 min = collector.bounds.min - vsg::dvec3(config.margin, config.margin, config.margin);
    const vsg::dvec3 max = collector.bounds.max + vsg::dvec3(config.margin, config.margin, config.margin);
    const vsg::dvec3 extent = max - min;
    const double maxDim = std::max({extent.x, extent.y, extent.z});
    if (maxDim <= 0) throw std::runtime_error("voxelizeMesh: degenerate model bounds");

    double cell = config.voxelSize > 0 ? config.voxelSize
        : maxDim / std::max(1, config.maxDimension);
    // Automatic quality requests are upper bounds. Pick the finest isotropic
    // grid that fits the existing 4M-cell budget; explicit metre sizes stay exact.
    if(config.voxelSize==0)for(int detail=config.maxDimension;detail>=1;--detail) {
        const double candidate=maxDim/detail;
        const double count=std::ceil(extent.x/candidate)*std::ceil(extent.y/candidate)*std::ceil(extent.z/candidate);
        if(count<=4'000'000){cell=candidate;break;}
    }
    if(!std::isfinite(cell)||cell<=0||maxDim/cell>4'000'000)throw std::invalid_argument("invalid voxel model cell size");
    const int nx = std::max(1, int(std::ceil(extent.x / cell)));
    const int ny = std::max(1, int(std::ceil(extent.y / cell)));
    const int nz = std::max(1, int(std::ceil(extent.z / cell)));
    if (int64_t(nx) * ny * nz > 4'000'000) throw std::runtime_error("voxel model exceeds 4M cells");

    const size_t volume=size_t(nx)*ny*nz;
    std::vector<bool> occupied(volume,false);
    struct Candidate {size_t triangle;double score;};
    std::map<uint64_t,Candidate> candidates;
    std::map<uint64_t,Candidate> cornerCandidates;
    std::vector<detail::SourcePatch> sourcePatches;size_t sourceVertices=0;
    auto index=[&](int x,int y,int z){return (size_t(z)*ny+y)*nx+x;};
    // Rasterize each triangle only into its bounded cell range. Testing all
    // mesh triangles against every empty voxel scales poorly on real assets.
    for(size_t i=0;i+2<collector.triangles_.size();i+=3) {
        const auto& a=collector.triangles_[i];const auto& b=collector.triangles_[i+1];const auto& c=collector.triangles_[i+2];
        if(vsg::length(vsg::cross(b-a,c-a))<1e-15)continue;
        int lo[3],hi[3];const int dims[3]={nx,ny,nz};
        for(int axis=0;axis<3;++axis) {
            lo[axis]=std::clamp(int(std::ceil((std::min({a[axis],b[axis],c[axis]})-min[axis])/cell))-1,0,dims[axis]-1);
            hi[axis]=std::clamp(int(std::floor((std::max({a[axis],b[axis],c[axis]})-min[axis])/cell)),0,dims[axis]-1);
        }
        for(int z=lo[2];z<=hi[2];++z)for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x) {
            const auto at=index(x,y,z);
            const auto boxMin=min+vsg::dvec3(x,y,z)*cell;
            if(!triangleAabb(a,b,c,boxMin,boxMin+vsg::dvec3(cell,cell,cell)))continue;
            occupied[at]=true;
            if(config.sourceSurface){
                auto polygon=detail::clipSourceTriangle(a,b,c,boxMin,cell);
                bool duplicate=false;for(int axis=0;axis<3;++axis)if((axis==0?x:axis==1?y:z)>0&&!polygon.empty()&&std::all_of(polygon.begin(),polygon.end(),[&](const auto& p){return std::abs(p[axis]-boxMin[axis])<cell*1e-11;}))duplicate=true;
                if(!duplicate&&!polygon.empty()){sourceVertices+=polygon.size();if(sourceVertices>8'000'000)throw std::runtime_error("source surface preparation exceeds bounded 8M clipping vertices");sourcePatches.push_back({uint32_t(at),i,std::move(polygon)});}
                continue;
            }
            if(!collector.attributes[i].present)continue;
            const auto n=vsg::normalize(vsg::cross(b-a,c-a));
            for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                const auto p=min+vsg::dvec3(x+dx,y+dy,z+dz)*cell;const auto w=closestWeights(p,a,b,c);const auto nearest=a*w.x+b*w.y+c*w.z;
                const auto key=(uint64_t(z+dz)*(ny+1)+y+dy)*(nx+1)+x+dx;const double score=vsg::length(p-nearest);
                auto found=cornerCandidates.find(key);if(found==cornerCandidates.end()||score<found->second.score)cornerCandidates[key]={i,score};
            }
            for(int face=0;face<6;++face) {
                auto centre=boxMin+vsg::dvec3(cell*.5,cell*.5,cell*.5);centre[face/2]+=(face%2?1:-1)*cell*.5;
                const auto e0=b-a,e1=c-a,d=centre-a;
                const double aa=vsg::dot(e0,e0),ab=vsg::dot(e0,e1),bb=vsg::dot(e1,e1),den=aa*bb-ab*ab;
                double w1=(bb*vsg::dot(d,e0)-ab*vsg::dot(d,e1))/den,w2=(aa*vsg::dot(d,e1)-ab*vsg::dot(d,e0))/den;
                double w0=std::max(0.,1-w1-w2);w1=std::max(0.,w1);w2=std::max(0.,w2);const double total=w0+w1+w2;
                const auto closest=(a*w0+b*w1+c*w2)/total;
                const double score=vsg::length(centre-closest)+cell*(1.-std::abs(n[face/2]))*.15;
                const uint64_t key=uint64_t(at)*6+face;auto old=candidates.find(key);
                if(old==candidates.end()||score<old->second.score)candidates[key]={i,score};
            }
        }
    }
    if(config.fillInterior) {
        std::vector<bool> outside(volume,false);std::vector<size_t> queue;
        auto visit=[&](int x,int y,int z){
            if(x<0||y<0||z<0||x>=nx||y>=ny||z>=nz)return;
            const auto at=index(x,y,z);if(occupied[at]||outside[at])return;
            outside[at]=true;queue.push_back(at);
        };
        for(int z=0;z<nz;++z)for(int y=0;y<ny;++y){visit(0,y,z);visit(nx-1,y,z);}
        for(int z=0;z<nz;++z)for(int x=0;x<nx;++x){visit(x,0,z);visit(x,ny-1,z);}
        for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){visit(x,y,0);visit(x,y,nz-1);}
        for(size_t head=0;head<queue.size();++head) {
            const auto at=queue[head];const int x=at%nx,y=(at/nx)%ny,z=at/(size_t(nx)*ny);
            visit(x-1,y,z);visit(x+1,y,z);visit(x,y-1,z);visit(x,y+1,z);visit(x,y,z-1);visit(x,y,z+1);
        }
        for(size_t at=0;at<volume;++at)if(!outside[at])occupied[at]=true;
    }
    const size_t count=std::count(occupied.begin(),occupied.end(),true);
    if(!count)throw std::runtime_error("voxelizeMesh: no nondegenerate triangles");
    auto terrain=VoxelTerrain::fromOccupancy(min,cell,nx,ny,nz,occupied,false);

    if(config.sourceSurface){
        // The original appearance contract stores exterior paint, not rooms
        // buried within solid filled occupancy. Preserve all patches in the
        // one-cell exterior neighbourhood, including diagonal thin features.
        sourcePatches.erase(std::remove_if(sourcePatches.begin(),sourcePatches.end(),[&](const auto& p){int x=p.cell%nx,y=p.cell/nx%ny,z=p.cell/(size_t(nx)*ny);for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int a=x+dx,b=y+dy,c=z+dz;if(a<0||b<0||c<0||a>=nx||b>=ny||c>=nz||!occupied[index(a,b,c)])return false;}return true;}),sourcePatches.end());
    }
    if(config.sourceSurface)terrain->setAppearance(detail::sourceAppearance(collector,std::move(sourcePatches),min,cell,{nx,ny,nz},occupied,config.workers));
    else if(!candidates.empty()) {
        auto appearance=std::make_shared<VoxelSurfaceAppearance>();appearance->origin=min;appearance->cellSize=cell;appearance->dimensions={nx,ny,nz};
        // Keep ONLY original exterior faces. New fracture cuts reveal neutral material.
        for(auto it=candidates.begin();it!=candidates.end();) {
            const auto at=it->first/6;const int face=it->first%6;
            int p[3]={int(at%nx),int(at/nx%ny),int(at/(size_t(nx)*ny))};p[face/2]+=face%2?1:-1;
            if(p[0]>=0&&p[1]>=0&&p[2]>=0&&p[0]<nx&&p[1]<ny&&p[2]<nz&&occupied[index(p[0],p[1],p[2])])it=candidates.erase(it);else ++it;
        }
        // Only exterior corners are moved. A shared grid-corner key ensures
        // neighbouring quads and later fracture cuts agree exactly on their seam.
        for(const auto& [key,candidate]:candidates) {
            const auto at=key/6;const int face=key%6,axis=face/2,side=face%2?1:-1;
            int corner[3]={int(at%nx),int(at/nx%ny),int(at/(size_t(nx)*ny))};if(side>0)++corner[axis];
            int u=(axis+1)%3,v=(axis+2)%3;if(side<0)std::swap(u,v);
            for(const auto& offset:std::array<std::array<int,2>,4>{{{0,0},{1,0},{1,1},{0,1}}}) {
                int p[3]={corner[0],corner[1],corner[2]};p[u]+=offset[0];p[v]+=offset[1];const auto id=appearance->cornerKey(p[0],p[1],p[2]);
                const auto found=cornerCandidates.find(id);if(found==cornerCandidates.end())continue;
                const size_t i=found->second.triangle;const auto a=collector.triangles_[i],b=collector.triangles_[i+1],c=collector.triangles_[i+2];
                const auto point=min+vsg::dvec3(p[0],p[1],p[2])*cell,w=closestWeights(point,a,b,c);
                auto normal=collector.attributes[i].normal*float(w.x)+collector.attributes[i+1].normal*float(w.y)+collector.attributes[i+2].normal*float(w.z);
                if(vsg::length(normal)<1e-8)normal=vsg::vec3(vsg::normalize(vsg::cross(b-a,c-a)));
                appearance->corners[id]={a*w.x+b*w.y+c*w.z,vsg::normalize(normal)};
            }
        }
        const int tile=VoxelSurfaceAppearance::tileSize;
        const uint32_t columns=uint32_t(std::ceil(std::sqrt(candidates.size()+1.)));
        appearance->width=columns*tile;appearance->height=uint32_t((candidates.size()+1+columns-1)/columns)*tile;
        if(uint64_t(appearance->width)*appearance->height*4>64*1024*1024)throw std::runtime_error("target surface atlas exceeds 64 MiB");
        appearance->rgba.resize(size_t(appearance->width)*appearance->height*4);
        for(size_t p=0;p<appearance->rgba.size();p+=4){appearance->rgba[p]=74;appearance->rgba[p+1]=77;appearance->rgba[p+2]=80;appearance->rgba[p+3]=255;}
        appearance->mr_rgba.resize(appearance->rgba.size());
        const bool hasNormal=std::any_of(collector.attributes.begin(),collector.attributes.end(),[](const auto& a){return bool(a.normalImage);});
        if(hasNormal)appearance->normal_rgba.resize(appearance->rgba.size());
        const bool hasEmission=std::any_of(collector.attributes.begin(),collector.attributes.end(),[](const auto& a){return vsg::length(a.emissive)>0;});if(hasEmission)appearance->emissive_rgba.resize(appearance->rgba.size(),0);
        for(size_t p=0;p<appearance->rgba.size();p+=4) {
            appearance->mr_rgba[p]=255;appearance->mr_rgba[p+1]=204;appearance->mr_rgba[p+2]=178;appearance->mr_rgba[p+3]=255;
            if(hasNormal){appearance->normal_rgba[p]=128;appearance->normal_rgba[p+1]=128;appearance->normal_rgba[p+2]=255;appearance->normal_rgba[p+3]=255;}
        }
        auto rawSample=[](const vsg::Data* data,vsg::vec2 uv)->vsg::vec4 {
            const auto* image=data?data->cast<vsg::ubvec4Array2D>():nullptr;
            if(!image)return {1,1,1,1};
            const int x=std::clamp(int((uv.x-std::floor(uv.x))*image->width()),0,int(image->width())-1),y=std::clamp(int((uv.y-std::floor(uv.y))*image->height()),0,int(image->height())-1);
            return vsg::vec4((*image)(x,y))/255.f;
        };
        auto linear=[](double c){return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);};
        uint32_t slot=1;
        for(const auto& [key,candidate]:candidates) {
            appearance->faceTiles[key]=slot;
            const auto at=key/6;const int face=key%6,axis=face/2,side=face%2?1:-1;
            int corner[3]={int(at%nx),int(at/nx%ny),int(at/(size_t(nx)*ny))};if(side>0)++corner[axis];
            int u=(axis+1)%3,v=(axis+2)%3;if(side<0)std::swap(u,v);
            const auto i=candidate.triangle;const auto a=collector.triangles_[i],e0=collector.triangles_[i+1]-a,e1=collector.triangles_[i+2]-a;
            const double aa=vsg::dot(e0,e0),ab=vsg::dot(e0,e1),bb=vsg::dot(e1,e1),den=aa*bb-ab*ab;
            // These four canonical positions are constant over the entire
            // atlas tile. Resolve source corners once, preserving every texel
            // while avoiding four tree lookups per sample on detailed assets.
            auto p0=min+vsg::dvec3(corner[0],corner[1],corner[2])*cell;std::array<vsg::dvec3,4> points{p0,p0,p0,p0};
            points[1][u]+=cell;points[2][u]+=cell;points[2][v]+=cell;points[3][v]+=cell;
            for(auto& p:points)if(const auto* source=appearance->corner(p))p=source->position;
            for(int y=0;y<tile;++y)for(int x=0;x<tile;++x) {
                const double su=std::clamp((x-.5)/8.,0.,1.),sv=std::clamp((y-.5)/8.,0.,1.);
                const auto p=points[0]*((1-su)*(1-sv))+points[1]*(su*(1-sv))+points[2]*(su*sv)+points[3]*((1-su)*sv);
                const auto weights3=closestWeights(p,a,a+e0,a+e1);float weights[3]={float(weights3.x),float(weights3.y),float(weights3.z)};vsg::vec2 uv{};vsg::vec4 color{};
                for(int j=0;j<3;++j){uv+=collector.attributes[i+j].uv*weights[j];color+=collector.attributes[i+j].color*weights[j];}
                const auto* image=collector.attributes[i].image.get();
                vsg::vec4 sampled(1,1,1,1);
                if(image) {
                    const auto* pixels=image->cast<vsg::ubvec4Array2D>();
                    if(pixels) {
                        const double px=(uv.x-std::floor(uv.x))*pixels->width()-.5,py=(uv.y-std::floor(uv.y))*pixels->height()-.5;
                        const int ix=int(std::floor(px)),iy=int(std::floor(py));sampled=vsg::vec4(0,0,0,1);
                        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                            const int sx=(ix+dx+int(pixels->width()))%pixels->width(),sy=(iy+dy+int(pixels->height()))%pixels->height();
                            const auto texel=(*pixels)(sx,sy);const float weight=float((dx?px-ix:1-px+ix)*(dy?py-iy:1-py+iy));
                            for(int j=0;j<3;++j)sampled[j]+=float(linear(texel[j]/255.))*weight;
                        }
                    }
                }
                const size_t dest=4*(size_t(slot/columns*tile+y)*appearance->width+slot%columns*tile+x);
                for(int j=0;j<3;++j){const double value=std::clamp(double(sampled[j]*color[j]),0.,1.);appearance->rgba[dest+j]=uint8_t(std::lround(255*(value<=.0031308?value*12.92:1.055*std::pow(value,1/2.4)-.055)));}
                appearance->rgba[dest+3]=255;
                const auto& attr=collector.attributes[i];if(hasEmission){for(int channel=0;channel<3;++channel)appearance->emissive_rgba[dest+channel]=uint8_t(std::clamp(attr.emissive[channel],0.f,1.f)*255+.5f);appearance->emissive_rgba[dest+3]=255;}const auto mr=attr.mrImage?rawSample(attr.mrImage,uv):vsg::vec4(1,1,1,1);
                appearance->mr_rgba[dest]=255;appearance->mr_rgba[dest+1]=uint8_t(std::clamp(attr.roughness*mr.g,.04f,1.f)*255+.5f);appearance->mr_rgba[dest+2]=uint8_t(std::clamp(attr.metallic*mr.b,0.f,1.f)*255+.5f);appearance->mr_rgba[dest+3]=255;
                if(hasNormal&&attr.normalImage) {
                    const auto sample=rawSample(attr.normalImage,uv);vsg::dvec3 bump(sample.x*2-1,sample.y*2-1,sample.z*2-1);
                    auto n=vsg::dvec3(attr.normal)*double(weights[0])+vsg::dvec3(collector.attributes[i+1].normal)*double(weights[1])+vsg::dvec3(collector.attributes[i+2].normal)*double(weights[2]);
                    if(vsg::length(n)<1e-8)n=vsg::cross(e0,e1);n=vsg::normalize(n);
                    const auto duv0=collector.attributes[i+1].uv-attr.uv,duv1=collector.attributes[i+2].uv-attr.uv;
                    auto tangent=e0*double(duv1.y)-e1*double(duv0.y);if(vsg::length(tangent)<1e-10)tangent=e0;
                    tangent=vsg::normalize(tangent);const auto bitangent=-vsg::normalize(vsg::cross(n,tangent));const auto world=vsg::normalize(tangent*bump.x+bitangent*bump.y+n*bump.z);
                    auto t=points[1]-points[0];t-=n*vsg::dot(t,n);if(vsg::length(t)<1e-8)t=tangent;t=vsg::normalize(t);const auto b=-vsg::normalize(vsg::cross(n,t));
                    const vsg::dvec3 encoded(vsg::dot(world,t),vsg::dot(world,b),vsg::dot(world,n));
                    for(int c=0;c<3;++c)appearance->normal_rgba[dest+c]=uint8_t(std::clamp(encoded[c]*.5+.5,0.,1.)*255+.5);appearance->normal_rgba[dest+3]=255;
                }
            }
            ++slot;
        }
        terrain->setAppearance(appearance);
    }
    VoxelModel result;
    result.terrain = terrain;
    result.origin = min;
    result.cellSize = cell;
    result.nx = nx; result.ny = ny; result.nz = nz;
    result.occupiedCount = count;
    return result;
}

VoxelModel voxelizeFile(const std::string& path, const VoxelModelConfig& config) {
    auto mesh = loadMeshFile(path);
    return voxelizeMesh(mesh, config);
}

} // namespace vsgraysim::earth
