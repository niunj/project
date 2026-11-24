#include "OsgVoxelImport.h"
#include <vsg/all.h>
#include <stdexcept>
#if VSGRAYSIM_OSG_IMPORT
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Material>
#include <osg/Texture2D>
#include <osg/LOD>
#include <osg/PagedLOD>
#include <osg/Transform>
#include <osg/TriangleIndexFunctor>
#include <osgDB/ReadFile>
#include <osgDB/FileNameUtils>
#include <algorithm>
#include <filesystem>
#include <set>
#include <mutex>
#include <map>
#include <osgDB/Registry>
namespace vsgraysim::earth {
namespace {
vsg::ref_ptr<vsg::Data> voxelTexture(const osg::Image& image) {
    if(image.s()<=0||image.t()<=0)return {};
    if(uint64_t(image.s())*image.t()>16'777'216)throw std::runtime_error("source texture exceeds 16M pixels");
    auto rgba=vsg::ubvec4Array2D::create(image.s(),image.t());
    for(int y=0;y<image.t();++y)for(int x=0;x<image.s();++x) {
        const auto color=image.getColor(x,y);vsg::ubvec4 pixel;
        for(int c=0;c<4;++c)pixel[c]=uint8_t(std::clamp(color[c],0.f,1.f)*255.f+.5f);
        (*rgba)(x,y)=pixel;
    }
    rgba->properties.format=VK_FORMAT_R8G8B8A8_SRGB;
    return rgba;
}
class Collector;
struct Triangles {
    Collector* collector=nullptr;
    const osg::Vec3Array* floats=nullptr;
    const osg::Vec3dArray* doubles=nullptr;
    osg::Matrixd matrix;
    const osg::Vec2Array* uv=nullptr;
    const osg::Vec3Array* normals=nullptr;bool normalOverall=true;
    float metallic=0,roughness=.65;vsg::vec3 emissive{};
    const osg::Vec4Array* colors=nullptr;
    bool overall=true;
    vsg::vec4 diffuse{1,1,1,1};
    vsg::ref_ptr<vsg::Data> image;
    void operator()(unsigned a,unsigned b,unsigned c);
};
class Collector final:public osg::NodeVisitor {
public:
    std::vector<vsg::dvec3> vertices;
    vsg::ref_ptr<vsg::Group> output=vsg::Group::create();
    osg::ref_ptr<osgDB::Options> options;
    std::set<std::string> reading;
    std::map<const osg::Image*,vsg::ref_ptr<vsg::Data>> images;
    explicit Collector(osgDB::Options* opts):osg::NodeVisitor(TRAVERSE_ALL_CHILDREN),options(opts) {}
    void apply(osg::Geode& node)override {
        for(unsigned i=0;i<node.getNumDrawables();++i) {
            const auto* geometry=node.getDrawable(i)->asGeometry();
            if(!geometry)throw std::runtime_error("OSG voxel import requires triangulatable geometry; convert procedural drawables first");
            osg::TriangleIndexFunctor<Triangles> functor;functor.collector=this;
            functor.floats=dynamic_cast<const osg::Vec3Array*>(geometry->getVertexArray());
            functor.doubles=dynamic_cast<const osg::Vec3dArray*>(geometry->getVertexArray());
            if(!functor.floats&&!functor.doubles)throw std::runtime_error("OSG voxel import requires Vec3 or Vec3d vertices");
            functor.matrix=osg::computeLocalToWorld(getNodePath());
            functor.normals=dynamic_cast<const osg::Vec3Array*>(geometry->getNormalArray());functor.normalOverall=geometry->getNormalBinding()!=osg::Geometry::BIND_PER_VERTEX;
            functor.uv=dynamic_cast<const osg::Vec2Array*>(geometry->getTexCoordArray(0));
            functor.colors=dynamic_cast<const osg::Vec4Array*>(geometry->getColorArray());
            functor.overall=geometry->getColorBinding()!=osg::Geometry::BIND_PER_VERTEX;
            auto state=new osg::StateSet;
            osg::ref_ptr<osg::StateSet> retained=state;
            for(const auto* ancestor:getNodePath())if(ancestor->getStateSet())state->merge(*ancestor->getStateSet());
            if(geometry->getStateSet())state->merge(*geometry->getStateSet());
            if(const auto* mat=dynamic_cast<const osg::Material*>(state->getAttribute(osg::StateAttribute::MATERIAL))) {
                const auto e=mat->getEmission(osg::Material::FRONT);functor.emissive={e.r(),e.g(),e.b()};
                const auto c=mat->getDiffuse(osg::Material::FRONT);functor.diffuse=vsg::vec4(c.r(),c.g(),c.b(),c.a());
                functor.roughness=std::clamp(std::sqrt(2.f/(mat->getShininess(osg::Material::FRONT)+2.f)),.08f,1.f);
            }
            if(const auto* tex=dynamic_cast<const osg::Texture2D*>(state->getTextureAttribute(0,osg::StateAttribute::TEXTURE))) {
                if(const auto* image=tex->getImage();image&&image->s()>0&&image->t()>0) {
                    if(auto found=images.find(image);found!=images.end())functor.image=found->second;
                    else {
                        functor.image=voxelTexture(*image);images[image]=functor.image;
                    }
                }
            }
            geometry->getUserValue("metallicFactor",functor.metallic);geometry->getUserValue("roughnessFactor",functor.roughness);
            geometry->accept(functor);
        }
    }
    void apply(osg::LOD& node)override {
        if(!node.getNumChildren())return;
        unsigned best=0;for(unsigned i=1;i<node.getNumChildren();++i)if(node.getMinRange(i)<node.getMinRange(best))best=i;
        node.getChild(best)->accept(*this);
    }
    void apply(osg::PagedLOD& node)override {
        // Import one finest declared level, never a superposition of LODs.
        unsigned best=0;const unsigned count=std::max(node.getNumChildren(),node.getNumFileNames());if(!count)return;
        for(unsigned i=1;i<count;++i)if(node.getMinRange(i)<node.getMinRange(best))best=i;
        if(best<node.getNumChildren()){node.getChild(best)->accept(*this);return;}
        const auto name=node.getFileName(best);auto path=std::filesystem::path(name);
        if(path.is_relative())path=std::filesystem::path(node.getDatabasePath())/path;
        if(path.is_relative()&&!options->getDatabasePathList().empty())path=std::filesystem::path(options->getDatabasePathList().front())/path;
        const auto canonical=std::filesystem::weakly_canonical(path).string();
        if(reading.size()>=32||!reading.insert(canonical).second)throw std::runtime_error("cyclic/excessively deep OSG paged asset");
        auto child=osgDB::readRefNodeFile(canonical,options);
        if(!child)throw std::runtime_error("missing finest OSG PagedLOD asset: "+canonical);
        child->accept(*this);reading.erase(canonical);
    }
};
void Triangles::operator()(unsigned a,unsigned b,unsigned c) {
    if(collector->vertices.size()+3>6'000'000)throw std::runtime_error("OSG import exceeds two million triangles");
    const size_t size=floats?floats->size():doubles->size();
    auto positions=vsg::dvec3Array::create(3);auto uvArray=vsg::vec2Array::create(3);auto colorArray=vsg::vec4Array::create(3);
    auto normalArray=vsg::vec3Array::create(3);osg::Matrixd normalMatrix;normalMatrix.transpose(osg::Matrixd::inverse(matrix));
    size_t vertex=0;
    for(const auto i:{a,b,c}) {
        if(i>=size)throw std::invalid_argument("OSG triangle index out of range");
        const auto p=(floats?osg::Vec3d((*floats)[i]):(*doubles)[i])*matrix;
        if(!std::isfinite(p.x())||!std::isfinite(p.y())||!std::isfinite(p.z()))throw std::invalid_argument("nonfinite OSG model vertex");
        collector->vertices.emplace_back(p.x(),p.y(),p.z());
        (*positions)[vertex]={p.x(),p.y(),p.z()};
        if(uv&&i<uv->size())(*uvArray)[vertex]={(*uv)[i].x(),(*uv)[i].y()};
        auto color=diffuse;
        if(colors&&!colors->empty()) {const auto& c=(*colors)[overall?0:i];for(int j=0;j<4;++j)color[j]*=c[j];}
        (*colorArray)[vertex]=color;
        if(normals&&!normals->empty()){const auto n=osg::Matrixd::transform3x3((*normals)[normalOverall?0:i],normalMatrix);(*normalArray)[vertex]=vsg::normalize(vsg::vec3(n.x(),n.y(),n.z()));}
        ++vertex;
    }
    auto draw=vsg::VertexIndexDraw::create();draw->assignArrays({positions});
    draw->assignIndices(vsg::uintArray::create({0,1,2}));draw->indexCount=3;draw->instanceCount=1;
    draw->setObject("voxel.import.uv",uvArray);draw->setObject("voxel.import.color",colorArray);
    if(normals)draw->setObject("voxel.import.normal",normalArray);
    draw->setValue("voxel.import.emissive",emissive);draw->setValue("voxel.import.metallic",metallic);draw->setValue("voxel.import.roughness",roughness);
    if(image)draw->setObject("voxel.import.image",image);
    draw->setValue("voxel.import.appearance",bool(image||colors||diffuse!=vsg::vec4(1,1,1,1)||vsg::length(emissive)>0));
    collector->output->addChild(draw);
}
}
void initializeVoxelModelImport() {
    static std::once_flag once;std::call_once(once,[]{osgDB::Registry::instance()->getLibraryFilePathList().push_front(VOXEL_OSG_PLUGIN_DIR);});
}
vsg::ref_ptr<vsg::Data> loadOsgVoxelTexture(const std::string& path) {
    initializeVoxelModelImport();
    const auto image=osgDB::readRefImageFile(path);
    return image?voxelTexture(*image):vsg::ref_ptr<vsg::Data>{};
}
vsg::ref_ptr<vsg::Node> loadOsgVoxelMesh(const std::string& path) {
    initializeVoxelModelImport();
    auto options=new osgDB::Options;options->setDatabasePath(std::filesystem::absolute(path).parent_path().string());
    auto node=osgDB::readRefNodeFile(path,options);
    if(!node)throw std::runtime_error("OSG reader/plugin could not load model: "+path);
    Collector collector(options);node->accept(collector);
    if(collector.vertices.empty())throw std::runtime_error("OSG asset has no triangle geometry: "+path);
    return collector.output;
}
}
#else
namespace vsgraysim::earth {
void initializeVoxelModelImport(){}
vsg::ref_ptr<vsg::Data> loadOsgVoxelTexture(const std::string&) {return {};}
vsg::ref_ptr<vsg::Node> loadOsgVoxelMesh(const std::string&) {throw std::runtime_error("OSG/IVE import is disabled; enable VSGRAYSIM_ENABLE_OSG_IMPORT and configure OPENSCENEGRAPH_ROOT");}
}
#endif
