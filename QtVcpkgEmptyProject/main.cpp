//#include <osg/Geode>
//#include <osg/Geometry>
//#include <osg/Group>
//#include <osgViewer/Viewer>
//#include <osgDB/ReadFile>
//#include <osgGA/TrackballManipulator>
//#include <osg/MatrixTransform>
//#include <osgUtil/SmoothingVisitor>
//// 新增着色器相关头文件
//#include <osg/Shader>
//#include <osg/Program>
//#include <osg/Uniform>
//#include <chrono>  // 用于时间计算

//// 创建带着色器的立方体（修改原函数）
//osg::ref_ptr<osg::Node> createColoredCube() {
//    // 1. 创建几何体（保留原有顶点定义）
//    osg::ref_ptr<osg::Geometry> geom = new osg::Geometry();

//    // 顶点坐标（8个顶点）
//    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();
//    vertices->push_back(osg::Vec3(-1, -1, -1));  // 顶点0
//    vertices->push_back(osg::Vec3(1, -1, -1));   // 顶点1
//    vertices->push_back(osg::Vec3(1, 1, -1));    // 顶点2
//    vertices->push_back(osg::Vec3(-1, 1, -1));   // 顶点3
//    vertices->push_back(osg::Vec3(-1, -1, 1));   // 顶点4
//    vertices->push_back(osg::Vec3(1, -1, 1));    // 顶点5
//    vertices->push_back(osg::Vec3(1, 1, 1));     // 顶点6
//    vertices->push_back(osg::Vec3(-1, 1, 1));    // 顶点7
//    geom->setVertexArray(vertices.get());

//    // 面定义（6个面）
//    osg::ref_ptr<osg::DrawElementsUInt> faces = new osg::DrawElementsUInt(osg::PrimitiveSet::QUADS, 0);
//    faces->push_back(4); faces->push_back(5); faces->push_back(6); faces->push_back(7);  // 前面
//    faces->push_back(0); faces->push_back(3); faces->push_back(2); faces->push_back(1);  // 后面
//    faces->push_back(1); faces->push_back(2); faces->push_back(6); faces->push_back(5);  // 右面
//    faces->push_back(0); faces->push_back(4); faces->push_back(7); faces->push_back(3);  // 左面
//    faces->push_back(3); faces->push_back(7); faces->push_back(6); faces->push_back(2);  // 顶面
//    faces->push_back(0); faces->push_back(1); faces->push_back(5); faces->push_back(4);  // 底面
//    geom->addPrimitiveSet(faces.get());

//    // 2. 添加纹理坐标（用于着色器计算，替代原有的固定颜色）
//    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array();
//    // 为每个顶点设置纹理坐标（范围0~1，用于着色器区分不同面）
//    texCoords->push_back(osg::Vec2(0, 0));  // 顶点0
//    texCoords->push_back(osg::Vec2(1, 0));  // 顶点1
//    texCoords->push_back(osg::Vec2(1, 1));  // 顶点2
//    texCoords->push_back(osg::Vec2(0, 1));  // 顶点3
//    texCoords->push_back(osg::Vec2(0, 0));  // 顶点4
//    texCoords->push_back(osg::Vec2(1, 0));  // 顶点5
//    texCoords->push_back(osg::Vec2(1, 1));  // 顶点6
//    texCoords->push_back(osg::Vec2(0, 1));  // 顶点7
//    geom->setTexCoordArray(0, texCoords.get());

//    // 3. 计算法线（用于光照计算）
//    osgUtil::SmoothingVisitor::smooth(*geom);

//    // 4. 创建着色器程序
//    // 顶点着色器：传递顶点位置、纹理坐标和法线
//    // 着色器代码（使用 GLSL 120 兼容版本）
//        const std::string vertexShaderSource = R"(
//            #version 120
//            attribute vec3 osg_Vertex;
//            attribute vec2 osg_MultiTexCoord0;
//            attribute vec3 osg_Normal;
//            varying vec2 v_texCoord;
//            varying vec3 v_normal;

//            void main()
//            {
//                v_texCoord = osg_MultiTexCoord0;
//                v_normal = gl_NormalMatrix * osg_Normal;
//                gl_Position = gl_ModelViewProjectionMatrix * vec4(osg_Vertex, 1.0);
//            }
//        )";

