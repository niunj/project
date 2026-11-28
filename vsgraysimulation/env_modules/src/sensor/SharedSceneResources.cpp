/// @file SharedSceneResources.cpp —— P0 场景级资源池实现。
///
/// 几何抽取逻辑上移自 BandRT.cpp / RadarChain.cpp 原各自重复实现 (同一
/// VertexIndexDraw 契约), 池化后全场景一次。SSBO/UBO 走 vsg BufferInfo
/// 共享语义: 首次 compile (viewer->compile() 的 Context) 分配 device buffer
/// 并上传, 其后所有引用同一 BufferInfo 的 DescriptorBuffer 复用同一 VkBuffer。
#include "rocky_env/SharedSceneResources.h"

#include <vsg/all.h>
#include <vsg/raytracing/BottomLevelAccelerationStructure.h>
#include <vsg/raytracing/TopLevelAccelerationStructure.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numeric>
#include <stdexcept>

namespace rockyenv {
namespace sensor {

// ===========================================================================
// 几何抽取 + 共享资源构建 (P0.1 / P0.2 / P0.3 静态部分)
// ===========================================================================
SharedSceneResources::Result SharedSceneResources::build(
        vsg::Window& window,
        vsg::ref_ptr<vsg::Node> scene,
        const std::vector<RadarMaterial>* materials,
        const TexArraySpec* texSpec) { return build(*window.getOrCreateDevice(), scene, materials, texSpec); }

SharedSceneResources::Result SharedSceneResources::build(
        vsg::Device& deviceArg,
        vsg::ref_ptr<vsg::Node> scene,
        const std::vector<RadarMaterial>* materials,
        const TexArraySpec* texSpec) {
    return buildResources(deviceArg,scene,materials,texSpec,nullptr,nullptr);
}

SharedSceneResources::Result SharedSceneResources::buildResources(
        vsg::Device& deviceArg,vsg::ref_ptr<vsg::Node> scene,const std::vector<RadarMaterial>* materials,
        const TexArraySpec* texSpec,const std::vector<SceneInstance>* instances,const SharedSceneResources* previous) {
    Result result;
    stats_.geometryBuilds++;
    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);
    if(previous==this||(previous&&previous->resourceDevice_!=device))previous=nullptr;
    resourceDevice_=device;

    // ---- 场景抽取: 单 VertexIndexDraw 契约 (binding0 pos / 1 props / 2 mat_idx) ----
    auto vid = scene ? scene->cast<vsg::VertexIndexDraw>() : nullptr;
    if (!vid || vid->arrays.size() < 2) {
        error_ = result.error = "scene 非 VertexIndexDraw 契约 (arrays<2)";
        return result;
    }
    const auto posArr = vid->arrays[0]->data->cast<vsg::vec3Array>();
    const auto propsArr = vid->arrays[1]->data->cast<vsg::vec2Array>();
    if (!posArr || !propsArr) {
        error_ = result.error = "顶点契约不符 (期望 vec3 pos + vec2 props)";
        return result;
    }
    const vsg::ref_ptr<vsg::Data> posData = vid->arrays[0]->data;    // BLAS 顶点直传
    const uint32_t nVerts = uint32_t(posArr->size());

    // indices: uint 提升 (ushort 场景拷贝转换)
    vsg::ref_ptr<vsg::uintArray> idxU32;
    if (vid->indices && vid->indices->data) {
        if (auto* u32 = vid->indices->data->cast<vsg::uintArray>()) {
            idxU32 = u32;
        } else if (auto* u16 = vid->indices->data->cast<vsg::ushortArray>()) {
            idxU32 = vsg::uintArray::create(u16->size());
            for (uint32_t i = 0; i < u16->size(); ++i) (*idxU32)[i] = (*u16)[i];
        }
    }
    if (!idxU32 || idxU32->size() < 3) {
        error_ = result.error = "索引数组缺失 (<3)";
        return result;
    }

    // mat_idx: binding2 缺失 (程序化场景) → 全 0 缺省材质
    vsg::ref_ptr<vsg::uintArray> matArr;
    if (vid->arrays.size() >= 3) {
        if (auto* m = vid->arrays[2]->data->cast<vsg::uintArray>()) matArr = m;
    }
    if (!matArr) {
        matArr = vsg::uintArray::create(nVerts);
        for (auto& m : *matArr) m = 0;
    }

    // pos 平铺 float (std430 vec3 数组 16B stride 陷阱规避)
    auto posFlat = vsg::floatArray::create(size_t(nVerts) * 3);
    std::memcpy(posFlat->dataPointer(), posArr->dataPointer(), size_t(nVerts) * sizeof(vsg::vec3));

