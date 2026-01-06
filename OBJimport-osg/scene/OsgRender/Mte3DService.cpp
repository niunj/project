#include "Mte3DService.h"
#include "QtConcurrent/qtconcurrentexception.h"
#include "MouseIntersectionHandler.h"
#include "OsgContext.h"

#include <QFileInfo>

#include "osgQtCompositeViewer.h"
#include "OsgContext.h"

#include <osg/PolygonOffset>
#include <osgDB/ObjectCache>

#include "../Common/readwritefile.h"
#include "../Log/log_manager.h"



#include <osg/Group>
#include <osg/Geode>
#include <osgDB/ReadFile>
#include <osgViewer/Viewer>
#include <osgGA/TrackballManipulator>
#include <osg/Vec3>
#include <osg/Vec4>
#include <osg/ShapeDrawable>
#include <osg/LineWidth>
#include <osg/Geometry>


// 假设你有一个相机更新回调来让 skybox 跟随相机位置
// class SkyboxUpdateCallback : public osg::NodeCallback {
// public:
//     SkyboxUpdateCallback(osg::Camera* cam) : _camera(cam) {}
//     virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
//         if (_camera.valid()) {
//             osg::Matrix view = _camera->getViewMatrix();
//             // 只取位置部分，移除旋转
//             osg::Vec3 eye, center, up;
//             view.getLookAt(eye, center, up);
//             node->asTransform()->asMatrixTransform()->setMatrix(
//                 osg::Matrix::translate(eye)
//             );
//         }
//         traverse(node, nv);
//     }
// private:
//     osg::observer_ptr<osg::Camera> _camera;
// };



// 手动创建带全景纹理坐标的球体（核心：适配skymap.jpg）
osg::ref_ptr<osg::Geode> createSphereForSkyMap1(float radius, int slices = 64, int stacks = 32)
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;

    // 1. 生成球体顶点和全景纹理坐标
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;

    // 球面参数方程：x=sinθcosφ, y=sinθsinφ, z=cosθ（θ：纬度，φ：经度）
    for (int stack = 0; stack <= stacks; ++stack)
    {
        float theta = osg::PI * static_cast<float>(stack) / static_cast<float>(stacks); // 纬度（0~π）
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);

        for (int slice = 0; slice <= slices; ++slice)
        {
            float phi = 2.0f * osg::PI * static_cast<float>(slice) / static_cast<float>(slices); // 经度（0~2π）
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);

            // 球体顶点（内侧，所以取反）
            vertices->push_back(osg::Vec3(
                -radius * sinTheta * cosPhi,  // 取反：相机在球内，看到内侧
                -radius * sinTheta * sinPhi,
                -radius * cosTheta
            ));

            // 全景纹理坐标（适配skymap.jpg的360°×180°）
            texCoords->push_back(osg::Vec2(
                static_cast<float>(slice) / static_cast<float>(slices),  // U：经度（0~1）
                static_cast<float>(stack) / static_cast<float>(stacks)   // V：纬度（0~1）
            ));
        }
    }

    geom->setVertexArray(vertices);
    geom->setTexCoordArray(0, texCoords);

    // 2. 生成图元（三角带）
    for (int stack = 0; stack < stacks; ++stack)
    {
        osg::ref_ptr<osg::DrawElementsUInt> de = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLE_STRIP);
        for (int slice = 0; slice <= slices; ++slice)
        {
            int idx1 = stack * (slices + 1) + slice;
            int idx2 = (stack + 1) * (slices + 1) + slice;
            de->push_back(idx1);
            de->push_back(idx2);
        }
        geom->addPrimitiveSet(de);
    }

    // 3. 禁用光照
    osg::ref_ptr<osg::StateSet> ss = geom->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    geode->addDrawable(geom);
    return geode;
}

// 创建全景天空盒（使用单张skymap.jpg）
osg::Node* createSkyBox1(const std::string& skyMapPath)
{
    // 1. 加载全景纹理（skymap.jpg）
    osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
    osg::ref_ptr<osg::Image> img = osgDB::readImageFile(skyMapPath);
    if (!img)
    {
        OSG_WARN << "全景天空贴图加载失败：" << skyMapPath << std::endl;
        return nullptr;
    }
    texture->setImage(img);

    // 2. 设置纹理参数（适配全景图）
    texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);  // S轴（经度）重复
    texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE); // T轴（纬度）夹紧
    texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);

    // 3. 创建带全景纹理坐标的球体（半径1000，足够大）
    osg::ref_ptr<osg::Geode> geode = createSphereForSkyMap1(1000.0f, 64, 32); // 64×32分片，兼顾性能和精度

    // 4. 设置状态集（核心渲染规则）
    osg::ref_ptr<osg::StateSet> stateset = geode->getOrCreateStateSet();
    stateset->setTextureAttributeAndModes(0, texture.get(), osg::StateAttribute::ON);
    stateset->setMode(GL_DEPTH_WRITEMASK, osg::StateAttribute::OFF); // 关闭深度写入
    stateset->setRenderBinDetails(-1, "RenderBin"); // 最先渲染
    stateset->setMode(GL_LIGHTING, osg::StateAttribute::OFF); // 禁用光照
    stateset->setAttributeAndModes(new osg::CullFace(osg::CullFace::BACK), osg::StateAttribute::OFF); // 禁用背面剔除

    // 5. 创建跟随相机的变换节点
    osg::ref_ptr<osg::MatrixTransform> skyBoxTransform = new osg::MatrixTransform;
    skyBoxTransform->setName("sky"); // 为天空节点添加名称
    skyBoxTransform->addChild(geode);

    // 相机跟随回调：天空盒中心始终与相机重合
    // skyBoxTransform->setUpdateCallback(new osg::NodeCallback() {
    //     virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
    //         osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(node);
    //         if (mt && nv && nv->getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR) {
    //             osg::View* view = nv->getView();
    //             if (view && view->getCamera()) {
    //                 osg::Vec3 eye = view->getCamera()->getViewMatrix().getTrans();
    //                 mt->setMatrix(osg::Matrix::translate(eye));
    //             }
    //         }
    //         traverse(node, nv);
    //     }
    // });

    return skyBoxTransform.release();
}

// 创建地面平面
osg::ref_ptr<osg::Geode> createGround1(float size)
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;

    // 设置地面厚度
    float groundThickness = 2.0f; // 地面厚度为2.0f

    // 创建顶部面
    osg::ref_ptr<osg::Geometry> topFace = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> topVertices = new osg::Vec3Array;
    topVertices->push_back(osg::Vec3(-size, -size, 0.0f));
    topVertices->push_back(osg::Vec3(size, -size, 0.0f));
    topVertices->push_back(osg::Vec3(size, size, 0.0f));
    topVertices->push_back(osg::Vec3(-size, size, 0.0f));
    topFace->setVertexArray(topVertices.get());

    // 顶部面颜色 - 浅灰色
    osg::ref_ptr<osg::Vec4Array> topColors = new osg::Vec4Array;
    topColors->push_back(osg::Vec4(0.8f, 0.8f, 0.8f, 1.0f));
    topFace->setColorArray(topColors.get(), osg::Array::BIND_OVERALL);

    // 顶部面法线
    osg::ref_ptr<osg::Vec3Array> topNormals = new osg::Vec3Array;
    topNormals->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));
    topFace->setNormalArray(topNormals.get(), osg::Array::BIND_OVERALL);

    // 顶部面图元
    topFace->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));

    // 创建四个侧面
    osg::ref_ptr<osg::Geometry> sideFaces = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> sideVertices = new osg::Vec3Array;

    // 前面 (-Y方向)
    sideVertices->push_back(osg::Vec3(-size, -size, 0.0f));
    sideVertices->push_back(osg::Vec3(size, -size, 0.0f));
    sideVertices->push_back(osg::Vec3(size, -size, -groundThickness));
    sideVertices->push_back(osg::Vec3(-size, -size, -groundThickness));

    // 右面 (+X方向)
    sideVertices->push_back(osg::Vec3(size, -size, 0.0f));
    sideVertices->push_back(osg::Vec3(size, size, 0.0f));
    sideVertices->push_back(osg::Vec3(size, size, -groundThickness));
    sideVertices->push_back(osg::Vec3(size, -size, -groundThickness));

    // 后面 (+Y方向)
    sideVertices->push_back(osg::Vec3(size, size, 0.0f));
    sideVertices->push_back(osg::Vec3(-size, size, 0.0f));
    sideVertices->push_back(osg::Vec3(-size, size, -groundThickness));
    sideVertices->push_back(osg::Vec3(size, size, -groundThickness));

    // 左面 (-X方向)
    sideVertices->push_back(osg::Vec3(-size, size, 0.0f));
    sideVertices->push_back(osg::Vec3(-size, -size, 0.0f));
    sideVertices->push_back(osg::Vec3(-size, -size, -groundThickness));
    sideVertices->push_back(osg::Vec3(-size, size, -groundThickness));

    sideFaces->setVertexArray(sideVertices.get());

    // 侧面颜色 - 深灰色，与顶面形成对比
    osg::ref_ptr<osg::Vec4Array> sideColors = new osg::Vec4Array;
    sideColors->push_back(osg::Vec4(0.5f, 0.5f, 0.5f, 1.0f));
    sideFaces->setColorArray(sideColors.get(), osg::Array::BIND_OVERALL);

    // 侧面法线
    osg::ref_ptr<osg::Vec3Array> sideNormals = new osg::Vec3Array;
    sideNormals->push_back(osg::Vec3(0.0f, -1.0f, 0.0f)); // 前面法线
    sideNormals->push_back(osg::Vec3(1.0f, 0.0f, 0.0f));  // 右面法线
    sideNormals->push_back(osg::Vec3(0.0f, 1.0f, 0.0f));  // 后面法线
    sideNormals->push_back(osg::Vec3(-1.0f, 0.0f, 0.0f)); // 左面法线
    sideFaces->setNormalArray(sideNormals.get(), osg::Array::BIND_PER_PRIMITIVE_SET);

    // 侧面图元
    for (int i = 0; i < 4; i++) {
        sideFaces->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, i * 4, 4));
    }

    // 创建底部面（可选，通常看不到）
    osg::ref_ptr<osg::Geometry> bottomFace = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> bottomVertices = new osg::Vec3Array;
    bottomVertices->push_back(osg::Vec3(-size, -size, -groundThickness));
    bottomVertices->push_back(osg::Vec3(size, -size, -groundThickness));
    bottomVertices->push_back(osg::Vec3(size, size, -groundThickness));
    bottomVertices->push_back(osg::Vec3(-size, size, -groundThickness));
    bottomFace->setVertexArray(bottomVertices.get());

    // 底部面颜色 - 与侧面相同
    osg::ref_ptr<osg::Vec4Array> bottomColors = new osg::Vec4Array;
    bottomColors->push_back(osg::Vec4(0.5f, 0.5f, 0.5f, 1.0f));
    bottomFace->setColorArray(bottomColors.get(), osg::Array::BIND_OVERALL);

    // 底部面法线
    osg::ref_ptr<osg::Vec3Array> bottomNormals = new osg::Vec3Array;
    bottomNormals->push_back(osg::Vec3(0.0f, 0.0f, -1.0f));
    bottomFace->setNormalArray(bottomNormals.get(), osg::Array::BIND_OVERALL);

    // 底部面图元
    bottomFace->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));

    // 将所有面添加到Geode
    geode->addDrawable(topFace.get());
    geode->addDrawable(sideFaces.get());
    geode->addDrawable(bottomFace.get());

    return geode;
}