//        const std::string fragmentShaderSource = R"(
//            #version 120
//            varying vec2 v_texCoord;
//            varying vec3 v_normal;
//            uniform float u_time;

//            void main()
//            {
//                float t = fract(u_time);

//                // gl_FragColor = vec4(t, 0.0, 0.0, 1.0);

//                vec3 color;
//                if (t < 0.33) {
//                    color = vec3(t / 0.33, 0.0, 0.0);
//                } else if (t < 0.66) {
//                    color = vec3(0.0, (t-0.33)/0.33, 0.0);
//                } else {
//                    color = vec3(0.0, 0.0, (t-0.66)/0.34);
//                }
//                gl_FragColor = vec4(color, 1.0);
//            }
//        )";

//    // 5. 编译并关联着色器
//    osg::ref_ptr<osg::Shader> vs = new osg::Shader(osg::Shader::VERTEX, vertexShaderSource);
//    osg::ref_ptr<osg::Shader> fs = new osg::Shader(osg::Shader::FRAGMENT, fragmentShaderSource);
//    osg::ref_ptr<osg::Program> program = new osg::Program();
//    program->addShader(vs);
//    program->addShader(fs);

//    // 6. 将着色器绑定到几何节点
//    osg::ref_ptr<osg::Geode> geode = new osg::Geode();
//    geode->addDrawable(geom.get());

//    // 获取状态集并绑定着色器
//    osg::ref_ptr<osg::StateSet> stateset = geode->getOrCreateStateSet();
//    stateset->setAttributeAndModes(program, osg::StateAttribute::ON);

//    return geode.get();
//}

//int main() {
//    // 1. 创建根节点
//    osg::ref_ptr<osg::Group> root = new osg::Group();

//    // 2. 创建矩阵变换节点（旋转动画）
//    osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform();
//    root->addChild(transform.get());

//    // 3. 添加带着色器的立方体
//    transform->addChild(createColoredCube());


//    osg::DisplaySettings* ds = osg::DisplaySettings::instance().get();
//    ds->setMinimumNumStencilBits(8);
//    ds->setGLContextVersion("3.3");  // 启用 OpenGL 3.3

//    // 4. 创建Viewer并配置
//    osgViewer::Viewer viewer;
//    viewer.setSceneData(root.get());
//    viewer.setCameraManipulator(new osgGA::TrackballManipulator());
//    viewer.setUpViewInWindow(100, 100, 800, 600);
//    viewer.realize();

//    // 5. 获取主相机并添加时间Uniform（着色器参数）
//    osg::Camera* camera = viewer.getCamera();
//    osg::ref_ptr<osg::Uniform> timeUniform = new osg::Uniform("u_time", 0.77f);
//    camera->getOrCreateStateSet()->addUniform(timeUniform);

//    // 6. 主循环（旋转+更新着色器时间参数）
//    return viewer.run();

//}

/////////////////////////////////////////////示例 说明///////////////////////////////////
////////多窗口中每个View采用不同的 着色器参数程序  通过每个View的Camera 传递数据参数/////////////
///////////////////////////////////////////////////////////////////////////////////////
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Shader>
#include <osg/Program>
#include <osg/Uniform>
#include <osgViewer/CompositeViewer>
#include <osgViewer/View>
#include <osgGA/TrackballManipulator>
#include <osgUtil/SmoothingVisitor>
#include <chrono>
#include <iostream>