    // 材质表 SSBO (批次4: 每材质 3 vec4 — d=介电锚点/sigma0/粗糙,
    // m=(rcs_lut_id, dielectric_model, 0, 0))。
    // 无表 → 单一缺省地形材质 (同 RadarChain 现行为: rchit εr≈0 回退双保险;
    // BandRT rchit 不消费材质表, 内容无影响)。
    const uint32_t nMats = materials ? std::max<uint32_t>(1, uint32_t(materials->size())) : 1;
    auto matData = vsg::floatArray::create(size_t(nMats) * 12);
    if (materials && !materials->empty()) {
        for (uint32_t m = 0; m < materials->size(); ++m) {
            const auto& r = (*materials)[m];
            float* d = matData->data(uint32_t(m) * 12);
            d[0] = r.dielectric_real; d[1] = r.dielectric_imag;
            d[2] = r.sigma0_db;       d[3] = r.surface_rough;
            d[4] = float(r.rcs_lut_id);
            d[5] = float(r.dielectric_model);   // 批次4: 介电模型 0/1/2/3
            d[6] = r.terrain ? 1.f : 0.f;
        }
    } else {
        float* d = matData->data(0);
        d[0] = 6.0f; d[1] = 0.0f; d[2] = -15.0f; d[3] = 0.3f;
        d[4] = -1.0f; d[5] = 0.0f;   // 模型 0 = 固定值
    }

    // 谱材质表 SSBO (批次1, 每材质 4 vec4, 与雷达材质表同序同数):
    //   a = (ρ_tv, ρ_swir, ε_mwir, ε_lwir)   b = (镜面强度, GGX 粗糙度, 水标记, 0); c/d = 热响应及热源
    // 推导规则 (0/负 = JSON 未指定):
    //   ε_band = eps_band > 0 ? eps_band : emissivity (单值镜像)
    //   ρ_band = rho_band > 0 ? rho_band : 1-ε_band (Kirchhoff, opaque 表面)
    //   水标记 (#4): dielectric_model==1 (海水 Debye 弛豫) → b.z=1, band_shade
    //   据此启用逐像素解析波法线/白沫 —— 非水材质恒 0 恒等退化。
    // 无表 → 单一缺省地形材质 (ε=0.92 → ρ_tv/swir=0.08, 朗伯)。
    {
        auto specData = vsg::floatArray::create(size_t(nMats) * 16);
        const RadarMaterial defMat{};
        for (uint32_t m = 0; m < nMats; ++m) {
            const RadarMaterial& r = (materials && m < materials->size())
                                         ? (*materials)[m] : defMat;
            const float epsBase = r.emissivity > 0.0f ? r.emissivity : 0.90f;
            const float eMw = r.eps_mwir >= 0.0f ? r.eps_mwir : -1.0f;
            const float eLw = r.eps_lwir >= 0.0f ? r.eps_lwir : -1.0f;
            float* s = specData->data(uint32_t(m) * 16);
            s[0] = r.rho_tv >= 0.0f ? r.rho_tv : 1.0f - epsBase;
            s[1] = r.rho_swir >= 0.0f ? r.rho_swir : 1.0f - epsBase;
            s[2] = eMw;
            s[3] = eLw;
            s[4] = r.spec_strength;
            s[5] = r.spec_roughness;
            s[6] = (r.dielectric_model == 1) ? 1.0f : 0.0f;
            s[7] = 0.0f;
            std::copy(r.thermal_surface.begin(), r.thermal_surface.end(), s + 8);
            std::copy(r.thermal_source.begin(), r.thermal_source.end(), s + 12);
        }
        spectralDataInfo = vsg::BufferInfo::create(specData);
    }

    // ---- 共享 SSBO BufferInfo (首次 compile 分配上传, 之后复用) ----
    posFlatInfo = vsg::BufferInfo::create(posFlat);
    indicesInfo = vsg::BufferInfo::create(idxU32);
    matIdxInfo = vsg::BufferInfo::create(matArr);
    matDataInfo = vsg::BufferInfo::create(matData);
    propsInfo = vsg::BufferInfo::create(vid->arrays[1]->data);   // props 直传引用
    const auto* terrainNormals = vid->arrays.size()>5 ? vid->arrays[5]->data->cast<vsg::vec4Array>() : nullptr;
    thermalNormalInfo = vsg::BufferInfo::create(terrainNormals && terrainNormals->size()==nVerts
        ? vid->arrays[5]->data : vsg::ref_ptr<vsg::Data>(vsg::vec4Array::create(1,vsg::vec4{})));

