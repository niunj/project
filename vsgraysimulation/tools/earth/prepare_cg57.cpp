// Coordinate conversion and source-named navigation-light emission of the pinned FlightGear CG-57 model.
// Source geometry and textures stay intact; FlightGear -X bow becomes local +Z, up +Y.
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osg/ComputeBoundsVisitor>
#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/Material>
#include <iostream>
#include <cmath>
class NavigationLights:public osg::NodeVisitor{
public:NavigationLights():osg::NodeVisitor(TRAVERSE_ALL_CHILDREN){}
 void apply(osg::Geode& geode)override{bool lamp=false;osg::Vec4 color;for(const auto* node:getNodePath()){const auto& name=node->getName();if(name=="beacon-port"){color={1,.015f,.008f,1};lamp=true;}else if(name=="beacon-starboard"){color={.01f,.9f,.035f,1};lamp=true;}else if(name=="beacon-stern"||name=="beacons-mast"){color={1,.95f,.8f,1};lamp=true;}}if(lamp){auto material=new osg::Material;material->setDiffuse(osg::Material::FRONT_AND_BACK,color);material->setEmission(osg::Material::FRONT_AND_BACK,color);material->setShininess(osg::Material::FRONT_AND_BACK,0);geode.getOrCreateStateSet()->setAttributeAndModes(material);for(unsigned i=0;i<geode.getNumDrawables();++i)geode.getDrawable(i)->getOrCreateStateSet()->setAttributeAndModes(material);}traverse(geode);}
};
int main(int argc,char** argv){
 if(argc!=3)return 2;auto options=new osgDB::Options("noRotation WriteImageHint=IncludeData");auto model=osgDB::readRefNodeFile(argv[1],options);if(!model)return 1;NavigationLights lights;model->accept(lights);
 osg::ComputeBoundsVisitor bv;model->accept(bv);auto b=bv.getBoundingBox();const bool zUp=(b.zMax()-b.zMin())>(b.yMax()-b.yMin());
 osg::Matrixd rotate=zUp?osg::Matrixd(0,0,-1,0,-1,0,0,0,0,1,0,0,0,0,0,1):osg::Matrixd(0,0,-1,0,0,1,0,0,1,0,0,0,0,0,0,1);
 auto root=new osg::MatrixTransform;root->setMatrix(rotate);root->addChild(model);osg::ComputeBoundsVisitor aligned;root->accept(aligned);auto a=aligned.getBoundingBox();root->setMatrix(rotate*osg::Matrixd::translate(-(a.xMin()+a.xMax())*.5,0,-(a.zMin()+a.zMax())*.5));
 osg::ComputeBoundsVisitor final;root->accept(final);a=final.getBoundingBox();std::cout<<"source_z_up="<<zUp<<" bounds_m=["<<a.xMin()<<','<<a.yMin()<<','<<a.zMin()<<"] ["<<a.xMax()<<','<<a.yMax()<<','<<a.zMax()<<"]\n";
 return osgDB::writeNodeFile(*root,argv[2],options)?0:1;
}