// 创建共享的场景（带立方体贴图和共用着色器）
osg::ref_ptr<osg::Node> createSharedScene() {
    // 1. 创建立方体几何体
    osg::ref_ptr<osg::Geometry> geom = new osg::Geometry();

    // 顶点坐标
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();
    vertices->push_back(osg::Vec3(-1, -1, -1));
    vertices->push_back(osg::Vec3(1, -1, -1));
    vertices->push_back(osg::Vec3(1, 1, -1));
    vertices->push_back(osg::Vec3(-1, 1, -1));
    vertices->push_back(osg::Vec3(-1, -1, 1));
    vertices->push_back(osg::Vec3(1, -1, 1));
    vertices->push_back(osg::Vec3(1, 1, 1));
    vertices->push_back(osg::Vec3(-1, 1, 1));
    geom->setVertexArray(vertices.get());

    // 面定义
    osg::ref_ptr<osg::DrawElementsUInt> faces = new osg::DrawElementsUInt(osg::PrimitiveSet::QUADS, 0);
    faces->push_back(4); faces->push_back(5); faces->push_back(6); faces->push_back(7); // 前面
    faces->push_back(0); faces->push_back(3); faces->push_back(2); faces->push_back(1); // 后面
    faces->push_back(1); faces->push_back(2); faces->push_back(6); faces->push_back(5); // 右面
    faces->push_back(0); faces->push_back(4); faces->push_back(7); faces->push_back(3); // 左面
    faces->push_back(3); faces->push_back(7); faces->push_back(6); faces->push_back(2); // 顶面
    faces->push_back(0); faces->push_back(1); faces->push_back(5); faces->push_back(4); // 底面
    geom->addPrimitiveSet(faces.get());

    // 纹理坐标和法线
    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array();
    for (int i = 0; i < 8; ++i) texCoords->push_back(osg::Vec2(i%2, i/2%2));
    geom->setTexCoordArray(0, texCoords.get());
    osgUtil::SmoothingVisitor::smooth(*geom);

    // 2. 创建共用的着色器程序
    const std::string vsSource = R"(
        #version 120
        attribute vec3 osg_Vertex;
        attribute vec2 osg_MultiTexCoord0;
        varying vec2 v_texCoord;
        void main() {
            v_texCoord = osg_MultiTexCoord0;
            gl_Position = gl_ModelViewProjectionMatrix * vec4(osg_Vertex, 1.0);
        }
    )";

    // 片段着色器：使用相机传递的颜色参数（u_baseColor）
    const std::string fsSource = R"(
        #version 120
        varying vec2 v_texCoord;
        uniform vec3 u_baseColor; // 由相机传递的颜色参数
        void main() {
            // 颜色 = 基础色 * 纹理坐标（产生渐变效果）
            gl_FragColor = vec4(u_baseColor * (v_texCoord.x + v_texCoord.y), 1.0);
        }
    )";

    osg::ref_ptr<osg::Program> program = new osg::Program();
    program->addShader(new osg::Shader(osg::Shader::VERTEX, vsSource));
    program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fsSource));

    // 3. 将着色器绑定到几何体节点（共享着色器）
    osg::ref_ptr<osg::Geode> geode = new osg::Geode();
    geode->addDrawable(geom.get());
    geode->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);

    return geode;
}

int main() {
    // 1. 创建共享场景（两个视图共用）
    osg::ref_ptr<osg::Group> root = new osg::Group();
    osg::ref_ptr<osg::Node> sharedScene = createSharedScene();
    root->addChild(sharedScene);

    // 2. 创建 CompositeViewer
    osgViewer::CompositeViewer viewer;
    viewer.setThreadingModel(osgViewer::CompositeViewer::SingleThreaded); // 单线程模式

    // 3. 创建第一个视图（窗口1）
    osg::ref_ptr<osgViewer::View> view1 = new osgViewer::View();
    viewer.addView(view1.get());
    // 窗口1配置：位置(100,100)，大小(600,400)
    view1->setUpViewInWindow(100, 100, 600, 400);
    view1->setSceneData(root.get());
    view1->setCameraManipulator(new osgGA::TrackballManipulator());
    // 相机1传递参数：红色基调
    osg::Camera* cam1 = view1->getCamera();
    cam1->getOrCreateStateSet()->addUniform(new osg::Uniform("u_baseColor", osg::Vec3(1.0f, 0.2f, 0.2f)));

    // 4. 创建第二个视图（窗口2）
    osg::ref_ptr<osgViewer::View> view2 = new osgViewer::View();
    viewer.addView(view2.get());
    // 窗口2配置：位置(800,100)，大小(600,400)
    view2->setUpViewInWindow(800, 100, 600, 400);
    view2->setSceneData(root.get()); // 共享同一场景
    view2->setCameraManipulator(new osgGA::TrackballManipulator());
    // 相机2传递参数：蓝色基调
    osg::Camera* cam2 = view2->getCamera();
    cam2->getOrCreateStateSet()->addUniform(new osg::Uniform("u_baseColor", osg::Vec3(0.2f, 0.2f, 1.0f)));

    // 5. 运行视图
    return viewer.run();
}

