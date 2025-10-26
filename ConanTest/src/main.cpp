#include <iostream>
#include <osgViewer/Viewer>
#include <osg/Node>
#include <osg/ref_ptr>

int main() {
    std::cout << "ConanTest project with OpenSceneGraph" << std::endl;
    
    // 创建一个简单的查看器
    osgViewer::Viewer viewer;
    
    // 设置一个基本的场景（这里不使用复杂的几何体）创建一个基本窗口。
    viewer.setSceneData(nullptr);
    
    std::cout << "Viewer initialized successfully" << std::endl;
    
    // 不进入渲染循环，直接返回成功
    return 0;
}