// 创建坐标轴
osg::ref_ptr<osg::Geode> createAixs1(float size)
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;

    // 设置坐标轴宽度
    osg::ref_ptr<osg::LineWidth> axesLineWidth = new osg::LineWidth;
    axesLineWidth->setWidth(2.5f); // 略微粗于网格线

    // 创建通用状态集
    osg::ref_ptr<osg::StateSet> stateSet = new osg::StateSet;
    stateSet->setAttribute(axesLineWidth.get());
    stateSet->setAttributeAndModes(new osg::PolygonOffset(-1.0f, -1.0f), osg::StateAttribute::ON);
    stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
    stateSet->setMode(GL_DEPTH_WRITEMASK, osg::StateAttribute::OFF); // 禁用深度写入，防止遮挡
    stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
  //  stateSet->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA), osg::StateAttribute::ON);
  //  stateSet->setAttributeAndModes(new osg::LineStipple(1, 0xFFFF), osg::StateAttribute::OFF);
    stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

    // X轴（红色）
    osg::ref_ptr<osg::Geometry> xAxis = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> xAxisVertices = new osg::Vec3Array;
    xAxisVertices->push_back(osg::Vec3(0, 0, 0.02f));
    xAxisVertices->push_back(osg::Vec3(size, 0, 0.02f));
    xAxis->setVertexArray(xAxisVertices.get());

    osg::ref_ptr<osg::Vec4Array> xAxisColor = new osg::Vec4Array;
    xAxisColor->push_back(osg::Vec4(1.0f, 0.0f, 0.0f, 1.0f)); // X轴红色
    xAxis->setColorArray(xAxisColor.get(), osg::Array::BIND_OVERALL);
    xAxis->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 2));
    xAxis->setStateSet(stateSet.get());
    geode->addDrawable(xAxis.get());

    // Y轴（绿色）
    osg::ref_ptr<osg::Geometry> yAxis = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> yAxisVertices = new osg::Vec3Array;
    yAxisVertices->push_back(osg::Vec3(0, 0, 0.02f));
    yAxisVertices->push_back(osg::Vec3(0, size, 0.02f));
    yAxis->setVertexArray(yAxisVertices.get());

    osg::ref_ptr<osg::Vec4Array> yAxisColor = new osg::Vec4Array;
    yAxisColor->push_back(osg::Vec4(0.0f, 1.0f, 0.0f, 1.0f)); // Y轴绿色
    yAxis->setColorArray(yAxisColor.get(), osg::Array::BIND_OVERALL);
    yAxis->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 2));
    yAxis->setStateSet(stateSet.get());
    geode->addDrawable(yAxis.get());

    // Z轴（蓝色）
    osg::ref_ptr<osg::Geometry> zAxis = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> zAxisVertices = new osg::Vec3Array;
    zAxisVertices->push_back(osg::Vec3(0, 0, 0.02f));
    zAxisVertices->push_back(osg::Vec3(0, 0, size));
    zAxis->setVertexArray(zAxisVertices.get());

    osg::ref_ptr<osg::Vec4Array> zAxisColor = new osg::Vec4Array;
    zAxisColor->push_back(osg::Vec4(0.0f, 0.0f, 1.0f, 1.0f)); // Z轴蓝色
    zAxis->setColorArray(zAxisColor.get(), osg::Array::BIND_OVERALL);
    zAxis->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 2));
    zAxis->setStateSet(stateSet.get());
    geode->addDrawable(zAxis.get());

    return geode;
}

// 创建轴网
osg::ref_ptr<osg::Geode> createGrid1(float size, float step)
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;

    // 创建几何对象
    osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;

    // 创建顶点数组
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;

    // 添加水平线（Y轴方向）
    for (float x = -size; x <= size; x += step)
    {
        vertices->push_back(osg::Vec3(x, -size, 0.01f));
        vertices->push_back(osg::Vec3(x, size, 0.01f));
    }

    // 添加垂直线（X轴方向）
    for (float y = -size; y <= size; y += step)
    {
        vertices->push_back(osg::Vec3(-size, y, 0.01f));
        vertices->push_back(osg::Vec3(size, y, 0.01f));
    }

    // 设置顶点数组
    geom->setVertexArray(vertices.get());

    // 创建颜色数组
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    colors->push_back(osg::Vec4(0.5f, 0.5f, 0.5f, 1.0f)); // 灰色轴网
    geom->setColorArray(colors.get(), osg::Array::BIND_OVERALL);

    // 设置图元类型为线段
    geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, vertices->size()));

    // 获取状态集
    osg::ref_ptr<osg::StateSet> stateSet = geom->getOrCreateStateSet();

    // 增加线宽
    osg::ref_ptr<osg::LineWidth> lineWidth = new osg::LineWidth;
    lineWidth->setWidth(1.5f); // 增加线宽到1.5f
    stateSet->setAttribute(lineWidth.get());

    // 添加深度偏移，确保线条在缩放时始终可见
    osg::ref_ptr<osg::PolygonOffset> polygonOffset = new osg::PolygonOffset;
    polygonOffset->setFactor(-1.0f);
    polygonOffset->setUnits(-1.0f);
    stateSet->setAttributeAndModes(polygonOffset.get(), osg::StateAttribute::ON);

    // 禁用深度写入，但启用深度测试，这样线条不会遮挡其他物体，但也不会被错误地遮挡
    stateSet->setMode(GL_DEPTH_WRITEMASK, osg::StateAttribute::OFF);
    stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

    // 确保线条在透视变换下保持恒定宽度
    stateSet->setMode(GL_LINE_SMOOTH, osg::StateAttribute::ON);
    stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
    stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

    // 添加绘制到Geode
    geode->addDrawable(geom.get());

    return geode;
}


#include <osg/TextureCubeMap>


Mte3DService::Mte3DService(QObject* parent)
: QObject(parent)
, _groundSize(50.0f)  // 默认地面大小为50
, _gridSize(50.0f)    // 默认网格大小为50
, _axisSize(5.0f)     // 默认坐标轴大小为5
{

    prepareSceneRoots();

}

Mte3DService::~Mte3DService()
{

}

Mte3DService& Mte3DService::getInstance()
{
    static Mte3DService single;
    return single;
}

class TextureCollector : public osg::NodeVisitor
{
public:
    TextureCollector(std::vector<std::string>& texturePaths)
        : osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
          _texturePaths(texturePaths)
    {}

    void apply(osg::Geode& geode) override
    {
        for (unsigned int i = 0; i < geode.getNumDrawables(); ++i)
        {
            osg::Drawable* drawable = geode.getDrawable(i);
            if (drawable)
            {
                osg::StateSet* ss = drawable->getStateSet();
                if (ss)
                {
                    for (unsigned int j = 0; j < ss->getTextureAttributeList().size(); ++j)
                    {
                        osg::Texture* texture = dynamic_cast<osg::Texture*>(ss->getTextureAttribute(j, osg::StateAttribute::TEXTURE));
                        if (texture && texture->getImage(0))
                        {
                            osg::Image* image = texture->getImage(0);
                            if (!image->getFileName().empty())
                            {
                                std::string fileName = image->getFileName();
                                if (std::find(_texturePaths.begin(), _texturePaths.end(), fileName) == _texturePaths.end())
                                {
                                    _texturePaths.push_back(fileName);
                                }
                            }
                        }
                    }
                }
            }
        }
        osg::NodeVisitor::traverse(geode);
    }

    void apply(osg::Group& group) override
    {
        osg::StateSet* ss = group.getStateSet();
        if (ss)
        {
            for (unsigned int j = 0; j < ss->getTextureAttributeList().size(); ++j)
            {
                osg::Texture* texture = dynamic_cast<osg::Texture*>(ss->getTextureAttribute(j, osg::StateAttribute::TEXTURE));
                if (texture && texture->getImage(0))
                {
                    osg::Image* image = texture->getImage(0);
                    if (!image->getFileName().empty())
                    {
                        std::string fileName = image->getFileName();
                        if (std::find(_texturePaths.begin(), _texturePaths.end(), fileName) == _texturePaths.end())
                        {
                            _texturePaths.push_back(fileName);
                        }
                    }
                }
            }
        }
        osg::NodeVisitor::traverse(group);
    }

private:
    std::vector<std::string>& _texturePaths;
};

bool collectTexturePaths(osg::Node* node, std::vector<std::string>& texturePaths)
{
    if (!node)
        return false;

    TextureCollector collector(texturePaths);
    node->accept(collector);
    return true;
}



//     // 使用 osgContext 创建并获取 osg3DCompositeViewer
//     osgContext* context = osgContext::getInstance();
//     if (context)
//     {
//         // 如果复合视口不存在，先创建一个

//         if (!context->get3DViewer())
//         {
//             // 创建复合视口
//             context->createCompositeViewer();
//         }

//         if (context->get3DViewer())
//         {
//             // 获取复合查看器实例
//             osgQtCompositeViewer* compositeViewer = dynamic_cast<osgQtCompositeViewer*>(context->get3DViewer());
//             if (compositeViewer)
//             {
//                 // 连接osgQtCompositeViewer的threeDSceneRemovePlatform_signal信号到Mte3DService的sig_platformRemoved信号
//                 connect(compositeViewer, &osgQtCompositeViewer::threeDSceneRemovePlatform_signal,
//                         this, &Mte3DService::sig_platformRemoved);
//             }
            
//             // 添加视图并获取viewId
//             context->addViewToComposite();
            
//             // 初始化鼠标拣选处理器
//             context->initMouseIntersectionHandler();

//             // 返回复合查看器的widget指针
//             return context->get3DViewer();
//         }

//     }
//     return nullptr;
// }