    // ---- TV 彩色: uv (binding3) / 贴图槽号 (binding4) 抽取 (缺失 → 零填充, 兼容程序化场景) ----
    {
        vsg::ref_ptr<vsg::vec2Array> uvArr;
        if (vid->arrays.size() >= 4)
            if (auto* u = vid->arrays[3]->data->cast<vsg::vec2Array>()) uvArr = u;
        vsg::ref_ptr<vsg::uintArray> slotArr;
        if (vid->arrays.size() >= 5)
            if (auto* s = vid->arrays[4]->data->cast<vsg::uintArray>()) slotArr = s;
        if (uvArr && uvArr->size() == nVerts) {
            uvFlatInfo = vsg::BufferInfo::create(uvArr);         // vec2 std430 无对齐陷阱, 直传
        } else {
            auto zeroUv = vsg::vec2Array::create(nVerts);
            for (auto& u : *zeroUv) u = vsg::vec2(0.0f, 0.0f);
            uvFlatInfo = vsg::BufferInfo::create(zeroUv);
        }
        if (slotArr && slotArr->size() == nVerts) {
            slotInfo = vsg::BufferInfo::create(slotArr);
        } else {
            auto zeroSlot = vsg::uintArray::create(nVerts);
            for (auto& s : *zeroSlot) s = 0;
            slotInfo = vsg::BufferInfo::create(zeroSlot);
        }
    }

    // ---- TV 彩色: 槽表 (Kd.rgb, hasTex) SSBO + map_Kd 贴图 texture2DArray ----
    // 层 i ↔ 槽 i+1 (槽 0 = 无贴图缺省, hasTex=0 → shader 走 Kd 纯色分支);
    // 失败槽由工具侧填 Kd 色占位层, 保证 层=槽-1 恒成立。
    texSlotDataInfo={};texArrayView={};texLayerCount=0;textureSlots_.clear();stats_.textureArrayReused=false;
    if (texSpec && !texSpec->slotData.empty()) {
        textureSlots_=texSpec->slotData;
        const auto reuseTexture=[&] {
            if(!previous||!previous->texArrayView||textureSlots_!=previous->textureSlots_||
               texSpec->layers.size()!=previous->texLayerCount)return false;
            const auto& image=*previous->texArrayView->image;
            if(!image.data)return false;
            const auto* bytes=static_cast<const uint8_t*>(image.data->dataPointer());
            const size_t size=size_t(image.extent.width)*image.extent.height*4;
            for(size_t i=0;i<texSpec->layers.size();++i) {
                const auto& layer=texSpec->layers[i];
                if(!layer||layer->width()!=image.extent.width||layer->height()!=image.extent.height||layer->dataSize()!=size||
                   std::memcmp(bytes+i*size,layer->dataPointer(),size)!=0)return false;
            }
            return true;
        };
        if(reuseTexture()) {
            texSlotDataInfo=previous->texSlotDataInfo;texArrayView=previous->texArrayView;
            texLayerCount=previous->texLayerCount;stats_.textureArrayReused=true;
        } else {
        auto slotData = vsg::floatArray::create(texSpec->slotData.size() * 4);
        for (uint32_t s = 0; s < texSpec->slotData.size(); ++s) {
            float* d = slotData->data(s * 4);
            d[0] = texSpec->slotData[s].r;
            d[1] = texSpec->slotData[s].g;
            d[2] = texSpec->slotData[s].b;
            d[3] = texSpec->slotData[s].a;   // hasTex
        }
        texSlotDataInfo = vsg::BufferInfo::create(slotData);
        if (!texSpec->layers.empty()) {
            const uint32_t nL = uint32_t(texSpec->layers.size());
            if(!texSpec->layers.front())throw std::invalid_argument("missing sensor texture layer");
            const uint32_t tw = texSpec->layers.front()->width();
            const uint32_t th = texSpec->layers.front()->height();
            auto arr = vsg::ubvec4Array3D::create(tw, th, nL);   // 每元素 RGBA8 4B
            arr->properties.format = VK_FORMAT_R8G8B8A8_UNORM;
            for (uint32_t l = 0; l < nL; ++l) {
                const auto& ld = texSpec->layers[l];
                if(!ld||ld->width()!=tw||ld->height()!=th||ld->dataSize()!=size_t(tw)*th*4)
                    throw std::invalid_argument("inconsistent sensor texture layer extent");
                std::memcpy(reinterpret_cast<unsigned char*>(arr->dataPointer()) + size_t(l) * tw * th * 4,
                            ld->dataPointer(), size_t(tw) * th * 4);
            }
            auto img = vsg::Image::create();
            img->imageType = VK_IMAGE_TYPE_2D;
            img->format = VK_FORMAT_R8G8B8A8_UNORM;
            img->extent = VkExtent3D{tw, th, nL};
            img->mipLevels = 1;
            img->arrayLayers = nL;
            img->samples = VK_SAMPLE_COUNT_1_BIT;
            img->tiling = VK_IMAGE_TILING_OPTIMAL;
            // TRANSFER_DST 必须保留: 缺失时 VSG 上传跳过 → 采样恒 0 (与 LUT 同坑)
            img->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            img->data = arr;
            texArrayView = vsg::ImageView::create(img);
            texArrayView->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            texLayerCount = nL;
            std::printf("[SharedScene] 贴图数组: %u 层 (%ux%u RGBA8, %.1f MB)\n",
                        nL, tw, th, double(nL) * tw * th * 4.0 / (1024.0 * 1024.0));
        }
        }
    }

