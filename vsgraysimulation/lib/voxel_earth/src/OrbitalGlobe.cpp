#include <vsgraysim/earth/OrbitalGlobe.h>
#include "EarthShader.h"
#include <simcore/GeoFrame.h>
#include <stdexcept>
#include <vsg/core/Array2D.h>
#include <vsg/utils/LineSegmentIntersector.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/utils/GraphicsPipelineConfigurator.h>
#include <cmath>
#include <cstring>
#include <numeric>

namespace vsgraysim::earth {
namespace {
vsg::vec4 footprint(const OrbitalGlobe::TileKey& tile) {
    if (tile[0]<0 || tile[0]>22) throw std::invalid_argument("invalid globe tile level");
    const int rows=1<<tile[0];
    if (tile[1]<0 || tile[1]>=2*rows || tile[2]<0 || tile[2]>=rows)
        throw std::invalid_argument("invalid globe tile address");
    return {float(tile[1]-1)/(2*rows),3.f/(2*rows),
            float(std::max(0,tile[2]-1))/rows,float(std::min(rows,tile[2]+2))/rows};
}
}

std::optional<double> OrbitalSurface::top(double lat,double lon) const {
    if(!side || side>256 || heights.size()!=2ULL*side*side) return {};
    if(!std::isfinite(lat) || std::abs(lat)>90 || !std::isfinite(lon)) return {};
    double u=std::fmod(lon+180.,360.);
    if(u<0) u+=360.;
    const auto i=std::min(unsigned(u*side/180.),2*side-1);
    const auto j=std::min(unsigned((90.-lat)*side/180.),side-1);
    const double h=heights[j*2*side+i];
    return std::isfinite(h) ? std::optional<double>(h) : std::nullopt;
}

OrbitalGlobe::OrbitalGlobe(std::shared_ptr<const OrbitalSurface> data,
                         std::shared_ptr<rockyenv::CloudGpuResources> cloud) : source(std::move(data)) {
    if(!cloud)cloud=rockyenv::createCloudGpuResources();
    if (!source || !source->side || source->side>256 || !source->texture_side || source->texture_side>256 ||
        source->heights.size()!=2ULL*source->side*source->side ||
        source->rgba.size()!=8ULL*source->texture_side*source->texture_side)
        throw std::invalid_argument("invalid orbital voxel grid or imagery dimensions");
    const unsigned n=source->side,t=source->texture_side;
    // Curvature tessellation stays independent of the resolution of the measured volume.
    const unsigned subdivisions=(96+n-1)/n, segments=n*subdivisions;
    std::vector<vsg::vec3> vertices,normals;
    std::vector<vsg::vec2> coords;
    vertices.reserve(8ULL*segments*segments);
    coords.reserve(vertices.capacity());
    const auto height=[&](unsigned col,unsigned row) {
        return source->heights[(row/subdivisions)*2*n+(col%(2*segments))/subdivisions];
    };
    // Triangles are accumulated per source column so the final index order can
    // be height-sorted (low columns first). At orbital zoom this lets the
    // early depth test reject the hidden fills of tall continental columns
    // instead of shading them first and overdrawing everything in front.
    std::vector<std::vector<uint32_t>> columnElements(2ULL*n*n);
    const auto heightAt=[&](unsigned col,unsigned row) {
        return source->heights[row*2*n+(col%(2*n))];
    };
    const auto append=[&](unsigned x,unsigned y,double h) {
        const double u=double(x)/(2*segments),v=double(y)/segments;
        coords.emplace_back(float(u),float(v));
        vertices.emplace_back(simcore::geo::geodeticToECEF(90-180*v,u*360-180,h));
        normals.emplace_back(simcore::geo::geoUp(90-180*v,u*360-180));
    };
    for(unsigned row=0;row<segments;++row) for(unsigned col=0;col<2*segments;++col) {
        const double h=height(col,row);
        if(!std::isfinite(h)) continue;
        const auto first=uint32_t(vertices.size());
        append(col,row,h);append(col+1,row,h);
        append(col,row+1,h);append(col+1,row+1,h);
        auto& out=columnElements[(row/subdivisions)*2*n+(col/subdivisions)%(2*n)];
        if(row!=0) out.insert(out.end(),{first,first+2,first+1});
        if(row+1!=segments) out.insert(out.end(),{first+1,first+2,first+3});
        // Join unequal column tops once at each shared boundary, including the date line.
        auto wall=[&](uint32_t a,uint32_t b,unsigned ax,unsigned ay,unsigned bx,unsigned by,double neighbour) {
            if(!std::isfinite(neighbour) || h==neighbour) return;
            uint32_t next=vertices.size();
            append(ax,ay,neighbour);append(bx,by,neighbour);
            // Wall vertices need their own geometric normals. Reusing the
            // cap normals incorrectly illuminates vertical cliffs as ground.
            const auto normal=vsg::normalize(vsg::cross(vertices[b]-vertices[a],vertices[next+1]-vertices[a]));
            const auto topA=uint32_t(vertices.size());vertices.push_back(vertices[a]);coords.push_back(coords[a]);normals.push_back(normal);
            const auto topB=uint32_t(vertices.size());vertices.push_back(vertices[b]);coords.push_back(coords[b]);normals.push_back(normal);
            normals[next]=normals[next+1]=normal;
            out.insert(out.end(),{topA,topB,next+1,topA,next+1,next});
        };
        wall(first+1,first+3,col+1,row,col+1,row+1,height(col+1,row));
        if(row+1<segments) wall(first+3,first+2,col+1,row+1,col,row+1,height(col,row+1));
    }
    // Emit columns ordered by ascending top height (NaN/empty columns first).
    std::vector<uint32_t> order(2ULL*n*n);
    std::iota(order.begin(),order.end(),0u);
    std::stable_sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){
        const double ha=heightAt(a%(2*n),a/(2*n)),hb=heightAt(b%(2*n),b/(2*n));
        const double va=std::isfinite(ha)?ha:-1e9,vb=std::isfinite(hb)?hb:-1e9;
        return va<vb;
    });
    std::vector<uint32_t> elements;
    elements.reserve(vertices.capacity()*3/2);
    for(const auto column:order) {
        auto& quads=columnElements[column];
        elements.insert(elements.end(),quads.begin(),quads.end());
    }
    auto positions=vsg::vec3Array::create(vertices.size());
    auto uv=vsg::vec2Array::create(coords.size());
    auto normalArray=vsg::vec3Array::create(normals.size());std::copy(normals.begin(),normals.end(),normalArray->begin());
    std::copy(vertices.begin(),vertices.end(),positions->begin());
    std::copy(coords.begin(),coords.end(),uv->begin());
    auto texture=vsg::ubvec4Array2D::create(2*t,t);
    std::memcpy(texture->dataPointer(),source->rgba.data(),source->rgba.size());
    texture->properties.format=VK_FORMAT_R8G8B8A8_SRGB;
    auto shaders=vsg::ShaderSet::create(vsg::ShaderStages{
        earthShader("voxel_globe.vert"), earthShader("voxel_globe.frag")});
    shaders->addAttributeBinding("position","",0,VK_FORMAT_R32G32B32_SFLOAT,{});
    shaders->addAttributeBinding("uv","",1,VK_FORMAT_R32G32_SFLOAT,{});
    shaders->addAttributeBinding("normal","",2,VK_FORMAT_R32G32B32_SFLOAT,{});
    shaders->addDescriptorBinding("imagery","",0,0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("windows","",0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addPushConstantRange("camera","",VK_SHADER_STAGE_VERTEX_BIT,0,128);
    shaders->addDescriptorBinding("cloudMap","",0,2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("cloudDetail","",0,3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("sparseCloudPages","",0,4,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("sparseCloudAtlas","",0,5,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("sparseCloud","",0,6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("gpuCloudState","",0,7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("gpuCloud","",0,8,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    auto pipeline=vsg::GraphicsPipelineConfigurator::create(shaders);
    vsg::DataList arrays;
    pipeline->assignArray(arrays,"position",VK_VERTEX_INPUT_RATE_VERTEX,positions);
    pipeline->assignArray(arrays,"uv",VK_VERTEX_INPUT_RATE_VERTEX,uv);
    pipeline->assignArray(arrays,"normal",VK_VERTEX_INPUT_RATE_VERTEX,normalArray);
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod=VK_LOD_CLAMP_NONE;
    pipeline->assignTexture("imagery",texture,sampler);
    pipeline->assignTexture("cloudMap",planetCloudTexture(),sampler);
    auto detailSampler=vsg::Sampler::create();
    detailSampler->addressModeU=detailSampler->addressModeV=detailSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    detailSampler->maxLod=VK_LOD_CLAMP_NONE;
    pipeline->assignTexture("cloudDetail",planetCloudDetailTexture(),detailSampler);
    auto sparseSampler=vsg::Sampler::create();
    sparseSampler->addressModeU=sparseSampler->addressModeV=sparseSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sparseSampler->maxLod=0;
    auto pageSampler=vsg::Sampler::create();
    pageSampler->addressModeU=pageSampler->addressModeV=pageSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pageSampler->magFilter=pageSampler->minFilter=VK_FILTER_NEAREST;pageSampler->maxLod=0;
    pipeline->assignTexture("sparseCloudPages",planetSparseCloudPageTable(),pageSampler);
    pipeline->assignTexture("sparseCloudAtlas",planetSparseCloudAtlas(),sparseSampler);
    pipeline->assignDescriptor("sparseCloud",planetSparseCloudParameters());
    pipeline->assignTexture("gpuCloudState",vsg::ImageInfoList{cloud->image});
    pipeline->assignDescriptor("gpuCloud",cloud->layout);
    windows_=vsg::vec4Array::create(7,vsg::vec4{});
    (*windows_)[4]=vsg::vec4(1,1,0,0);
    (*windows_)[2]=vsg::vec4{};
    windows_->properties.dataVariance=vsg::DYNAMIC_DATA;
    pipeline->assignDescriptor("windows",windows_);
    // The orbital shaders have one fixed interface and are already compiled.
    shaders->variants.emplace(pipeline->shaderHints, shaders->stages);
    pipeline->init();
    node=vsg::StateGroup::create();
    pipeline->copyTo(node);
    auto draw=vsg::VertexIndexDraw::create();
    draw->assignArrays(arrays);
    auto indices=vsg::uintArray::create(elements.size());
    std::copy(elements.begin(),elements.end(),indices->begin());
    draw->assignIndices(indices);
    draw->indexCount=elements.size();
    draw->instanceCount=1;
    node->addChild(draw);
}

void OrbitalGlobe::setWeather(const PlanetWeatherFrame& f) {
    (*windows_)[5]=planetAdvection(f);
    (*windows_)[6]=vsg::vec4(f.clouds?f.cover:0.f,f.opticalDepth,f.cloudBase,f.cloudHeight);
    windows_->dirty();
}
void OrbitalGlobe::setLighting(const vsg::vec3& moon,float phase,float exposure,float turbidity) {
    (*windows_)[3]=vsg::vec4(moon,phase);
    (*windows_)[4]=vsg::vec4(exposure,turbidity,0,0);
    windows_->dirty();
}
void OrbitalGlobe::setSun(const vsg::vec3& direction) {
    auto value=vsg::vec4(direction,1.f);
    if(value!=(*windows_)[2]) { (*windows_)[2]=value;windows_->dirty(); }
}
bool OrbitalGlobe::intersect(const vsg::dvec3& origin,const vsg::dvec3& direction,vsg::dvec3& hit) const {
    auto ray=vsg::LineSegmentIntersector::create(origin,origin+direction*1e8);
    node->accept(*ray);
    double best=2.;
    for(const auto& candidate:ray->intersections) {
        if(candidate->ratio>=best || candidate->arrays.size()<2) continue;
        auto* texcoord=candidate->arrays[1]->cast<vsg::vec2Array>();
        if(!texcoord) continue;
        vsg::dvec2 uv{};
        for(const auto& vertex:candidate->indexRatios) uv+=vsg::dvec2((*texcoord)[vertex.index])*vertex.ratio;
        bool hidden=false;
        for(unsigned i=0;i<2;++i) {
            const auto r=(*windows_)[i];
            const double x=uv.x-r.x;
            hidden|=(x-std::floor(x)<r.y && uv.y>=r.z && uv.y<r.w);
        }
        if(!hidden) {best=candidate->ratio;hit=candidate->worldIntersection;}
    }
    return best<=1.;
}
void OrbitalGlobe::setWindows(const TileKey& active, std::optional<TileKey> observer) {
    (*windows_)[0]=footprint(active);
    (*windows_)[1]=observer?footprint(*observer):vsg::vec4{};
    windows_->dirty();
}
}