void Mte3DService::prepareSceneRoots()
{
    // 如果已经准备好则直接返回
    if (_top3Droot.valid()) return;

    _top3Droot = new osg::Group();

    // 添加地面
    // osg::ref_ptr<osg::Geode> ground = createGround(_groundSize);
    // ground->setName("ground");
    // _top3Droot->addChild(ground);
    
    // 添加坐标轴
    // osg::ref_ptr<osg::Geode> axis = createAixs(_axisSize);
    // axis->setName("axis");
    // _top3Droot->addChild(axis);
    
    // 添加网格
    // osg::ref_ptr<osg::Geode> grid = createGrid(_gridSize, _gridSize / 10.0f);  // 步长为网格大小的1/10
    // grid->setName("grid");
    // _top3Droot->addChild(grid);


    // 添加天空盒（应该最先添加，作为背景）
    // osg::ref_ptr<osg::Node> skybox = createSkyBox("data/Images/sky/posx.jpg", "data/Images/sky/negx.jpg",
    //                                             "data/Images/sky/posy.jpg", "data/Images/sky/negy.jpg",
    //                                             "data/Images/sky/posz.jpg", "data/Images/sky/negz.jpg");
    
    // osg::ref_ptr<osg::Node> skybox = createSkyCubeMap("data/Images/sky/cubemap_posx.jpg", "data/Images/sky/cubemap_negx.jpg",
    //                                             "data/Images/sky/cubemap_posy.jpg", "data/Images/sky/cubemap_negy.jpg",
    //                                             "data/Images/sky/cubemap_posz.jpg", "data/Images/sky/cubemap_negz.jpg");


    // 1. 先添加天空盒（最先渲染）
    // osg::ref_ptr<osg::Node> skybox = createSkyBox("data/Images/sky/skymap.jpg"); // 单张全景图路径
    // if (skybox) {
    //    _top3Droot->addChild(skybox);
    // } else {
    //    OSG_WARN << "天空盒创建失败，请检查skymap.jpg路径！" << std::endl;
    // }


    // 添加默认方向光
    // osg::ref_ptr<osg::Light> light = new osg::Light;
    // light->setLightNum(0);
    // light->setDiffuse(osg::Vec4(0.8f, 0.8f, 0.8f, 1.0f));
    // light->setSpecular(osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    // light->setAmbient(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));
    // light->setDirection(osg::Vec3(-1.0f, -1.0f, -1.0f));
    
    // osg::ref_ptr<osg::LightSource> lightSource = new osg::LightSource;
    // lightSource->setLight(light);
    // lightSource->setReferenceFrame(osg::LightSource::ABSOLUTE_RF);
    // lightSource->setStateSetModes(*_top3Droot->getOrCreateStateSet(), osg::StateAttribute::ON);
    // _top3Droot->addChild(lightSource);

}

// void Mte3DService::setCameraLocation(const QVector3D& cameraPos, double heading, double pitch, double range, double duration)
// {
//     // 如果range为-1（默认值），则使用高度作为range
//     double actualRange = (range < 0) ? cameraPos.z() : range;
// }


// QMap<int, osg::ref_ptr<osg::Geode>> Mte3DService::getModelBox()
// {
//     return  platformBoxMap;
// }




// void Mte3DService::setPlatformHidden(int platformID, bool is_hidden)
// {
//     if (platformMatrixMap.contains(platformID))
//     {
//         if (is_hidden)
//         {
//             platformMatrixMap[platformID]->setNodeMask(0);
//         }
//         else
//         {
//             platformMatrixMap[platformID]->setNodeMask(1);
//         }
//     }
// }

// void Mte3DService::updatePlatformPos(int platformID, const QVector3D& platformPos)
// {
//     if (platformTransMap.contains(platformID))
//     {
//         platformTransMap[platformID] = osg::Vec3d(platformPos.x(), platformPos.y(), platformPos.z());
//         setPlatformMatrix(platformID, platformScaleMap[platformID], platformPostureMap[platformID], platformTransMap[platformID]);
//     }
// }

// void Mte3DService::updatePlatformPosture(int platformID, const QVector3D& platPostureDeg)
// {
//     if (platformPostureMap.contains(platformID))
//     {
//         platformPostureMap[platformID] = osg::Vec3d(platPostureDeg.x(), platPostureDeg.y(), platPostureDeg.z());
//         setPlatformMatrix(platformID, platformScaleMap[platformID], platformPostureMap[platformID], platformTransMap[platformID]);
//     }
// }

// void Mte3DService::updatePlatformPosPosture(int platformID, const QVector3D& platformPos, const QVector3D& platformPosture)
// {
//     updatePlatformPos(platformID, platformPos);
//     updatePlatformPosture(platformID, platformPosture);
// }

// void Mte3DService::setPlatformScale(int platformID, double m_scale)
// {
//     if (platformScaleMap.contains(platformID))
//     {
//         platformScaleMap[platformID] = osg::Vec3d(m_scale, m_scale, m_scale);
//         setPlatformMatrix(platformID, platformScaleMap[platformID], platformPostureMap[platformID], platformTransMap[platformID]);
//     }
// }

// QVector3D Mte3DService::getPlatformPos(int platformID)
// {
//     QVector3D qVec;
//     if (platformTransMap.contains(platformID))
//     {
//         qVec.setX(platformTransMap[platformID].x());
//         qVec.setY(platformTransMap[platformID].y());
//         qVec.setZ(platformTransMap[platformID].z());
//     }
//     return qVec;
// }

// QVector3D Mte3DService::getPlatformPosture(int platformID)
// {
//     QVector3D qVec;
//     if (platformPostureMap.contains(platformID))
//     {
//         qVec.setX(platformPostureMap[platformID].x());
//         qVec.setY(platformPostureMap[platformID].y());
//         qVec.setZ(platformPostureMap[platformID].z());
//     }

//     return qVec;
// }

// QVector3D Mte3DService::getPlatformScale(int platformID)
// {
//     QVector3D qVec;
//     if (platformScaleMap.contains(platformID))
//     {
//         qVec.setX(platformScaleMap[platformID].x());
//         qVec.setY(platformScaleMap[platformID].y());
//         qVec.setZ(platformScaleMap[platformID].z());
//     }

//     return qVec;
// }

// osg::ref_ptr<osg::MatrixTransform> Mte3DService::getPlatformMatrix(int platformID)
// {
//     return platformMatrixMap[platformID];
// }

// int Mte3DService::getPlatformMatrixID(osg::ref_ptr<osg::MatrixTransform> m_matrix)
// {
//     for (auto it = platformMatrixMap.begin(); it != platformMatrixMap.end(); it++)
//     {
//         if (it.value() == m_matrix)
//         {
//             return it.key();
//         }
//     }

//     return -1;
// }

// MtePlatformStru Mte3DService::getPlatformInfo(int platformID)
// {

//     return platformMap[platformID];
// }


// QMap<int, MtePlatformStru> Mte3DService::getAllPlatformInfo()
// {
//     return platformMap;
// }


// QVector3D Mte3DService::getCurPlatPos(int platformID)
// {
//     QVector3D curPos;
//     if (platformMatrixMap.contains(platformID))
//     {
//         osg::ref_ptr<osg::MatrixTransform>pMatrx = platformMatrixMap[platformID];
//         if (pMatrx)
//         {
//             osg::Matrix matrix = pMatrx->getMatrix();
//             osg::Vec3d m_Trans;
//             osg::Vec3d m_Scale;
//             osg::Quat m_Rotate;
//             osg::Quat m_So;
//             matrix.decompose(m_Trans, m_Rotate, m_Scale, m_So);


//             curPos.setX(m_Trans.x());
//             curPos.setY(m_Trans.y());
//             curPos.setZ(m_Trans.z());
//         }
//     }
//     return curPos;
// }


// osg::Geode* Mte3DService::createBoundingBoxGeode(osg::Node* node)
// {
//     // 参考用户提供的代码，直接获取世界坐标系下的包围盒
//     osg::ComputeBoundsVisitor cbv;
//     node->accept(cbv);
//     osg::BoundingBox bb = cbv.getBoundingBox();

//     // 检查包围盒是否有效
//     if (bb.valid())
//     {
//         // 创建包围盒几何体，中心位于包围盒中心
//         // 注意：这个包围盒将作为独立节点添加到场景中，不会被再次应用变换
//         osg::Box* box = new osg::Box(bb.center(), bb.xMax() - bb.xMin(), bb.yMax() - bb.yMin(), bb.zMax() - bb.zMin());
//         osg::ShapeDrawable* shape = new osg::ShapeDrawable(box);
//         shape->setColor(osg::Vec4(1.0, 0.0, 0.0, 1.0)); // 红色

//         // 设置线框模式
//         osg::PolygonMode* pm = new osg::PolygonMode();
//         pm->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
//         osg::StateSet* stateSet = shape->getOrCreateStateSet();
//         stateSet->setAttributeAndModes(pm);
//         stateSet->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
//         // 禁用深度写入，确保包围盒不会影响grid和axis的显示
//         stateSet->setMode(GL_DEPTH_WRITEMASK, osg::StateAttribute::OFF);
//         // 启用深度测试，确保包围盒正确显示在模型周围
//         stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

//         osg::Geode* geode = new osg::Geode();
//         geode->addDrawable(shape);
//         return geode;
//     }

//     // 如果包围盒无效，创建一个默认大小的包围盒
//     osg::Box* box = new osg::Box(osg::Vec3d(0, 0, 0), 1.0, 1.0, 1.0);
//     osg::ShapeDrawable* shape = new osg::ShapeDrawable(box);
//     shape->setColor(osg::Vec4(1.0, 0.0, 0.0, 1.0));
    
//     osg::PolygonMode* pm = new osg::PolygonMode();
//     pm->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
//     shape->getOrCreateStateSet()->setAttributeAndModes(pm);
//     shape->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

//     osg::Geode* geode = new osg::Geode();
//     geode->addDrawable(shape);
//     return geode;
// }



// osg::ref_ptr<osg::Node> Mte3DService::getPlatformModel(int id) const
// {
//     return platformModelMap[id];
// }


// helper: 使用 (heading, pitch, roll) -> osg::Quat（heading: Z, pitch: Y, roll: X）
static osg::Quat eulerHPRAnglesToQuat(double headingDeg, double pitchDeg, double rollDeg)
{
    double h = osg::DegreesToRadians(headingDeg);
    double p = osg::DegreesToRadians(pitchDeg);
    double r = osg::DegreesToRadians(rollDeg);
    osg::Quat q_heading(h, osg::Z_AXIS);
    osg::Quat q_pitch(p, osg::Y_AXIS);
    osg::Quat q_roll(r, osg::X_AXIS);
    return q_heading * q_pitch * q_roll;
}


// void Mte3DService::updateBoundingBoxForPlatform(int platformID)
// {
//     // 确保存在必要对象
//     if ( !platformMatrixMap.contains(platformID))
//         return;

//     osg::ref_ptr<osg::MatrixTransform> mt = platformMatrixMap[platformID];
//     if (!mt) return;

//     // 移除旧的包围盒
//     osg::Node* oldBox = nullptr;
//     if (platformBoxMap.contains(platformID))
//     {
//         oldBox = platformBoxMap[platformID];
//         if (oldBox)
//         {
//             // 移除旧包围盒
//             osg::Node::ParentList parents = oldBox->getParents();
//             for (auto parent : parents)
//             {
//                 parent->removeChild(oldBox);
//             }
//         }
//     }