    // ---- 单 BLAS + 单实例 TLAS (恒等变换; 视角由 rgen push 下发) ----
    sceneGeometry_=vid;
    if(instances)installInstances(*device,*instances,previous);
    else installInstances(*device,{{"scene",vsg::ref_ptr<vsg::VertexIndexDraw>(vid),vsg::dmat4(),true}},previous);

    // ---- P0.2 sampler 共享 (clamp + linear, 与 BandRT/SensorPipeline 同配置) ----
    sampler = vsg::Sampler::create();
    sampler->magFilter = VK_FILTER_LINEAR;
    sampler->minFilter = VK_FILTER_LINEAR;
    sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    // ---- P0.3 共享环境 UBO (每帧一次 setEnv; 数值去重) ----
    // 8 float: [σ/km, sky, Esun, 0, sunDir.x, sunDir.y, sunDir.z, 0]
    // (批次1 扩容: 后 4 float 太阳 ENU 指向, band_shade.comp 太阳反射消费)
    envData_ = vsg::floatArray::create(8);
    for (auto& f : *envData_) f = 0.0f;
    envData_->properties.dataVariance = vsg::DYNAMIC_DATA;
    envInfo = vsg::BufferInfo::create(envData_);

    vertexCount = nVerts;
    triangleCount = uint32_t(idxU32->size() / 3);
    materialCount = nMats;

