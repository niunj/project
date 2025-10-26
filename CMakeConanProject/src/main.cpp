#include <osgViewer/Viewer>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Vec3>
#include <osg/Vec4>
#include <osg/PolygonMode>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgUtil/Optimizer>

int main(int argc, char** argv)
{
    // 创建Viewer对象
    osg::ref_ptr<osgViewer::Viewer> viewer = new osgViewer::Viewer();
    
    // 创建根节点
    osg::ref_ptr<osg::Group> root = new osg::Group();
    
    // 创建一个Geode节点
    osg::ref_ptr<osg::Geode> geode = new osg::Geode();
    
    // 创建几何体
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry();
    
    // 创建顶点数组
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();
    vertices->push_back(osg::Vec3(-1.0, -1.0, 0.0));
    vertices->push_back(osg::Vec3(1.0, -1.0, 0.0));
    vertices->push_back(osg::Vec3(1.0, 1.0, 0.0));
    vertices->push_back(osg::Vec3(-1.0, 1.0, 0.0));
    geometry->setVertexArray(vertices.get());
    
    // 创建颜色数组
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array();
    colors->push_back(osg::Vec4(1.0, 0.0, 0.0, 1.0)); // 红色
    geometry->setColorArray(colors.get(), osg::Array::Binding::BIND_OVERALL);
    
    // 创建法线数组
    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array();
    normals->push_back(osg::Vec3(0.0, 0.0, 1.0));
    geometry->setNormalArray(normals.get(), osg::Array::Binding::BIND_OVERALL);
    
    // 设置图元
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));
    
    // 将几何体添加到Geode
    geode->addDrawable(geometry.get());
    
    // 将Geode添加到根节点
    root->addChild(geode.get());
    
    // 优化场景图
    osgUtil::Optimizer optimizer;
    optimizer.optimize(root.get());
    
    // 设置场景数据
    viewer->setSceneData(root.get());
    
    // 设置窗口大小
    viewer->setUpViewInWindow(100, 100, 800, 600);
    
    // 运行查看器
    return viewer->run();
}