//     // 保留旧包围盒的可见性（node mask）
//     unsigned int oldMask = 0xFFFFFFFF;
//     if (oldBox)
//         oldMask = oldBox->getNodeMask();

//     // 创建新的包围盒 Geode（基于模型节点，而不是MatrixTransform）
//     osg::Node* modelNode = platformModelMap[platformID];
//     osg::Geode* newBox = createBoundingBoxGeode(modelNode);
//     if (!newBox) return;

//     // 设置与之前相同的 node mask（保持显示/隐藏状态）
//     newBox->setNodeMask(oldMask);

//     // 将新包围盒添加到MatrixTransform节点
//     mt->addChild(newBox);

//     // 更新 platformBoxMap（注意 platformBoxMap 存的是裸指针）
//     platformBoxMap.insert(platformID, newBox);
// }

// void Mte3DService::setPlatformMatrix(int platformID, osg::Vec3d mscale, osg::Vec3d mposture, osg::Vec3d mtrans)
// {
//     if (!platformMatrixMap.contains(platformID) || !platformMatrixMap[platformID])
//     {
//         std::cout << "[Mte3DService] setPlatformMatrix: platformID=" << platformID << " not found!" << std::endl;
//         return;
//     }

//     // 1. 构造旋转四元数（假设 eulerHPRAnglesToQuat 是 H-P-R 顺序）
//     osg::Quat q = eulerHPRAnglesToQuat(mposture.x(), mposture.y(), mposture.z());
//     osg::ref_ptr<osg::MatrixTransform> mt = platformMatrixMap[platformID];

//     // 2. 获取模型局部几何中心（必须是加载时计算的包围盒中心）
//     osg::Vec3d localModelCenter(0, 0, 0);
//     if (platformCenterMap.contains(platformID))
//     {
//         localModelCenter = platformCenterMap[platformID];
//     }
//     else
//     {
//         // 如果没有中心，默认为 (0,0,0)，但会退化为绕世界原点
//         std::cout << "[Mte3DService] WARNING: No local center for platformID=" << platformID << std::endl;
//     }


//     // ✅ 正确的变换顺序
//     osg::Matrix toOrigin   = osg::Matrix::translate(-localModelCenter); // 移到局部原点
//     osg::Matrix scaleM     = osg::Matrix::scale(mscale);                // 缩放
//     osg::Matrix rotateM    = osg::Matrix::rotate(q);                    // 旋转
//     osg::Matrix toWorld    = osg::Matrix::translate(mtrans + localModelCenter); // 平移到世界位置并加上模型中心偏移

//     osg::Matrix finalMatrix =  rotateM * scaleM * toOrigin * toWorld;

//     mt->setMatrix(finalMatrix);

//     // --- 同步内部状态缓存 ---
//     platformScaleMap[platformID]   = mscale;
//     platformPostureMap[platformID] = mposture;
//     platformTransMap[platformID]   = mtrans;

//     // 更新平台属性（用于UI/保存）
//     if (platformMap.contains(platformID))
//     {
//         MtePlatformStru& plat = platformMap[platformID];
//         plat.m_attribute.m_x = mtrans.x();
//         plat.m_attribute.m_y = mtrans.y();
//         plat.m_attribute.m_z = mtrans.z();
//         plat.m_attribute.m_h = mposture.x();
//         plat.m_attribute.m_p = mposture.y();
//         plat.m_attribute.m_r = mposture.z();
//         plat.m_attribute.m_scale = mscale.x(); // 假设等比缩放
//     }

//     // 更新包围盒（可选，但推荐）
//     updateBoundingBoxForPlatform(platformID);

//     // 发出信号通知UI刷新
//     if (platformMap.contains(platformID)) {
//         emit sig_platformTransformChanged(
//             platformID,
//             platformMap[platformID].m_attribute.m_x,
//             platformMap[platformID].m_attribute.m_y,
//             platformMap[platformID].m_attribute.m_z,
//             platformMap[platformID].m_attribute.m_h,
//             platformMap[platformID].m_attribute.m_p,
//             platformMap[platformID].m_attribute.m_r,
//             platformMap[platformID].m_attribute.m_scale
//         );
//     }
// }

// 缩放平台（滚轮缩放）
// void Mte3DService::scalePlatformByFactor(int platformID, double factor)
// {
//     if (!platformScaleMap.contains(platformID)) {
//         std::cout << "[Mte3DService] scalePlatformByFactor: platformID=" << platformID << " not found in platformScaleMap" << std::endl;
//         return;
//     }

//     // 获取当前缩放值（等比缩放）
//     osg::Vec3d curScale = platformScaleMap[platformID];
//     osg::Vec3d newScale = curScale * factor; // 等比缩放，x/y/z同比例

//     std::cout << "[Mte3DService] scalePlatformByFactor: platformID=" << platformID
//               << ", factor=" << factor
//               << ", curScale=(" << curScale.x() << ", " << curScale.y() << ", " << curScale.z() << ")"
//               << ", newScale=(" << newScale.x() << ", " << newScale.y() << ", " << newScale.z() << ")" << std::endl;

//     // 复用setPlatformMatrix，确保绕自身中心缩放
//     setPlatformMatrix(platformID,
//                       newScale,
//                       platformPostureMap[platformID], // 姿态不变
//                       platformTransMap[platformID]); // 位置不变
// }

// 旋转平台（中键拖动）
// void Mte3DService::rotatePlatformByDelta(int platformID, double deltaH, double deltaP, double deltaR)
// {
//     std::cout << "[Mte3DService] rotatePlatformByDelta: platformID=" << platformID
//               << ", deltaH=" << deltaH << ", deltaP=" << deltaP << ", deltaR=" << deltaR << std::endl;
    
//     if (!platformPostureMap.contains(platformID))
//     {
//         std::cout << "[Mte3DService] rotatePlatformByDelta: platformID=" << platformID << " not found in platformPostureMap" << std::endl;
//         return;
//     }

//     // 获取当前姿态
//     osg::Vec3d curPosture = platformPostureMap[platformID];
//     osg::Vec3d newPosture(
//         curPosture.x() + deltaH,
//         curPosture.y() + deltaP,
//         curPosture.z() + deltaR
//     );

//     std::cout << "[Mte3DService] rotatePlatformByDelta: curPosture=('" << curPosture.x() << ", " << curPosture.y() << ", " << curPosture.z() << ")"
//               << ", newPosture=('" << newPosture.x() << ", " << newPosture.y() << ", " << newPosture.z() << ")" << std::endl;
    
//     // 检查platformCenterMap是否包含该平台ID
//     if (platformCenterMap.contains(platformID))
//     {
//         osg::Vec3d localCenter = platformCenterMap[platformID];
//         std::cout << "[Mte3DService] rotatePlatformByDelta: platformCenterMap contains platformID=" << platformID
//                   << ", localCenter=('" << localCenter.x() << ", " << localCenter.y() << ", " << localCenter.z() << ")" << std::endl;
//     }
//     else
//     {
//         std::cout << "[Mte3DService] rotatePlatformByDelta: WARNING - platformCenterMap does not contain platformID=" << platformID << std::endl;
//     }
    
//     // 复用setPlatformMatrix，确保绕自身中心旋转
//     setPlatformMatrix(platformID,
//                       platformScaleMap[platformID],    // 缩放不变
//                       newPosture,
//                       platformTransMap[platformID]); // 位置不变
// }

// 平移平台（左键拖动）
// void Mte3DService::translatePlatformByDelta(int platformID, double deltaX, double deltaY, double deltaZ)
// {
//     if (!platformTransMap.contains(platformID)) return;

//     // 获取当前位置
//     osg::Vec3d curTrans = platformTransMap[platformID];
//     // 计算新位置，确保Z坐标不小于0
//     osg::Vec3d newTrans(
//         curTrans.x() + deltaX,
//         curTrans.y() + deltaY,
//         std::max(0.0, curTrans.z() + deltaZ) // 确保Z坐标不小于0
//     );

//     // 复用setPlatformMatrix，确保位置平移不影响旋转/缩放中心
//     setPlatformMatrix(platformID,
//                       platformScaleMap[platformID],    // 缩放不变
//                       platformPostureMap[platformID], // 姿态不变
//                       newTrans);
// }


// QMap<int, osg::Vec3d> Mte3DService::getPlatformScaleMap()
// {
//     return platformScaleMap;
// }

// QMap<int, osg::Vec3d> Mte3DService::getPlatformPostureMap()
// {
//     return platformPostureMap;
// }

// QMap<int, osg::Vec3d> Mte3DService::getPlatformTransMap()
// {
//     return platformTransMap;
// }

// 获取平台当前经纬高（lat, lon, alt）
// osg::Vec3d Mte3DService::getPlatformTrans(int platformID) const
// {
//     if (platformTransMap.contains(platformID))
//         return platformTransMap[platformID];
//     return osg::Vec3d(); // 无效时返回默认值
// }

// void Mte3DService::selectPlatform(int platformID)
// {
//     // 如果已选中相同 id，直接返回
//     if (m_selectedPlatformId == platformID) return;

//     // 隐藏所有包围盒
//     for (auto it = platformBoxMap.begin(); it != platformBoxMap.end(); ++it)
//     {
//         if (it.value())
//             it.value()->setNodeMask(0);
//     }

//     // 显示目标包围盒（如果存在）
//     if (platformBoxMap.contains(platformID) && platformBoxMap[platformID])
//     {
//         platformBoxMap[platformID]->setNodeMask(1);
//     }

//     m_selectedPlatformId = platformID;
//     emit sig_platformSelected(platformID);
// }

// void Mte3DService::deselectPlatform()
// {
//     // 隐藏所有包围盒
//     for (auto it = platformBoxMap.begin(); it != platformBoxMap.end(); ++it)
//     {
//         if (it.value())
//             it.value()->setNodeMask(0);
//     }

//     m_selectedPlatformId = -1;
//     emit sig_platformSelected(-1);
// }

// int Mte3DService::getSelectedPlatformId() const
// {
//     return m_selectedPlatformId;
// }

/*int Mte3DService::deletePlatform(int platformID)
{
		// 从分组中移除节点（如果平台属于某个分组）
		if (platformToGroupMap.contains(platformID))
		{
			QString groupName = platformToGroupMap[platformID];
			if (groupNodeMap.contains(groupName))
			{
				// 从分组节点中移除平台的MatrixTransform节点
				if (platformMatrixMap.contains(platformID))
				{
					groupNodeMap[groupName]->removeChild(platformMatrixMap[platformID]);
				}
			}
			platformToGroupMap.remove(platformID);
		}
        
        // 移除包围盒节点
        if (platformBoxMap.contains(platformID))
        {
            osg::ref_ptr<osg::Geode> boxGeode = platformBoxMap[platformID];
            if (boxGeode)
            {
                // 查找包围盒节点的父节点并移除它
                osg::Node::ParentList parents = boxGeode->getParents();
                for (auto parent : parents)
                {
                    parent->removeChild(boxGeode);
                }
            }
        }
        
        //不需要从MapNode和ShadowCastingGroup直接移除，因为它们是通过groupNode添加的
		//删除平台相关
        platformMatrixMap.remove(platformID);
        platformMap.remove(platformID);
        platformScaleMap.remove(platformID);
        platformPostureMap.remove(platformID);
        platformTransMap.remove(platformID);
        platformModelMap.remove(platformID);
        lightMap.remove(platformID);
        platformBoxMap.remove(platformID);

        emit sig_platformListChanged();
        return 0;

}*/