    ready_ = true;
    std::printf("[SharedScene] 资源池就绪: %u 顶点/%u 三角/%u 材质 | "
                "加速块新建 %u / 复用 %u\n",vertexCount,triangleCount,materialCount,
                stats_.accelerationBuilt,stats_.accelerationReused);
    result.ok = true;
    return result;
}

// ===========================================================================
// P0.2 波段 LUT 纹理缓存 (同波段参数只建一份 device 纹理)
// ===========================================================================
namespace {
std::vector<uint32_t> meshIndices(vsg::VertexIndexDraw& mesh) {
    std::vector<uint32_t> indices;
    auto* u32=mesh.indices->data->cast<vsg::uintArray>();
    auto* u16=mesh.indices->data->cast<vsg::ushortArray>();
    const auto size=u32 ? u32->size() : u16 ? u16->size() : 0;
    if(!size || mesh.firstIndex+uint64_t(mesh.indexCount)>size || mesh.indexCount%3)
        throw std::invalid_argument("invalid thermal mesh index range");
    indices.reserve(mesh.indexCount);
    for(uint64_t i=mesh.firstIndex;i<uint64_t(mesh.firstIndex)+mesh.indexCount;++i) {
        const int64_t index=int64_t(u32 ? (*u32)[i] : (*u16)[i])+mesh.vertexOffset;
        const auto* positions=mesh.arrays[0]->data->cast<vsg::vec3Array>();
        if(index<0 || uint64_t(index)>=positions->size()) throw std::invalid_argument("thermal mesh vertex index out of bounds");
        indices.push_back(uint32_t(index));
    }
    return indices;
}
class Refit : public vsg::Inherit<vsg::Command,Refit> {
public:
    explicit Refit(vsg::ref_ptr<DynamicTlas> tlas):tlas_(tlas) {}
    void compile(vsg::Context& context) override { tlas_->compile(context); }
    void record(vsg::CommandBuffer& command) const override {
        auto update=tlas_->updateCommand();
        if(!update) throw std::logic_error("dynamic TLAS was not compiled");
        update->record(command);
    }
private: vsg::ref_ptr<DynamicTlas> tlas_;
};
}
SharedSceneResources::Result SharedSceneResources::buildInstances(vsg::Device& device,
        const std::vector<SceneInstance>& instances,const std::vector<RadarMaterial>& materials,const TexArraySpec* textures,
        const SharedSceneResources* previous) {
    if(instances.empty()) {
        // Vulkan descriptors and acceleration structures still need valid
        // storage after every target has been destroyed. A masked, zero-area
        // primitive is internal storage only; callers retain zero instances.
        auto draw=vsg::VertexIndexDraw::create();
        draw->assignArrays({vsg::vec3Array::create(3,vsg::vec3(0,0,0)),
            vsg::vec2Array::create(3,vsg::vec2(0,0)),vsg::uintArray::create(3,0u),
            vsg::vec2Array::create(3,vsg::vec2(0,0)),vsg::uintArray::create(3,0u)});
        draw->assignIndices(vsg::uintArray::create({0,1,2}));draw->indexCount=3;draw->instanceCount=1;
        const auto fallback=materials.empty()?std::vector<RadarMaterial>(1):materials;
        auto result=buildInstances(device,{{"",draw,vsg::dmat4(),false}},fallback,textures,previous);
        if(result.ok){instanceCount=0;triangleCount=0;sceneGeometry_->indexCount=0;}
        return result;
    }
    std::vector<vsg::vec3> positions;
    std::vector<vsg::vec2> props,uvs;
    std::vector<vsg::vec4> normals;
    std::vector<uint32_t> materialIds,slots,indices;
    const bool needNormals = std::any_of(instances.begin(),instances.end(),[](const auto& instance) {
        return instance.geometry->arrays.size()>5;
    });
    for(const auto& instance:instances) {
        auto& geometry=*instance.geometry;
        const auto* pos=geometry.arrays.at(0)->data->cast<vsg::vec3Array>();
        const auto* prop=geometry.arrays.at(1)->data->cast<vsg::vec2Array>();
        const auto* mat=geometry.arrays.at(2)->data->cast<vsg::uintArray>();
        if(!pos || !prop || !mat || prop->size()!=pos->size() || mat->size()!=pos->size())
            throw std::invalid_argument("invalid instance thermal vertex arrays");
        const auto* uv=geometry.arrays.size()>3?geometry.arrays[3]->data->cast<vsg::vec2Array>():nullptr;
        const auto* slot=geometry.arrays.size()>4?geometry.arrays[4]->data->cast<vsg::uintArray>():nullptr;
        const auto* shading=geometry.arrays.size()>5?geometry.arrays[5]->data->cast<vsg::vec4Array>():nullptr;
        if((uv&&uv->size()!=pos->size())||(slot&&slot->size()!=pos->size()))throw std::invalid_argument("invalid instance texture arrays");
        if(shading&&shading->size()!=pos->size())throw std::invalid_argument("invalid instance thermal normal array");
        const auto offset=uint32_t(positions.size());
        for(auto index:meshIndices(geometry)) indices.push_back(offset+index);
        for(std::size_t i=0;i<pos->size();++i) {
            if((*mat)[i]>=materials.size()) throw std::invalid_argument("GPU material index out of bounds");
            positions.push_back((*pos)[i]); props.push_back((*prop)[i]); materialIds.push_back((*mat)[i]);
            const auto textureSlot=slot?(*slot)[i]:0u;
            if(textureSlot&&(!textures||textureSlot>=textures->slotData.size()))throw std::invalid_argument("GPU texture slot index out of bounds");
            uvs.push_back(uv?(*uv)[i]:vsg::vec2(0,0));slots.push_back(textureSlot);
            if(needNormals) normals.push_back(shading?(*shading)[i]:vsg::vec4{});
        }
    }
    auto draw=vsg::VertexIndexDraw::create();
    auto pos=vsg::vec3Array::create(positions.size()); auto prop=vsg::vec2Array::create(props.size());
    auto mat=vsg::uintArray::create(materialIds.size()); auto uv=vsg::vec2Array::create(uvs.size()); auto slot=vsg::uintArray::create(slots.size());
    auto index=vsg::uintArray::create(indices.size());
    std::copy(positions.begin(),positions.end(),pos->begin()); std::copy(props.begin(),props.end(),prop->begin());
    std::copy(materialIds.begin(),materialIds.end(),mat->begin()); std::copy(uvs.begin(),uvs.end(),uv->begin());
    std::copy(slots.begin(),slots.end(),slot->begin()); std::copy(indices.begin(),indices.end(),index->begin());
    if (needNormals) {
        auto shading=vsg::vec4Array::create(normals.size());
        std::copy(normals.begin(),normals.end(),shading->begin());
        draw->assignArrays({pos,prop,mat,uv,slot,shading});
    } else draw->assignArrays({pos,prop,mat,uv,slot});
    draw->assignIndices(index); draw->indexCount=uint32_t(index->size()); draw->instanceCount=1;
    return buildResources(device,draw,&materials,textures,&instances,previous);
}
struct SharedSceneResources::MeshAcceleration {
    vsg::ref_ptr<vsg::vec3Array> vertices;
    std::vector<uint32_t> indices;
    std::vector<GpuBvhNode> nodes;
    std::vector<uint32_t> primitives;
    vsg::ref_ptr<vsg::BottomLevelAccelerationStructure> blas;

