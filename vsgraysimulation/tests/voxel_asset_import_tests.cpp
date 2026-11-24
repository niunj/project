#include "../src/earth/VoxelModelVoxelizer.h"
#include "../src/earth/VoxelTerrain.h"
#include <vsg/all.h>
#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <filesystem>
#include <iostream>
#include <chrono>
using namespace vsgraysim::earth;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const std::vector<uint32_t> triangles={0,1,2,0,2,3,4,7,6,4,6,5,0,4,5,0,5,1,3,2,6,3,6,7,0,3,7,0,7,4,1,5,6,1,6,2};
const std::vector<vsg::vec3> points={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
vsg::ref_ptr<vsg::Node> vsgCube(){
    auto p=vsg::vec3Array::create(points.size()+4);for(size_t i=0;i<points.size();++i)(*p)[i+4]=points[i];
    auto indices=vsg::uintArray::create(triangles.size()+3);for(size_t i=0;i<triangles.size();++i)(*indices)[i+3]=triangles[i];
    auto geometry=vsg::Geometry::create();geometry->assignArrays({p});geometry->assignIndices(indices);
    auto command=vsg::DrawIndexed::create();command->indexCount=uint32_t(triangles.size());command->instanceCount=1;command->firstIndex=3;command->vertexOffset=4;geometry->commands.push_back(command);
    auto transformed=vsg::MatrixTransform::create(vsg::translate(10.,20.,30.));transformed->addChild(geometry);return transformed;
}
}
int main()try{
    const auto root=std::filesystem::temp_directory_path()/("voxel-assets-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{root};std::filesystem::create_directories(root);
    auto nativeModel=vsgCube();VoxelModelConfig config;config.voxelSize=.25;config.margin=0;
    const auto expected=voxelizeMesh(nativeModel,config).terrain->encodeVolume();
    for(const auto* ext:{".vsgt",".vsgb"}) {
        const auto path=(root/(std::string("cube")+ext)).string();require(vsg::write(nativeModel,path),"native VSG fixture could not be written");
        require(voxelizeFile(path,config).terrain->encodeVolume()==expected,"native VSG Geometry draw range/vertex offset changed occupancy");
        std::cout<<"PASS: actual "<<ext<<" Geometry/DrawIndexed target import\n";
    }
    auto lod=vsg::LOD::create();lod->addChild({.5,nativeModel});auto coarse=vsg::MatrixTransform::create(vsg::translate(1000.,0.,0.));coarse->addChild(nativeModel);lod->addChild({0,coarse});
    require(voxelizeMesh(lod,config).terrain->encodeVolume()==expected,"native VSG LOD levels were superimposed");
    auto paged=vsg::PagedLOD::create();paged->filename="cube.vsgb";paged->children[0].minimumScreenHeightRatio=.5;paged->children[1].node=coarse;
    require(vsg::write(paged,(root/"paged.vsgt").string()),"could not write VSG PagedLOD fixture");
    require(voxelizeFile((root/"paged.vsgt").string(),config).terrain->encodeVolume()==expected,"VSG PagedLOD did not load its finest external asset");
    auto strip=vsg::Geometry::create();strip->assignArrays({vsg::vec3Array::create({{-1,-1,0},{1,-1,0},{-1,1,0},{1,1,0}})});strip->assignIndices(vsg::uintArray::create({0,1,2,3}));
    strip->commands.push_back(vsg::DrawIndexed::create(4,1,0,0,0));auto group=vsg::StateGroup::create();auto pipeline=vsg::GraphicsPipeline::create();auto assembly=vsg::InputAssemblyState::create();assembly->topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;pipeline->pipelineStates.push_back(assembly);group->add(vsg::BindGraphicsPipeline::create(pipeline));group->addChild(strip);
    auto list=vsg::VertexIndexDraw::create();list->arrays=strip->arrays;list->assignIndices(vsg::uintArray::create({0,1,2,2,1,3}));list->indexCount=6;
    require(voxelizeMesh(group,config).terrain->encodeVolume()==voxelizeMesh(list,config).terrain->encodeVolume(),"native VSG triangle strip topology was treated as a list");
    std::cout<<"PASS: native VSG strip assembly and VSG finest external PagedLOD\n";
    std::filesystem::copy_file(root/"cube.vsgt",root/"cube.vsg");require(voxelizeFile((root/"cube.vsg").string(),config).terrain->encodeVolume()==expected,".vsg native-header alias did not load");
    // Actual UV checker texture must survive import, damage, cropping and archive restore.
    auto painted = vsg::VertexIndexDraw::create();
    painted->assignArrays({vsg::vec3Array::create({{0,0,0},{4,0,0},{4,4,0},{0,4,0}})});
    painted->assignIndices(vsg::uintArray::create({0,1,2,0,2,3}));
    painted->indexCount = 6;
    painted->instanceCount = 1;
    painted->setObject("voxel.import.uv", vsg::vec2Array::create({{0,0},{1,0},{1,1},{0,1}}));
    auto checker = vsg::ubvec4Array2D::create(8,8, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_SRGB});
    for (unsigned y=0; y<8; ++y) for (unsigned x=0; x<8; ++x)
        (*checker)(x,y) = vsg::ubvec4(x<4?240:20, 20, x<4?20:240, 255);
    painted->setObject("voxel.import.image", checker);
    painted->setValue("voxel.import.emissive", vsg::vec3(.2f,.4f,.8f));
    const auto paintPath = (root/"painted-material.vsgb").string();
    require(vsg::write(painted,paintPath),"paint fixture write failed");
    auto paint=voxelizeFile(paintPath,config).terrain;
    require(paint->appearance()&&!paint->imagery_rgba.empty()&&paint->imagery_uv.size()==paint->vertices().size(),"source UV texture was discarded");
    bool red=false,blue=false;
    for(size_t i=0;i<paint->imagery_rgba.size();i+=4) {
        red|=paint->imagery_rgba[i]>200&&paint->imagery_rgba[i+2]<50;
        blue|=paint->imagery_rgba[i+2]>200&&paint->imagery_rgba[i]<50;
    }
    require(red&&blue,"source texture color regions were averaged away");
    bool lamp=false;
    const auto& glow=paint->appearance()->emissive_rgba;
    for(size_t i=0;i<glow.size();i+=4)
        lamp|=std::abs(int(glow[i])-51)<=1&&std::abs(int(glow[i+1])-102)<=1&&std::abs(int(glow[i+2])-204)<=1;
    require(lamp,"source emissive lamp was discarded");
    auto nativeImage=vsg::ubvec4Array2D::create(8,8);for(int y=0;y<8;++y)for(int x=0;x<8;++x)(*nativeImage)(x,y)=vsg::ubvec4(x<4?240:20,20,x<4?20:240,255);
    nativeImage->properties.format=VK_FORMAT_R8G8B8A8_SRGB;
    auto nativeDraw=vsg::VertexIndexDraw::create();nativeDraw->assignArrays({vsg::vec3Array::create({{0,0,0},{4,0,0},{4,4,0},{0,4,0}}),vsg::vec3Array::create(4,vsg::vec3(0,0,1)),vsg::vec2Array::create({{0,0},{1,0},{1,1},{0,1}}),vsg::vec4Array::create(1,vsg::vec4(1,1,1,1))});
    nativeDraw->assignIndices(vsg::uintArray::create({0,1,2,0,2,3}));nativeDraw->indexCount=6;nativeDraw->instanceCount=1;
    auto builder=vsg::Builder::create();vsg::StateInfo state;state.image=nativeImage;auto nativePaint=builder->createStateGroup(state);nativePaint->addChild(nativeDraw);
    for(const auto* ext:{".vsgt",".vsgb"}) {
        const auto file=(root/(std::string("painted")+ext)).string();require(vsg::write(nativePaint,file),"painted VSG fixture write failed");
        auto imported=voxelizeFile(file,config).terrain;require(imported->appearance()&&!imported->imagery_rgba.empty(),"VSG descriptor UV image was discarded");
    }
    // Tilted thin surface: projection must recover the authored plane rather
    // than thicken it to a whole voxel. PBR maps are linear data, not sRGB.
    auto fineDraw=vsg::VertexIndexDraw::create();const auto authoredNormal=vsg::normalize(vsg::vec3(0,-.17f,4));
    fineDraw->assignArrays({vsg::vec3Array::create({{0,0,0},{4,0,0},{4,4,.17f},{0,4,.17f}}),vsg::vec3Array::create(4,authoredNormal),vsg::vec2Array::create({{0,0},{1,0},{1,1},{0,1}}),vsg::vec4Array::create(1,vsg::vec4(1,1,1,1))});fineDraw->assignIndices(vsg::uintArray::create({0,1,2,0,2,3}));fineDraw->indexCount=6;fineDraw->instanceCount=1;
    auto pbr=vsg::GraphicsPipelineConfigurator::create(vsg::createPhysicsBasedRenderingShaderSet());
    pbr->enableArray("vsg_Vertex",VK_VERTEX_INPUT_RATE_VERTEX,12);pbr->enableArray("vsg_Normal",VK_VERTEX_INPUT_RATE_VERTEX,12);pbr->enableArray("vsg_TexCoord0",VK_VERTEX_INPUT_RATE_VERTEX,8);pbr->enableArray("vsg_Color",VK_VERTEX_INPUT_RATE_INSTANCE,16);
    auto mr=vsg::ubvec4Array2D::create(1,1,vsg::ubvec4(255,128,204,255));mr->properties.format=VK_FORMAT_R8G8B8A8_UNORM;
    auto normalMap=vsg::ubvec4Array2D::create(1,1,vsg::ubvec4(148,128,253,255));normalMap->properties.format=VK_FORMAT_R8G8B8A8_UNORM;
    pbr->assignTexture("diffuseMap",nativeImage);pbr->assignTexture("mrMap",mr);pbr->assignTexture("normalMap",normalMap);auto factor=vsg::PbrMaterialValue::create();factor->value().emissiveFactor=vsg::vec4(.1f,.3f,.05f,1.f);factor->value().metallicFactor=.5;factor->value().roughnessFactor=.6;pbr->assignDescriptor("material",factor);pbr->init();auto fineGroup=vsg::StateGroup::create();pbr->copyTo(fineGroup);fineGroup->addChild(fineDraw);
    auto fine=voxelizeMesh(fineGroup,config).terrain;
    require(!fine->appearance()->corners.empty()&&fine->surface_normals.size()==fine->vertices().size(),"source surface/normal reconstruction missing");
    for(const auto& point:fine->vertices())require(std::abs(point.z-point.y*.17/4)<1e-6,"thin wing was inflated by the voxel grid");
    bool physical=false,bump=false;for(size_t i=0;i<fine->mr_rgba.size();i+=4){physical|=std::abs(int(fine->mr_rgba[i+1])-77)<=1&&std::abs(int(fine->mr_rgba[i+2])-102)<=1;bump|=fine->normal_rgba[i]!=128;}
    bool nativeEmission=false;const auto& e=fine->appearance()->emissive_rgba;for(size_t i=0;i<e.size();i+=4)nativeEmission|=std::abs(int(e[i])-26)<=1&&std::abs(int(e[i+1])-77)<=1&&std::abs(int(e[i+2])-13)<=1;require(nativeEmission,"VSG PBR emissive factor was discarded");
    require(physical&&bump,"normal/metallic/roughness factors or maps were discarded");
    auto roundtrip=VoxelTerrain::decodeVolume(fine->encodeVolume());require(roundtrip->encodeVolume()==fine->encodeVolume()&&roundtrip->vertices()==fine->vertices(),"subcell/PBR checkpoint changed geometry");
    auto thinScene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",{{{"id","thin"},{"source",{{"type","volume"},{"volume",fine->encodeVolume()}}},{"physics",{{"motion","static"},{"fracture",true},{"support","none"}}}}}}});
    for(int y=1;y<=3;++y)for(int x=1;x<=3;++x){const auto a=thinScene->raycast({double(x),double(y),5},{0,0,-1},10),b=thinScene->rigidRaycast({double(x),double(y),5},{0,0,-1},10);require(a&&b&&std::abs(a->distance-b->distance)<.001,"thin reconstructed collider differs from sensor surface");}
    std::cout<<"PASS: thin surface reconstruction, authored normals, tangent normal map and linear metallic/roughness atlas\n";
    VoxelModelConfig clippedConfig=config;clippedConfig.sourceSurface=true;clippedConfig.workers=1;
    auto clipped=voxelizeMesh(fineGroup,clippedConfig).terrain;clippedConfig.workers=4;auto parallelClipped=voxelizeMesh(fineGroup,clippedConfig).terrain;
    require(clipped->occupancy()==fine->occupancy(),"source detail changed occupied physical cells");require(clipped->appearance()->detailed(),"source patches missing");
    require(clipped->encodeVolume()==parallelClipped->encodeVolume(),"parallel atlas preparation is not deterministic");
    auto clippedRestored=VoxelTerrain::decodeVolume(clipped->encodeVolume());require(clippedRestored->vertices()==clipped->vertices()&&clippedRestored->imagery_uv==clipped->imagery_uv,"compressed source checkpoint changed the canonical surface");
    auto sheets=vsg::VertexIndexDraw::create();sheets->assignArrays({vsg::vec3Array::create({{0,0,.1f},{4,0,.1f},{4,4,.1f},{0,4,.1f},{0,0,.2f},{4,0,.2f},{4,4,.2f},{0,4,.2f}})});sheets->assignIndices(vsg::uintArray::create({0,1,2,0,2,3,4,5,6,4,6,7}));sheets->indexCount=12;sheets->instanceCount=1;clippedConfig.voxelSize=.5;
    auto separate=voxelizeMesh(sheets,clippedConfig).terrain;
    auto sourceScene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",{{{"id","sheets"},{"source",{{"type","volume"},{"volume",separate->encodeVolume()}}},{"physics",{{"motion","static"},{"fracture",true},{"support","none"}}}}}}});
    for(const auto direction:{vsg::dvec3(0,0,1),vsg::dvec3(0,0,-1)}){auto a=sourceScene->raycast({1.3,1.7,.15},direction,1),b=sourceScene->rigidRaycast({1.3,1.7,.15},direction,1);require(a&&b&&std::abs(a->distance-.05)<1e-6&&std::abs(a->distance-b->distance)<.001,"two thin structures in one cell were merged or disagreed with Bullet");}
    sourceScene->schedule({{"kind","damage"},{"object","sheets"},{"tick",1},{"point_m",{2,2,.15}},{"radius_m",1.1}});sourceScene->setPaused(false);sourceScene->advanceTo(.02);
    require(!sourceScene->raycast({2,2,1},{0,0,-1},2)&&!sourceScene->rigidRaycast({2,2,1},{0,0,-1},2),"source patches survived a deleted owner cell");
    sourceScene->schedule({{"kind","fracture"},{"object","sheets"},{"tick",2},{"point_m",{2,2,.15}},{"normal",{1,0,0}},{"radius_m",10},{"energy_j",1e9}});sourceScene->advanceTo(.04);auto sourceResume=VoxelScene::load(sourceScene->checkpoint());require(sourceResume->checkpoint()==sourceScene->checkpoint(),"source-clipped fragments did not roundtrip");
    for(const auto& [id,o]:sourceResume->snapshot()->objects)require(o.volume->appearance()->detailed()&&o.volume->imagery_uv.size()==o.volume->vertices().size(),"crop lost source-patch ownership/UVs");
    auto corrupt=separate->encodeVolume();corrupt["surface_appearance"]["source_patches_zlib_cbor"]["raw_bytes"]=300ULL*1024*1024;bool rejected=false;try{VoxelTerrain::decodeVolume(corrupt);}catch(const std::exception&){rejected=true;}require(rejected,"unbounded compressed source allocation accepted");
    auto largeAppearance=std::make_shared<VoxelSurfaceAppearance>();largeAppearance->origin={0,0,0};largeAppearance->dimensions={1,1,1};largeAppearance->cellSize=1;largeAppearance->width=1100;largeAppearance->height=1000;largeAppearance->rgba.resize(4*1100*1000,127);auto archiveFixture=VoxelTerrain::fromOccupancy({0,0,0},1,1,1,1,{true},false);archiveFixture->setAppearance(largeAppearance);auto compact=archiveFixture->encodeVolume();require(compact["surface_appearance"].contains("rgba_zlib_hex"),"large repetitive atlas was not losslessly compressed");auto expanded=VoxelTerrain::decodeVolume(compact);require(expanded->imagery_rgba==largeAppearance->rgba,"compressed atlas changed source texels");
    std::cout<<"PASS: independent thin structures, deterministic threaded source atlas, canonical collision, damage/crop and compressed checkpoint\n";
    auto restored=VoxelTerrain::decodeVolume(paint->encodeVolume());require(restored->appearance()->emissive_rgba==paint->appearance()->emissive_rgba&&restored->imagery_rgba==paint->imagery_rgba&&restored->imagery_uv==paint->imagery_uv,"surface appearance checkpoint drift");
    auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",{{{"id","painted"},{"source",{{"type","mesh"},{"path",paintPath},{"voxel_size_m",.25}}},{"physics",{{"motion","static"},{"fracture",true},{"support","none"}}}}}}});
    scene->schedule({{"kind","damage"},{"object","painted"},{"tick",1},{"point_m",{2,2,0}},{"radius_m",1.1}});scene->setPaused(false);scene->advanceTo(.02);
    scene->schedule({{"kind","fracture"},{"object","painted"},{"tick",2},{"point_m",{2,2,0}},{"normal",{1,0,0}},{"radius_m",10},{"energy_j",1e9}});scene->advanceTo(.04);
    require(!scene->raycast({2,2,5},{0,0,-1},10)&&!scene->rigidRaycast({2,2,5},{0,0,-1},10),"reconstructed collider bridged a damage opening");
    require(scene->snapshot()->objects.size()>1,"painted fixture did not exercise cropped fragments");
    for(const auto& [id,o]:scene->snapshot()->objects)require(o.volume->appearance()&&o.volume->imagery_uv.size()==o.volume->vertices().size(),"damage or cropped fragment discarded paint");
    auto resumed=VoxelScene::load(scene->checkpoint());require(resumed->checkpoint()==scene->checkpoint(),"painted fragments failed archive round trip");
    require(resumed->checkpoint().at("surface_appearances").size()==1,"fracture checkpoint duplicated source skin atlases");
    const auto sharedSkin=resumed->snapshot()->objects.begin()->second.volume->appearance();for(const auto& [id,o]:resumed->snapshot()->objects)require(o.volume->appearance()==sharedSkin,"restored fragments did not share immutable surface data");
    const auto sensors=makeVoxelSceneSensorData(scene->snapshot());require(!sensors.textures.layers.empty(),"sensor target albedo missing");
    std::cout<<"PASS: source UV color regions, damage, fragment atlas, checkpoint and sensor albedo\n";
    VoxelModelConfig budget;budget.maxDimension=512;budget.margin=0;
    auto fitted=voxelizeMesh(nativeModel,budget);require(fitted.terrain->cellCount()<=4'000'000&&fitted.nx<512,"automatic import exceeded its grid budget");
    require(uint64_t(fitted.nx+1)*(fitted.ny+1)*(fitted.nz+1)>4'000'000,"automatic grid did not choose the finest permitted resolution");
    std::cout<<"PASS: finest automatic resolution within unchanged cell budget\n";
    config.unitsToMetres=.001;config.voxelSize=.00025;config.upAxis="z";auto converted=voxelizeFile((root/"cube.vsgb").string(),config);
    require(std::abs(converted.terrain->min_x-.009)<1e-7&&std::abs(converted.terrain->min_height-.029)<1e-7&&std::abs(converted.terrain->min_z+.021)<1e-7,"explicit millimetre/Z-up import conversion is wrong");
    // Reconstructed dynamic contact skins use the same triangulation as
    // display/measurement; verify actual settling and deterministic restart.
    auto contactDraw = vsg::VertexIndexDraw::create();
    auto contactPositions = vsg::vec3Array::create(points.size());
    std::copy(points.begin(), points.end(), contactPositions->begin());
    auto contactIndices = vsg::uintArray::create(triangles.size());
    std::copy(triangles.begin(), triangles.end(), contactIndices->begin());
    contactDraw->assignArrays({contactPositions});
    contactDraw->assignIndices(contactIndices);
    contactDraw->indexCount = contactIndices->size();
    contactDraw->instanceCount = 1;
    contactDraw->setValue("voxel.import.appearance", true);
    contactDraw->setValue("voxel.import.diffuse", vsg::vec4(.5f,.6f,.7f,1.f));
    auto contactModel = vsg::MatrixTransform::create(vsg::translate(10.,20.,30.));
    contactModel->addChild(contactDraw);
    const auto contactPath = (root/"contact.vsgb").string();
    require(vsg::write(contactModel,contactPath),"contact mesh fixture write failed");
    auto contactScene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",nlohmann::json::array({{{"id","model"},{"position_m",{-10,-18,-30}},{"source",{{"type","mesh"},{"path",contactPath},{"max_dimension",16}}},{"physics",{{"motion","dynamic"},{"support","none"}}}},{{"id","floor"},{"position_m",{-10,-.5,-10}},{"source",{{"type","box"},{"dimensions",{80,2,80}},{"cell_size_m",.25}}},{"physics",{{"motion","static"}}}}})}});contactScene->setPaused(false);contactScene->advanceTo(2,128);const auto cs=contactScene->snapshot();const auto& dynamic=cs->objects.at("model");const auto centre=dynamic.pose(cs->tick,cs->fixedStep)*dynamic.localCentreOfMass();require(!cs->contacts.empty()&&std::abs(centre.y-1)<.04&&vsg::length(dynamic.velocity)<.1,"canonical dynamic triangle skin did not settle on the solid floor");auto contactRestart=VoxelScene::load(contactScene->checkpoint());contactRestart->setPaused(false);contactScene->advanceTo(2.2,16);contactRestart->advanceTo(2.2,16);require(contactScene->checkpoint()==contactRestart->checkpoint(),"canonical contact mesh depends on unsaved contact caches");std::cout<<"PASS: canonical dynamic triangle contact skin, settling and deterministic restart\n";
    std::cout<<"PASS: explicit unit conversion and Z-up to east/up/south\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