// int Mte3DService::getPlatformBoxHeight(int platformId)
// {
//     // 尝试从platformBoxMap获取包围盒高度并添加到位置高度上
//     if (platformBoxMap.contains(platformId)) {
//         osg::ref_ptr<osg::Geode> boxGeode = platformBoxMap[platformId];
//         if (boxGeode.valid() /*&& !boxGeode->getDrawableList().empty()*/) {
//             osg::ShapeDrawable* shapeDrawable = dynamic_cast<osg::ShapeDrawable*>(boxGeode->getDrawable(0));
//             if (shapeDrawable) {
//                 osg::Box* box = dynamic_cast<osg::Box*>(shapeDrawable->getShape());
//                 if (box) {
//                     // 获取包围盒的高度，并添加到位置高度上
//                     double boxHeight = box->getHalfLengths().z() * 2.0; // 半长的2倍就是整个高度
//                     return boxHeight += 8;
//                 }
//             }
//         }
//     }

//     return 20;
// }


int Mte3DService::addPlatform(const MtePlatformStru& platform)
{
    // return addPlatform(platform, false);

    return addPlatform(platform.m_id, platform.m_path,
                       QVector3D(platform.m_attribute.m_x, platform.m_attribute.m_y, platform.m_attribute.m_z ),
                       QVector3D (0,0,0),
                       1,
                       false);
}


int Mte3DService::addPlatform(int id, const QString& modelPath, QVector3D position, QVector3D posture, float scale, bool fast)
{
    int platformID = id;

    std::cout << "========================================" << std::endl;
    std::cout << "[addPlatform] START - platformID=" << platformID << std::endl;
    std::cout << "[addPlatform] Model file path: " << modelPath.toStdString() << std::endl;
    std::cout << "[addPlatform] Initial position: (" << position.x() << ", "
              << position.y() << ", " << position.z() << ")" << std::endl;
    std::cout << "[addPlatform] Initial posture: (h=" << posture.x() << ", p="
              << posture.y() << ", r=" << posture.z() << ")" << std::endl;
    std::cout << "[addPlatform] Initial scale: " << scale << std::endl;

    osg::ref_ptr<osgDB::Options> readOptions = new osgDB::Options;
    readOptions->setOptionString("ReadFullMaterialProperties true");
    readOptions->setOptionString("LoadTextures true");
    readOptions->setPluginStringData("obj", "y-up=true");

    std::cout << "[addPlatform] Loading model from: " << modelPath.toStdString() << std::endl;

    osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(modelPath.toStdString(), readOptions);

    if (!node) {
        std::cout << "[addPlatform] ERROR: Failed to load model!" << std::endl;
        return -1;
    }
    else {
        std::cout << "[addPlatform] Model loaded successfully!" << std::endl;

        if (true) {
            osgUtil::Optimizer optimizer;
            optimizer.optimize(node.get(), osgUtil::Optimizer::MERGE_GEOMETRY |
                                         osgUtil::Optimizer::REMOVE_REDUNDANT_NODES);
        }

        osg::ComputeBoundsVisitor cbv;
        node->accept(cbv);
        osg::BoundingBox boundingBox = cbv.getBoundingBox();
        std::cout << "[addPlatform] Model bounding box:" << std::endl;
        std::cout << "  Center: (" << boundingBox.center().x() << ", "
                  << boundingBox.center().y() << ", " << boundingBox.center().z() << ")" << std::endl;
    }

    osg::ref_ptr<osg::MatrixTransform> pat = new osg::MatrixTransform;
    pat->addChild(node);

    _top3Droot->addChild(pat);

    platformMatrixMap[platformID] = pat;
    platformModelMap[platformID] = node;
    // platformPathMap[platformID] = modelPath;

    if (node.valid()) {
        osg::ComputeBoundsVisitor cbv;
        node->accept(cbv);
        osg::BoundingBox boundingBox = cbv.getBoundingBox();
        platformCenterMap[platformID] = boundingBox.center();
        std::cout << "[addPlatform] Saved platformCenterMap[" << platformID << "] = ("
                  << boundingBox.center().x() << ", " << boundingBox.center().y() << ", "
                  << boundingBox.center().z() << ")" << std::endl;
    }

    osg::Vec3d initialPos(position.x(), position.y(), position.z());
    osg::Vec3d initialPosture(posture.x(), posture.y(), posture.z());
    osg::Vec3d initialScale(scale, scale, scale);

    platformTransMap[platformID] = initialPos;
    platformPostureMap[platformID] = initialPosture;
    platformScaleMap[platformID] = initialScale;

    setPlatformMatrix(platformID, QVector3D(scale, scale, scale), posture, position);

    std::cout << "[addPlatform] END - platformID=" << platformID << " added successfully!" << std::endl;
    std::cout << "========================================" << std::endl;

    return platformID;
}


void Mte3DService::setPlatformMatrix(int platformID, QVector3D scale, QVector3D posture, QVector3D translation)
{
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "[setPlatformMatrix] START - platformID=" << platformID << std::endl;
    std::cout << "[setPlatformMatrix] Input parameters:" << std::endl;
    std::cout << "  scale: (" << scale.x() << ", " << scale.y() << ", " << scale.z() << ")" << std::endl;
    std::cout << "  posture: (" << posture.x() << ", " << posture.y() << ", " << posture.z() << ")" << std::endl;
    std::cout << "  trans: (" << translation.x() << ", " << translation.y() << ", " << translation.z() << ")" << std::endl;

    if (!platformMatrixMap.contains(platformID) || !platformMatrixMap[platformID])
    {
        std::cout << "[setPlatformMatrix] ERROR: platformID=" << platformID << " not found in platformMatrixMap!" << std::endl;
        return;
    }

    std::cout << "[setPlatformMatrix] Found MatrixTransform for platformID=" << platformID << std::endl;

    osg::Quat q = eulerHPRAnglesToQuat(posture.x(), posture.y(), posture.z());
    osg::ref_ptr<osg::MatrixTransform> mt = platformMatrixMap[platformID];

    osg::Vec3d localModelCenter(0, 0, 0);
    if (platformCenterMap.contains(platformID))
    {
        localModelCenter = platformCenterMap[platformID];
        std::cout << "[setPlatformMatrix] Using platformCenterMap[" << platformID << "] = ("
                  << localModelCenter.x() << ", " << localModelCenter.y() << ", " << localModelCenter.z() << ")" << std::endl;
    }
    else
    {
        std::cout << "[setPlatformMatrix] WARNING: No local center for platformID=" << platformID << ", using (0,0,0)" << std::endl;
    }

    osg::Matrix toOrigin   = osg::Matrix::translate(-localModelCenter);
    osg::Matrix scaleM     = osg::Matrix::scale(scale.x(), scale.y(), scale.z());
    osg::Matrix rotateM    = osg::Matrix::rotate(q);
    osg::Matrix toWorld    = osg::Matrix::translate(osg::Vec3d(translation.x(), translation.y(), translation.z()) + localModelCenter);

    osg::Matrix finalMatrix = rotateM * scaleM * toOrigin * toWorld;

    mt->setMatrix(finalMatrix);
    std::cout << "[setPlatformMatrix] Matrix applied to platformID=" << platformID << std::endl;

    platformScaleMap[platformID]   = osg::Vec3d(scale.x(), scale.y(), scale.z());
    platformPostureMap[platformID] = osg::Vec3d(posture.x(), posture.y(), posture.z());
    platformTransMap[platformID]   = osg::Vec3d(translation.x(), translation.y(), translation.z());

    std::cout << "[setPlatformMatrix] END - platformID=" << platformID << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}

// int Mte3DService::addPlatform(const MtePlatformStru& platform, bool fast)
// {
//     return -1;
//     QString asoPath = platform.m_path;
//     LOG_DEBUG << "model path:" << asoPath;
    
//     osg::ref_ptr<osg::Node> modelNode;
//     if (fast)
//     {
//         // 1. 创建加载选项
//         osg::ref_ptr<osgDB::Options> options = new osgDB::Options();
        
//         // 关键优化1：禁用纹理加载（若模型无纹理/后续手动加载）
//         // options->setOptionString("NO_TEXTURE_LOADING");
        
//         // 关键优化2：合并重复顶点（减少顶点数量，降低内存）
//         // options->setOptionString("MERGE_GEOMETRY=ON");
        
//         // 关键优化3：使用内存池（减少小对象内存分配开销）
//         // options->setObjectCache(new osgDB::ObjectCache());
        
//         // 关键优化4：禁用法线计算（若OBJ已有法线/不需要法线）
//         // options->setOptionString("NO_NORMALS");
        
//         // 关键优化5：设置线程池（多线程解析，CPU密集场景提速）
//         // options->setNumThreadsHint(4); // 根据CPU核心数设置
        

//         osg::ref_ptr<osgDB::Options> readOptions = new osgDB::Options;
//         readOptions->setOptionString("ReadFullMaterialProperties true");
//         readOptions->setOptionString("LoadTextures true");                  //强制加载纹理


//         // 2. 加载模型
//         modelNode = osgDB::readNodeFile(std::string(asoPath.toLocal8Bit()), readOptions);
        
//         // 3. 后续优化：合并Geometry（减少DrawCall）
//         if (modelNode)
//         {
//             osgUtil::Optimizer optimizer;
//             optimizer.optimize(modelNode.get(), osgUtil::Optimizer::MERGE_GEOMETRY |
//                                          osgUtil::Optimizer::REMOVE_REDUNDANT_NODES);
//         }
//     }
//     else
//     {
//         modelNode = osgDB::readNodeFile(std::string(asoPath.toLocal8Bit()));
//     }
    
//     if (!modelNode.valid())
//     {
//         LOG_DEBUG << "read node file failed";
//         return -1002;
//     }

//     // 计算模型的局部几何中心（此时模型还没有应用任何变换）
//     osg::ComputeBoundsVisitor cbv;
//     modelNode->accept(cbv);
//     osg::BoundingBox bb = cbv.getBoundingBox();
//     osg::Vec3d localModelCenter = bb.center();
    
//     // 创建MatrixTransform节点
//     osg::ref_ptr<osg::MatrixTransform> mt = new osg::MatrixTransform;
    
//     // 直接将模型添加到MatrixTransform节点
//     mt->addChild(modelNode);