    void build(vsg::Device& device,bool hardware) {
        if(hardware) {
            auto localIndices=vsg::uintArray::create(indices.size());
            std::copy(indices.begin(),indices.end(),localIndices->begin());
            auto geometry=vsg::AccelerationGeometry::create();
            geometry->assignVertices(vertices);geometry->assignIndices(localIndices);
            blas=vsg::BottomLevelAccelerationStructure::create(&device);
            blas->geometries.push_back(geometry);
            return;
        }
        const auto triangles=indices.size()/3;
        std::vector<uint32_t> order(triangles);
        std::iota(order.begin(),order.end(),0);
        primitives.reserve(triangles);
        const auto vertex=[&](uint32_t triangle,int corner){return (*vertices)[indices[3*triangle+corner]];};
        const auto centroid=[&](uint32_t triangle){return (vertex(triangle,0)+vertex(triangle,1)+vertex(triangle,2))/3.f;};
        const auto subdivide=[&](const auto& self,size_t begin,size_t end)->uint32_t {
            const auto index=uint32_t(nodes.size());nodes.emplace_back();
            vsg::vec3 lo(1e30f,1e30f,1e30f),hi(-1e30f,-1e30f,-1e30f);
            for(auto i=begin;i<end;++i)for(int corner=0;corner<3;++corner) {
                const auto p=vertex(order[i],corner);
                for(int axis=0;axis<3;++axis){lo[axis]=std::min(lo[axis],p[axis]);hi[axis]=std::max(hi[axis],p[axis]);}
            }
            nodes[index].lo=vsg::vec4(lo,0);nodes[index].hi=vsg::vec4(hi,0);
            if(end-begin<=8) {
                const auto first=uint32_t(primitives.size());
                primitives.insert(primitives.end(),order.begin()+begin,order.begin()+end);
                nodes[index].links=vsg::uivec4(0,0,first,uint32_t(end-begin));
            } else {
                const auto extent=hi-lo;int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;
                const auto middle=begin+(end-begin)/2;
                std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,
                    [&](uint32_t a,uint32_t b){return centroid(a)[axis]<centroid(b)[axis];});
                const auto left=self(self,begin,middle),right=self(self,middle,end);
                nodes[index].links=vsg::uivec4(left,right,0,0);
            }
            return index;
        };
        subdivide(subdivide,0,triangles);
    }
};