//     // 使用正确的变换顺序：
//     // 1. 平移模型，使其实体中心位于局部坐标原点（translate(-localModelCenter)）
//     // 2. 缩放模型（scale）
//     // 3. 旋转模型（rotate(q)）
//     // 4. 平移模型到世界坐标的目标位置（translate(initialPos)）
//     // 由于OSG变换矩阵是从右到左应用的，所以变换矩阵的顺序是：
//     // translate(initialPos) * rotate(q) * scale * translate(-localModelCenter)
//     osg::Quat q = eulerHPRAnglesToQuat(platform.m_attribute.m_h, platform.m_attribute.m_p, platform.m_attribute.m_r);
//     osg::Vec3d scale(platform.m_attribute.m_scale, platform.m_attribute.m_scale, platform.m_attribute.m_scale);
//     osg::Vec3d initialPos(platform.m_attribute.m_x, platform.m_attribute.m_y, platform.m_attribute.m_z);
    
//     // 确保初始矩阵顺序与setPlatformMatrix函数中使用的顺序一致
//     // 在OSG中，矩阵乘法是从右到左应用的，所以正确的顺序是：
//     // 1. 先将模型平移，使其实体中心位于局部坐标原点（translate(-localModelCenter)）
//     // 2. 然后缩放模型（绕局部原点缩放）
//     // 3. 然后旋转模型（绕局部原点旋转）
//     // 4. 最后将模型平移到世界坐标的目标位置（translate(initialPos + localModelCenter)）
//     // 这样缩放和旋转不会影响模型的最终位置
    
//     osg::Matrix initialMatrix =
//         osg::Matrix::translate(initialPos) *   // 4. 平移到世界位置并加上模型中心偏移
//         osg::Matrix::rotate(q) *                           // 3. 绕局部原点旋转
//         osg::Matrix::scale(scale) *                       // 2. 绕局部原点缩放
//         osg::Matrix::translate(-localModelCenter);         // 1. 平移到局部原点
        
//     mt->setMatrix(initialMatrix);
//     std::cout << "[Mte3DService] addPlatform: Initial matrix order: translate(initialPos) * rotate(q) * scale * translate(-localModelCenter)" << std::endl;
    
//     // 存储模型的局部几何中心到platformCenterMap中
//     platformCenterMap.insert(platform.m_id, localModelCenter);
    
//     // 初始化平台的变换属性映射
//     platformTransMap.insert(platform.m_id, initialPos);
//     platformPostureMap.insert(platform.m_id, osg::Vec3d(platform.m_attribute.m_h, platform.m_attribute.m_p, platform.m_attribute.m_r));
//     platformScaleMap.insert(platform.m_id, scale);
    
//     std::cout << "[Mte3DService] addPlatform: platformID=" << platform.m_id << ", localModelCenter=('" << localModelCenter.x() << ", " << localModelCenter.y() << ", " << localModelCenter.z() << ")" << std::endl;
//     std::cout << "[Mte3DService] addPlatform: Initial posture=('" << platform.m_attribute.m_h << ", " << platform.m_attribute.m_p << ", " << platform.m_attribute.m_r << ")" << std::endl;
//     std::cout << "[Mte3DService] addPlatform: Initial scale=('" << scale.x() << ", " << scale.y() << ", " << scale.z() << ")" << std::endl;
//     std::cout << "[Mte3DService] addPlatform: Initial pos=('" << initialPos.x() << ", " << initialPos.y() << ", " << initialPos.z() << ")" << std::endl;
    
//     mt->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);
//     mt->getOrCreateStateSet()->setMode(GL_LIGHT0, osg::StateAttribute::ON);
//     mt->getOrCreateStateSet()->setMode(GL_LIGHT1, osg::StateAttribute::ON);


//     // 添加包围盒
//     osg::Geode* m_Geode = createBoundingBoxGeode(modelNode); // 使用原始模型节点计算包围盒
//     platformBoxMap.insert(platform.m_id, m_Geode);
//     m_Geode->setNodeMask(0);
//     // 将包围盒添加到MatrixTransform节点中，使其跟随平台一起移动和旋转
//     mt->addChild(m_Geode);

// 	// 实现分组管理
// 	// 创建分组名称（基于ModelType和m_path）
// 	QString groupName = createGroupName(platform.modelType, platform.m_path);
// 	// 获取或创建分组节点
// 	osg::ref_ptr<osg::Group> groupNode = getOrCreateGroupNode(groupName);
//     // 将MatrixTransform节点添加到分组节点
//     groupNode->addChild(mt);
// 	// 记录平台到分组的映射关系
// 	platformToGroupMap.insert(platform.m_id, groupName);
	
//     platformModelMap.insert(platform.m_id, modelNode);
//     platformMap.insert(platform.m_id, platform);
//     platformMatrixMap.insert(platform.m_id, mt);

//     // 自动调整地面、坐标轴和网格大小
//     float maxModelSize = calculateMaxModelSize();
//     // 如果最大模型尺寸超过当前地面一半，则更新地面大小
//     if (maxModelSize > _groundSize / 2.0f) {
//         // 计算新的地面大小：取最大模型尺寸的2倍，并向上取整到10的倍数
//         float newSize = static_cast<float>(std::ceil(maxModelSize * 2.0f / 10.0f) * 10.0f);
//         updateGroundAndGridSize(newSize);
//         // 同时更新天空节点大小
//         updateSkyNodeSize(newSize);
//     }

//     emit sig_platformListChanged();
//     return 0;
// }



// 分组管理相关方法实现
// QString Mte3DService::createGroupName(ModelType modelType, const QString& path)
// {
//     // 根据ModelType获取对应的字符串表示
//     QString typeStr;
//     switch (modelType)
//     {
//     case ModelType::OBJECT:
//         typeStr = "OBJECT";
//         break;
//     case ModelType::BACKGROUND:
//         typeStr = "BACKGROUND";
//         break;
//     case ModelType::GEO:
//         typeStr = "GEO";
//         break;
//     default:
//         typeStr = "UNKNOWN";
//         break;
//     }
    
//     // 创建分组名称，格式为：ModelType_m_path
//     QString groupName = typeStr + "_" + path;
//     return groupName;
// }
// osg::ref_ptr<osg::Group> Mte3DService::getOrCreateGroupNode(const QString& groupName)
// {
//     // 如果分组节点已存在，直接返回
//     if (groupNodeMap.contains(groupName))
//     {
//         return groupNodeMap[groupName];
//     }
    
//     // 创建新的分组节点
//     osg::ref_ptr<osg::Group> newGroupNode = new osg::Group;
//     newGroupNode->setName(groupName.toStdString());
    
//     // 将新创建的分组节点添加到顶层根节点中
//     if (_top3Droot)
//     {
//         _top3Droot->addChild(newGroupNode);
//     }
    
//     // 记录分组节点
//     groupNodeMap.insert(groupName, newGroupNode);
    
//     return newGroupNode;
// }

// void Mte3DService::deletePlatformsByGroup(const QString& groupName)
// {
//     //QMutexLocker locker(&_modelMutex); // 添加互斥锁确保线程安全
    
//     if (!groupNodeMap.contains(groupName))
//     {
//         return; // 分组不存在，直接返回
//     }
    
//     // 获取分组下的所有平台ID（优化版）
//     QVector<int> platformIds;
//     QMutableMapIterator<int, QString> it(platformToGroupMap);
//     while (it.hasNext())
//     {
//         it.next();
//         if (it.value() == groupName)
//         {
//             platformIds.append(it.key());
//             // 预先从映射中移除，避免deletePlatform中的再次查找
//             it.remove();
//         }
//     }
    
//     // 批量删除平台节点
//     // 先从所有相关的映射表中移除平台数据
//     for (int platformID : platformIds)
//     {
//         // 从所有相关映射表中移除
//         platformBoxMap.remove(platformID);
//         platformModelMap.remove(platformID);
//         platformMap.remove(platformID);
//         // platformGeoMap.remove(platformID);
//         platformMatrixMap.remove(platformID);
//         platformScaleMap.remove(platformID);
//         platformPostureMap.remove(platformID);
//         platformTransMap.remove(platformID);

//     }
    
//     // 从场景图中移除分组节点（一次性删除所有节点）
//     osg::ref_ptr<osg::Group> groupNode = groupNodeMap[groupName];
//     groupNode->removeChildren(0, groupNode->getNumChildren()); // 清空分组节点内的所有子节点

//     // 从映射表中移除分组节点
//     groupNodeMap.remove(groupName);
    
//     // 发出信号通知平台列表已更改
//     emit sig_platformListChanged();
// }
// QStringList Mte3DService::getAllGroupNames()
// {
//     return groupNodeMap.keys();
// }
// QVector<int> Mte3DService::getPlatformIdsByGroup(const QString& groupName)
// {
//     QVector<int> platformIds;
    
//     // 遍历平台到分组的映射，找出属于指定分组的所有平台ID
//     QMap<int, QString>::const_iterator it;
//     for (it = platformToGroupMap.constBegin(); it != platformToGroupMap.constEnd(); ++it)
//     {
//         if (it.value() == groupName)
//         {
//             platformIds.append(it.key());
//         }
//     }
    
//     return platformIds;
// }
// QString Mte3DService::getPlatformGroupName(int platformID)
// {
//     if (platformToGroupMap.contains(platformID))
//     {
//         return platformToGroupMap[platformID];
//     }
//     return QString(); // 返回空字符串表示平台不属于任何分组
// }
// // 按模型类型清空所有该类型的分组和节点
// void Mte3DService::deletePlatformsByModelType(ModelType modelType)
// {
// //    QMutexLocker locker(&_modelMutex); // 添加互斥锁确保线程安全
    
//     // 获取指定模型类型下的所有分组名称
//     QStringList groupNames = getGroupNamesByModelType(modelType);
    
//     // 遍历所有分组名称，删除每个分组
//     for (const QString& groupName : groupNames)
//     {
//         // 调用优化后的deletePlatformsByGroup函数删除分组
//         deletePlatformsByGroup(groupName);
//     }
// }
// // 获取指定模型类型下的所有分组名称
// QStringList Mte3DService::getGroupNamesByModelType(ModelType modelType)
// {
//     QStringList result;
//     QString typePrefix;
    
//     // 根据ModelType获取对应的字符串前缀
//     switch (modelType)
//     {
//     case ModelType::OBJECT:
//         typePrefix = "OBJECT_";
//         break;
//     case ModelType::BACKGROUND:
//         typePrefix = "BACKGROUND_";
//         break;
//     case ModelType::GEO:
//         typePrefix = "GEO_";
//         break;
//     default:
//         typePrefix = "UNKNOWN_";
//         break;
//     }
    
//     // 遍历所有分组名称，筛选出指定模型类型的分组
//     QMapIterator<QString, osg::ref_ptr<osg::Group>> it(groupNodeMap);
//     while (it.hasNext())
//     {
//         it.next();
//         if (it.key().startsWith(typePrefix))
//         {
//             result.append(it.key());
//         }
//     }
    
//     return result;
// }


// float Mte3DService::calculateMaxModelSize()
// {
//     float maxSize = 0.0f;

//     // 遍历所有平台模型
//     for (auto it = platformModelMap.begin(); it != platformModelMap.end(); ++it)
//     {
//         osg::ref_ptr<osg::Node> model = it.value();
//         if (!model) continue;

//         // 计算模型的包围盒
//         osg::ComputeBoundsVisitor cbv;
//         model->accept(cbv);
//         osg::BoundingBox bb = cbv.getBoundingBox();
//         float modelWidth = bb.xMax() - bb.xMin();
//         float modelLength = bb.yMax() - bb.yMin();
//         float modelHeight = bb.zMax() - bb.zMin();

//         // 找到模型的最大尺寸
//         float modelSize = std::max(std::max(modelWidth, modelLength), modelHeight);
//         maxSize = std::max(maxSize, modelSize);
//     }

//     return maxSize;
// }

// void Mte3DService::updateSkyNodeSize(float newSize)
// {
//     if (newSize <= 0.0f) return;

//     // 如果场景根节点不存在，直接返回
//     if (!_top3Droot) return;

//     // 计算天空节点的新大小，设置为地面大小的5倍，确保足够大
//     float skySize = newSize * 5.0f;
    
//     // 遍历场景根节点的所有子节点，查找天空节点
//     for (int i = _top3Droot->getNumChildren() - 1; i >= 0; --i)
//     {
//         osg::Node* child = _top3Droot->getChild(i);
//         if (!child) continue;

//         // 检查是否为天空节点（MatrixTransform类型）
//         osg::MatrixTransform* skyTransform = dynamic_cast<osg::MatrixTransform*>(child);
//         if (skyTransform)
//         {
//             // 检查是否有Geode子节点
//             if (skyTransform->getNumChildren() > 0)
//             {
//                 osg::Geode* skyGeode = dynamic_cast<osg::Geode*>(skyTransform->getChild(0));
//                 if (skyGeode && skyGeode->getNumDrawables() > 0)
//                 {
//                     // 检查是否为几何图形
//                     osg::Geometry* skyGeometry = dynamic_cast<osg::Geometry*>(skyGeode->getDrawable(0));
//                     if (skyGeometry)
//                     {
//                         // 更新天空节点的大小
//                         // 1. 获取顶点数组
//                         osg::Vec3Array* vertices = dynamic_cast<osg::Vec3Array*>(skyGeometry->getVertexArray());
//                         if (vertices)
//                         {
//                             // 2. 根据天空盒类型（立方体或球体）更新顶点
//                             bool isSphere = (vertices->size() > 24); // 球体顶点数量远大于立方体(24个)
                            
//                             if (isSphere)
//                             {
//                                 // 球体：根据原始半径调整到新半径
//                                 // 找到原始最大半径
//                                 float oldRadius = 0.0f;
//                                 for (size_t j = 0; j < vertices->size(); ++j)
//                                 {
//                                     float radius = (*vertices)[j].length();
//                                     if (radius > oldRadius)
//                                         oldRadius = radius;
//                                 }
                                
//                                 // 缩放所有顶点到新半径
//                                 float scaleFactor = skySize / oldRadius;
//                                 for (size_t j = 0; j < vertices->size(); ++j)
//                                 {
//                                     (*vertices)[j] *= scaleFactor;
//                                 }
//                             }
//                             else
//                             {
//                                 // 立方体：重新创建顶点数组
//                                 osg::ref_ptr<osg::Vec3Array> newVertices = new osg::Vec3Array;
//                                 float h = skySize * 0.5f; // 半长
//                                 // 6个面的顶点（按右、左、上、下、前、后顺序）
//                                 // +X (POSITIVE_X)
//                                 newVertices->push_back(osg::Vec3(h, -h, -h)); newVertices->push_back(osg::Vec3(h, h, -h));
//                                 newVertices->push_back(osg::Vec3(h, h, h));   newVertices->push_back(osg::Vec3(h, -h, h));
//                                 // -X (NEGATIVE_X)
//                                 newVertices->push_back(osg::Vec3(-h, -h, h)); newVertices->push_back(osg::Vec3(-h, h, h));
//                                 newVertices->push_back(osg::Vec3(-h, h, -h)); newVertices->push_back(osg::Vec3(-h, -h, -h));
//                                 // +Y (POSITIVE_Y)
//                                 newVertices->push_back(osg::Vec3(-h, h, -h)); newVertices->push_back(osg::Vec3(h, h, -h));
//                                 newVertices->push_back(osg::Vec3(h, h, h));   newVertices->push_back(osg::Vec3(-h, h, h));
//                                 // -Y (NEGATIVE_Y)
//                                 newVertices->push_back(osg::Vec3(-h, -h, -h)); newVertices->push_back(osg::Vec3(-h, -h, h));
//                                 newVertices->push_back(osg::Vec3(h, -h, h));   newVertices->push_back(osg::Vec3(h, -h, -h));
//                                 // +Z (POSITIVE_Z)
//                                 newVertices->push_back(osg::Vec3(-h, -h, h)); newVertices->push_back(osg::Vec3(h, -h, h));
//                                 newVertices->push_back(osg::Vec3(h, h, h));   newVertices->push_back(osg::Vec3(-h, h, h));
//                                 // -Z (NEGATIVE_Z)
//                                 newVertices->push_back(osg::Vec3(-h, -h, -h)); newVertices->push_back(osg::Vec3(-h, h, -h));
//                                 newVertices->push_back(osg::Vec3(h, h, -h));   newVertices->push_back(osg::Vec3(h, -h, -h));
                                
//                                 // 更新顶点数组
//                                 skyGeometry->setVertexArray(newVertices);
//                                 // 更新纹理坐标（与顶点相同）
//                                 skyGeometry->setTexCoordArray(0, newVertices);
//                             }
                            
//                             // 标记顶点数组为脏，需要重新渲染
//                             vertices->dirty();
//                             skyGeometry->dirtyDisplayList();
//                             skyGeometry->dirtyBound();
                            
//                             return; // 找到并更新了天空节点，退出函数
//                         }
//                     }
//                 }
//             }
//         }
//     }
// }

// void Mte3DService::updateGroundAndGridSize(float newSize)
// {
//     if (newSize <= 0.0f) return;

//     // 更新成员变量
//     _groundSize = newSize;
//     _gridSize = newSize;
//     _axisSize = std::max(5.0f, newSize * 0.1f); // 坐标轴大小为地面大小的1/10，但不小于5

//     // 如果场景根节点不存在，直接返回
//     if (!_top3Droot) return;

//     // 创建新的地面、坐标轴和网格节点
//     osg::ref_ptr<osg::Geode> newGround = createGround(_groundSize);
//     osg::ref_ptr<osg::Geode> newAxis = createAixs(_axisSize);
//     osg::ref_ptr<osg::Geode> newGrid = createGrid(_gridSize, _gridSize / 10.0f); // 步长为网格大小的1/10

//     // 为新节点设置名称
//     newGround->setName("ground");
//     newAxis->setName("axis");
//     newGrid->setName("grid");

//     // 移除当前的地面、坐标轴和网格节点，并记录它们在场景中的位置
//     int groundIndex = -1;
//     int axisIndex = -1;
//     int gridIndex = -1;

//     for (int i = _top3Droot->getNumChildren() - 1; i >= 0; --i)
//     {
//         osg::Node* child = _top3Droot->getChild(i);
//         if (!child) continue;

//         if (child->getName() == "ground")
//         {
//             groundIndex = i;
//             _top3Droot->removeChild(i);
//         }
//         else if (child->getName() == "axis")
//         {
//             axisIndex = i;
//             _top3Droot->removeChild(i);
//         }
//         else if (child->getName() == "grid")
//         {
//             gridIndex = i;
//             _top3Droot->removeChild(i);
//         }
//     }

//     // 在原来的位置添加新节点，如果找不到原来的位置，则添加到前面
//     if (groundIndex >= 0)
//         _top3Droot->insertChild(groundIndex, newGround);
//     else
//         _top3Droot->insertChild(0, newGround);

//     if (axisIndex >= 0)
//         _top3Droot->insertChild(axisIndex, newAxis);
//     else
//         _top3Droot->insertChild(1, newAxis);

//     if (gridIndex >= 0)
//         _top3Droot->insertChild(gridIndex, newGrid);
//     else
//         _top3Droot->insertChild(2, newGrid);
    
//     // 更新天空节点大小
//     updateSkyNodeSize(newSize);
// }

#include <QTextStream>