void SharedSceneResources::installInstances(vsg::Device& device,const std::vector<SceneInstance>& instances,
                                            const SharedSceneResources* previous) {
    const auto begin=std::chrono::steady_clock::now();
    meshAccelerations_.clear();computeInstanceSpans_.clear();
    stats_.accelerationBuilt=stats_.accelerationReused=0;stats_.accelerationTrianglesBuilt=0;
    std::vector<GpuBvhNode> nodes;
    std::vector<uint32_t> primitives,roots;
    std::vector<GpuSceneInstance> packed;
    dynamicTlas_={};tlas={};bvhInfo={};primitiveInfo={};instanceInfo={};instanceData_={};
    const bool hardware=device.supportsDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)&&
        device.supportsDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    if(hardware)dynamicTlas_=DynamicTlas::create(&device);
    uint32_t triangleBase=0;
    for(const auto& instance:instances) {
        auto& mesh=*instance.geometry;
        const auto* vertices=mesh.arrays[0]->data->cast<vsg::vec3Array>();
        auto indices=meshIndices(mesh);
        const auto triangles=uint32_t(indices.size()/3);
        if(!triangles)throw std::invalid_argument("empty GPU instance geometry");
        const MeshKey key{instance.id,instance.geometryPart};
        std::shared_ptr<const MeshAcceleration> acceleration;
        if(previous) {
            const auto saved=previous->meshAccelerations_.find(key);
            if(saved!=previous->meshAccelerations_.end()) {
                const auto& old=*saved->second;
                if(old.indices==indices&&old.vertices->size()==vertices->size()&&
                   std::equal(old.vertices->begin(),old.vertices->end(),vertices->begin())) acceleration=saved->second;
            }
        }
        if(acceleration)++stats_.accelerationReused;
        else {
            auto built=std::make_shared<MeshAcceleration>();
            built->indices=std::move(indices);built->vertices=vsg::vec3Array::create(vertices->size());
            std::copy(vertices->begin(),vertices->end(),built->vertices->begin());
            built->build(device,hardware);acceleration=std::move(built);
            ++stats_.accelerationBuilt;stats_.accelerationTrianglesBuilt+=triangles;
        }
        meshAccelerations_[key]=acceleration;
        if(hardware) {
            // The SBT's instance index is the current packed triangle offset.
            // A shared BLAS never carries indices from a prior revision's buffers.
            if(triangleBase>0xffffffu||triangles>0x1000000u-triangleBase)
                throw std::invalid_argument("TLAS triangle range exceeds 24-bit instance index");
            auto entry=vsg::GeometryInstance::create();entry->id=triangleBase;entry->mask=instance.visible?255:0;
            entry->flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;entry->shaderOffset=0;
            entry->transform=vsg::mat4(instance.transform);entry->accelerationStructure=acceleration->blas;
            dynamicTlas_->geometryInstances.push_back(entry);
        } else {
            const auto base=uint32_t(nodes.size()),leafBase=uint32_t(primitives.size());
            roots.push_back(base);
            for(auto node:acceleration->nodes) {
                if(node.links.w)node.links.z+=leafBase;
                else {node.links.x+=base;node.links.y+=base;}
                nodes.push_back(node);
            }
            for(const auto primitive:acceleration->primitives)primitives.push_back(triangleBase+primitive);
        }
        triangleBase+=triangles;
    }
    sceneUpdates=vsg::Commands::create();instanceCount=uint32_t(instances.size());
    if(hardware){tlas=dynamicTlas_;sceneUpdates->addChild(Refit::create(dynamicTlas_));}
    else {
        // A small hierarchy of block roots keeps compute traversal at one
        // matrix pair per logical object, even for terrain split into 64 blocks.
        const auto join=[&](const auto& self,size_t first,size_t last)->uint32_t {
            if(last-first==1)return roots[first];
            vsg::vec4 lo=nodes[roots[first]].lo,hi=nodes[roots[first]].hi;
            for(size_t i=first+1;i<last;++i)for(int axis=0;axis<3;++axis) {
                lo[axis]=std::min(lo[axis],nodes[roots[i]].lo[axis]);
                hi[axis]=std::max(hi[axis],nodes[roots[i]].hi[axis]);
            }
            const auto extent=hi-lo;int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;
            const auto middle=first+(last-first)/2;
            std::nth_element(roots.begin()+first,roots.begin()+middle,roots.begin()+last,[&](uint32_t a,uint32_t b) {
                return nodes[a].lo[axis]+nodes[a].hi[axis]<nodes[b].lo[axis]+nodes[b].hi[axis];
            });
            const auto left=self(self,first,middle),right=self(self,middle,last);
            nodes.push_back({lo,hi,{left,right,0,0}});return uint32_t(nodes.size()-1);
        };
        for(size_t first=0;first<instances.size();) {
            const auto& owner=instances[first];size_t end=first+1;
            if(owner.geometryPart)while(end<instances.size()&&instances[end].geometryPart&&instances[end].id==owner.id) {
                if(instances[end].transform!=owner.transform||instances[end].visible!=owner.visible)
                    throw std::invalid_argument("GPU geometry parts must share one object pose and mask");
                ++end;
            }
            packed.push_back({vsg::mat4(owner.transform),vsg::mat4(vsg::inverse(owner.transform)),
                              {join(join,first,end),owner.visible?1u:0u,0,0}});
            computeInstanceSpans_.emplace_back(first,end);first=end;
        }
        auto nodeData=vsg::ubyteArray::create(nodes.size()*sizeof(GpuBvhNode));
        std::memcpy(nodeData->data(),nodes.data(),nodeData->size());
        auto primitiveData=vsg::uintArray::create(primitives.size());
        std::copy(primitives.begin(),primitives.end(),primitiveData->begin());
        instanceData_=vsg::ubyteArray::create(packed.size()*sizeof(GpuSceneInstance));
        std::memcpy(instanceData_->data(),packed.data(),instanceData_->size());
        instanceData_->properties.dataVariance=vsg::DYNAMIC_DATA;
        bvhInfo=vsg::BufferInfo::create(nodeData);primitiveInfo=vsg::BufferInfo::create(primitiveData);
        instanceInfo=vsg::BufferInfo::create(instanceData_);
    }
    stats_.accelerationHostMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
}
void SharedSceneResources::setTransforms(const std::vector<vsg::dmat4>& transforms,const std::vector<bool>& visible) {
    if(transforms.size()!=instanceCount||visible.size()!=instanceCount)throw std::invalid_argument("GPU instance topology mismatch");
    if(!instanceCount)return; // keep the internal empty-scene primitive masked
    if(dynamicTlas_){dynamicTlas_->update(transforms,visible);return;}
    auto* instances=reinterpret_cast<GpuSceneInstance*>(instanceData_->data());
    for(size_t i=0;i<computeInstanceSpans_.size();++i) {
        const auto [first,last]=computeInstanceSpans_[i];
        for(size_t part=first+1;part<last;++part)if(transforms[first]!=transforms[part]||visible[first]!=visible[part])
            throw std::invalid_argument("GPU geometry parts must retain one object pose and mask");
        instances[i].objectToWorld=vsg::mat4(transforms[first]);instances[i].worldToObject=vsg::mat4(vsg::inverse(transforms[first]));
        instances[i].meta.y=visible[first]?1u:0u;
    }
    instanceData_->dirty();
}

vsg::ref_ptr<vsg::ImageView> SharedSceneResources::lutView(const BandRadianceLut& lut) {
    const auto& table = lut.table();
    for (auto& e : lutCache_) {
        if (e.tMin == lut.tMin() && e.tMax == lut.tMax() && e.table.size() == table.size() &&
            std::memcmp(e.table.data(), table.data(), table.size() * sizeof(float)) == 0) {
            return e.view;   // 命中: 同波段复用 (多实例同波段零重复上传)
        }
    }
    constexpr uint32_t kLutSamples = 512;
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = VK_FORMAT_R32_SFLOAT;
    img->extent = VkExtent3D{kLutSamples, 1, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_DST 必须保留: 缺失时 VSG 上传跳过 → 采样恒 0 (历史坑)
    img->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto data = vsg::floatArray2D::create(kLutSamples, 1);
    data->properties.format = VK_FORMAT_R32_SFLOAT;
    for (uint32_t i = 0; i < kLutSamples && i < table.size(); ++i) (*data)(i, 0) = table[i];
    img->data = data;
    auto view = vsg::ImageView::create(img);
    view->viewType = VK_IMAGE_VIEW_TYPE_2D;

    LutCacheEntry e;
    e.tMin = lut.tMin();
    e.tMax = lut.tMax();
    e.table = table;
    e.view = view;
    lutCache_.push_back(e);
    stats_.lutTextures++;
    return view;
}

// ===========================================================================
// P0.3 共享环境 UBO (数值去重 → 每帧一次上传)
// ===========================================================================
void SharedSceneResources::setEnv(const EnvAnswer& env) {
    if (!envData_) return;
    stats_.envSetCalls++;
    // 批次1: 太阳角 → ENU 单位指向 (dirFromAzEl 单一真值换算, SensorApi.h)
    double dir[3];
    dirFromAzEl(env.sun_az_deg, env.sun_el_deg, dir);
    const float next[8] = {float(env.sigma_per_km), float(env.sky_radiance),
                           float(env.sun_irradiance), 0.0f,
                           float(dir[0]), float(dir[1]), float(dir[2]), 0.0f};
    if (envInitialized_ && std::memcmp(lastEnv_, next, sizeof(next)) == 0) {
        return;   // 同帧多链转发/值未变: 不再 dirty (保证每帧仅一次上传)
    }
    std::memcpy(lastEnv_, next, sizeof(next));
    envInitialized_ = true;
    std::memcpy(envData_->dataPointer(), next, sizeof(next));
    envData_->dirty();   // DYNAMIC_DATA: record 时上传 (TransferTask 按 data 去重)
    stats_.envDirtyCount++;
}

void SharedSceneResources::printStats() const {
    if (!ready_) return;
    std::printf("[SharedScene] 统计: 池构建 %u 次 | env 注入 %llu 次 (去重后上传 %llu 次) | "
                "LUT 纹理 %u 份 (缓存去重)\n",
                stats_.geometryBuilds,
                (unsigned long long)stats_.envSetCalls,
                (unsigned long long)stats_.envDirtyCount,
                stats_.lutTextures);
}

} // namespace sensor
} // namespace rockyenv