bool Mte3DService::saveSceneToObj(const QString& filePath)
{
    std::cout << "================================================" << std::endl;
    std::cout << "[saveSceneToObj] START - Exporting scene to OBJ" << std::endl;
    std::cout << "[saveSceneToObj] Target file: " << filePath.toStdString() << std::endl;

    int modelCount = platformMatrixMap.size();
    std::cout << "[saveSceneToObj] Number of platforms to export: " << modelCount << std::endl;

    for (auto it = platformMatrixMap.begin(); it != platformMatrixMap.end(); ++it) {
        int platformID = it.key();
        std::cout << "[saveSceneToObj] Platform ID: " << platformID << std::endl;

        if (platformTransMap.contains(platformID)) {
            osg::Vec3d trans = platformTransMap[platformID];
            std::cout << "  Position: (" << trans.x() << ", " << trans.y() << ", " << trans.z() << ")" << std::endl;
        }

        if (platformPostureMap.contains(platformID)) {
            osg::Vec3d posture = platformPostureMap[platformID];
            std::cout << "  Posture: (h=" << posture.x() << ", p=" << posture.y() << ", r=" << posture.z() << ")" << std::endl;
        }

        if (platformScaleMap.contains(platformID)) {
            osg::Vec3d scale = platformScaleMap[platformID];
            std::cout << "  Scale: (" << scale.x() << ", " << scale.y() << ", " << scale.z() << ")" << std::endl;
        }

        if (platformCenterMap.contains(platformID)) {
            osg::Vec3d center = platformCenterMap[platformID];
            std::cout << "  Local Center: (" << center.x() << ", " << center.y() << ", " << center.z() << ")" << std::endl;
        }

        // if (platformPathMap.contains(platformID)) {
        //     std::cout << "  Model file: " << platformPathMap[platformID].toStdString() << std::endl;
        // }
    }

    if (platformMatrixMap.empty())
    {
        std::cout << "[saveSceneToObj] WARNING: No platforms found in scene to export!" << std::endl;
        std::cout << "================================================" << std::endl;
        return false;
    }

    osg::ref_ptr<osg::Group> exportRoot = new osg::Group;

    std::cout << "[saveSceneToObj] Creating export root and adding platforms..." << std::endl;

    for (auto it = platformMatrixMap.begin(); it != platformMatrixMap.end(); ++it) {
        std::cout << "[saveSceneToObj] Adding platform ID " << it.key() << " to export root" << std::endl;
        exportRoot->addChild(it.value());
    }

    std::cout << "[saveSceneToObj] Export root created with " << exportRoot->getNumChildren() << " children" << std::endl;

    osg::ref_ptr<osg::Group> transform = exportRoot;

    std::cout << "[saveSceneToObj] Using exportRoot directly without additional coordinate transformation" << std::endl;

    osg::ref_ptr<osgDB::Options> writeOptions = new osgDB::Options;

    QFileInfo fileInfo(filePath);
    QString exportDir = fileInfo.absolutePath();

    std::cout << "[saveSceneToObj] Export directory: " << exportDir.toStdString() << std::endl;

    writeOptions->setOptionString("WriteMaterials true");
    writeOptions->setOptionString("MaterialLibraryFile " + fileInfo.baseName().toStdString() + ".mtl");
    writeOptions->setOptionString("TexturePath " + exportDir.toStdString());
    writeOptions->setOptionString("RelativeTexturePath true");
    writeOptions->setOptionString("OverrideTexturePath " + exportDir.toStdString());

    std::cout << "[saveSceneToObj] Export options configured" << std::endl;

    std::vector<std::string> texturePaths;
    TextureCollector textureCollector(texturePaths);
    transform->accept(textureCollector);

    std::set<std::string> uniqueTexturePaths(texturePaths.begin(), texturePaths.end());
    int uniqueCount = uniqueTexturePaths.size();
    std::cout << "[saveSceneToObj] Found " << texturePaths.size() << " texture(s), "
              << uniqueCount << " unique texture(s)" << std::endl;

    for (const std::string& texturePath : uniqueTexturePaths)
    {
        QFileInfo textureInfo(QString::fromStdString(texturePath));
        QFile sourceFile(texturePath.c_str());
        QString destPath = exportDir + "/" + textureInfo.fileName();

        std::cout << "[saveSceneToObj] Processing texture: " << texturePath << std::endl;

        if (sourceFile.exists() && !QFile::exists(destPath))
        {
            if (!sourceFile.copy(destPath))
            {
                std::cout << "[saveSceneToObj] ERROR: Failed to copy texture: " << sourceFile.errorString().toStdString() << std::endl;
            }
            else
            {
                std::cout << "[saveSceneToObj] Copied texture to: " << destPath.toStdString() << std::endl;
            }
        }
        else
        {
            if (!sourceFile.exists()) {
                std::cout << "[saveSceneToObj] WARNING: Source texture file does not exist: " << texturePath << std::endl;
            }
            else {
                std::cout << "[saveSceneToObj] Texture already exists at destination: " << destPath.toStdString() << std::endl;
            }
        }
    }

    std::cout << "[saveSceneToObj] Writing OBJ file..." << std::endl;

    bool exportSuccess = osgDB::writeNodeFile(*transform, filePath.toStdString(), writeOptions);

    if (!exportSuccess)
    {
        std::cout << "[saveSceneToObj] ERROR: Failed to write OBJ file!" << std::endl;
        return false;
    }

    std::cout << "[saveSceneToObj] OBJ file written successfully!" << std::endl;

    QString mtlFilePath = fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".mtl";
    QFile mtlFile(mtlFilePath);

    if (mtlFile.exists()) {
        std::cout << "[saveSceneToObj] Processing MTL file: " << mtlFilePath.toStdString() << std::endl;

        if (mtlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&mtlFile);
            QString content = in.readAll();
            mtlFile.close();

            QStringList lines = content.split("\n");
            int textureCount = 0;
            for (int i = 0; i < lines.size(); ++i) {
                QString line = lines[i].trimmed();
                if (line.startsWith("map_Kd", Qt::CaseInsensitive)) {
                    QStringList parts = line.split(" ", Qt::SkipEmptyParts);
                    if (parts.size() >= 2) {
                        QString fullPath = parts[1];
                        QFileInfo textureInfo(fullPath);
                        QString fileName = textureInfo.fileName();
                        lines[i] = "map_Kd " + fileName;
                        textureCount++;
                        std::cout << "[saveSceneToObj] Updated texture path: " << fullPath.toStdString() << " -> " << fileName.toStdString() << std::endl;
                    }
                }
            }

            std::cout << "[saveSceneToObj] Updated " << textureCount << " texture path(s) in MTL file" << std::endl;

            QString modifiedContent = lines.join("\n");

            if (mtlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                QTextStream out(&mtlFile);
                out << modifiedContent;
                mtlFile.close();
                std::cout << "[saveSceneToObj] MTL file updated successfully" << std::endl;
            }
        }
    }
    else
    {
        std::cout << "[saveSceneToObj] WARNING: MTL file not found: " << mtlFilePath.toStdString() << std::endl;
    }

    std::cout << "[saveSceneToObj] END - Export completed successfully!" << std::endl;
    std::cout << "================================================" << std::endl;

    return true;
}

// 保存场景到Obj文件，只保存addPlatform添加的平台节点
// bool Mte3DService::saveSceneToObj(const QString& filePath)
// {
//     if (!_top3Droot) {
//         LOG_DEBUG << "场景根节点不存在，无法保存到Obj文件";
//         return false;
//     }

//     // 创建一个临时根节点，用于保存所有平台的完整变换
//     osg::ref_ptr<osg::Group> tempRoot = new osg::Group;
    
//     // 遍历所有平台节点
//     for (auto it = platformMatrixMap.begin(); it != platformMatrixMap.end(); ++it) {
//         int platformId = it.key();
//         osg::ref_ptr<osg::MatrixTransform> platformNode = it.value();
        
//         if (platformNode) {
//             // 创建一个新的矩阵变换节点，复制原始节点的变换
//             osg::ref_ptr<osg::MatrixTransform> newMt = new osg::MatrixTransform;
//             newMt->setMatrix(platformNode->getMatrix());
            
//             // 复制所有子节点（模型和包围盒）
//             for (unsigned int i = 0; i < platformNode->getNumChildren(); ++i) {
//                 osg::Node* child = platformNode->getChild(i);
//                 if (child) {
//                     // 只复制模型节点，不复制包围盒（包围盒节点名称为"boundingBox"）
//                     if (child->getName() != "boundingBox") {
//                         // 克隆节点，避免节点只能有一个父节点的问题
//                         osg::ref_ptr<osg::Node> clonedChild = osg::clone(child, osg::CopyOp::DEEP_COPY_ALL);
//                         if (clonedChild) {
//                             newMt->addChild(clonedChild);
//                         }
//                     }
//                 }
//             }
            
//             // 将新节点添加到临时根节点
//             tempRoot->addChild(newMt);
//         }
//     }
    
//     // 将临时根节点写入Obj文件
//     std::string strPath = filePath.toLocal8Bit().toStdString();
//     bool result = osgDB::writeNodeFile(*tempRoot, strPath);
    
//     if (result) {
//         LOG_DEBUG << "场景成功保存到Obj文件：" << filePath;
//         LOG_DEBUG << "保存的平台节点数量：" << tempRoot->getNumChildren();
//     } else {
//         LOG_DEBUG << "保存场景到Obj文件失败：" << filePath;
//     }
    
//     return result;
// }

// 保存选中平台到Obj文件
// bool Mte3DService::saveSelectedPlatformToObj(const QString& filePath)
// {
//     int selectedId = getSelectedPlatformId();
//     if (selectedId == -1) {
//         LOG_DEBUG << "没有选中的平台，无法保存到Obj文件";
//         return false;
//     }

//     // 获取选中平台的矩阵变换节点
//     auto it = platformMatrixMap.find(selectedId);
//     if (it == platformMatrixMap.end()) {
//         LOG_DEBUG << "找不到选中平台的矩阵变换节点，无法保存到Obj文件";
//         return false;
//     }

//     osg::ref_ptr<osg::MatrixTransform> platformNode = it.value();
//     if (!platformNode) {
//         LOG_DEBUG << "选中平台的矩阵变换节点无效，无法保存到Obj文件";
//         return false;
//     }

//     // 将选中平台写入Obj文件

//     // 1. 创建导出选项
//       osg::ref_ptr<osgDB::Options> writeOptions = new osgDB::Options;

//       // 2. 配置：禁用材质简化，保留完整属性
//       writeOptions->setOptionString("WriteFullMaterialProperties true");
//       // 配置：尝试使用相对路径（部分OSG版本支持，若不支持则后续用方案1修复）
//       writeOptions->setOptionString("UseRelativePaths true");
//       // 配置：确保写入mtllib指令（关联MTL文件）
//       writeOptions->setOptionString("WriteMTLReference true");

//     std::string strPath = filePath.toLocal8Bit().toStdString();


//     bool result = osgDB::writeNodeFile(*platformNode, strPath, writeOptions);
    
//     if (result) {
//         LOG_DEBUG << "选中平台成功保存到Obj文件：" << filePath;
//     } else {
//         LOG_DEBUG << "保存选中平台到Obj文件失败：" << filePath;
//     }
    
//     return result;
// }

// void Mte3DService::positionPlatform(int platformID)
// {
//     // 获取平台的位置信息
//     osg::Vec3d platformTrans = getPlatformTrans(platformID);
    
//     // 获取osgContext实例
//     osgContext* context = osgContext::getInstance();
//     if (!context) {
//         LOG_DEBUG << "osgContext实例获取失败，无法定位相机";
//         return;
//     }
    
//     // 获取主视图（viewId=0）
//     osgViewer::View* view = context->getView(0);
//     if (!view) {
//         LOG_DEBUG << "无法获取主视图，无法定位相机";
//         return;
//     }
    
//     // 获取相机操纵器
//     osg::ref_ptr<osgGA::CameraManipulator> manipulator = view->getCameraManipulator();
//     if (!manipulator) {
//         LOG_DEBUG << "相机操纵器获取失败，无法定位相机";
//         return;
//     }
    
//     // 将操纵器转换为TrackballManipulator类型
//     osg::ref_ptr<osgGA::TrackballManipulator> trackballManipulator = dynamic_cast<osgGA::TrackballManipulator*>(manipulator.get());
//     if (trackballManipulator) {
//         // 计算相机位置，使相机从平台上方500米处观察平台
//         osg::Vec3d eye(platformTrans.x(), platformTrans.y(), platformTrans.z() + 500.0);
//         osg::Vec3d center(platformTrans.x(), platformTrans.y(), platformTrans.z());
//         osg::Vec3d up(0, 0, 1);
        
//         // 设置相机位置
//         trackballManipulator->setHomePosition(eye, center, up);
//         trackballManipulator->home(0.0); // 立即切换到新位置，无动画
        
//         LOG_DEBUG << "相机已定位到平台 " << platformID << " 位置，高度：500米";
//     } else {
//         LOG_DEBUG << "相机操纵器类型不是TrackballManipulator，无法定位相机";
//     }
// }


