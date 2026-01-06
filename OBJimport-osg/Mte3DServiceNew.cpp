#include "Mte3DServiceNew.h"
#include <QMessageBox>
#include <QDebug>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osg/Math>
#include <algorithm>
#include <iostream>
#include <set>
#include <osg/Geometry>
#include <osg/LineWidth>
#include <osg/PolygonOffset>
#include <osg/CullFace>
#include <osg/Light>
#include <osg/LightSource>
#include <osgUtil/SmoothingVisitor>
#include <osgUtil/Optimizer>

LocaleGuard::LocaleGuard()
{
    savedLocale = std::locale::global(std::locale::classic());
    std::locale::global(std::locale::classic());
}

LocaleGuard::~LocaleGuard()
{
    std::locale::global(savedLocale);
}


// 手动创建带全景纹理坐标的球体（核心：适配skymap.jpg）
osg::ref_ptr<osg::Geode> createSphereForSkyMap(float radius, int slices = 64, int stacks = 32)
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
osg::Node* createSkyBox(const std::string& skyMapPath)
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
osg::ref_ptr<osg::Geode> geode = createSphereForSkyMap(1000.0f, 64, 32); // 64×32分片，兼顾性能和精度

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

    return skyBoxTransform.release();
}

// 创建地面平面
osg::ref_ptr<osg::Geode> createGround(float size)
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
osg::ref_ptr<osg::Geode> createAixs(float size)
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
osg::ref_ptr<osg::Geode> createGrid(float size, float step)
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

bool collectTexturePaths1(osg::Node* node, std::vector<std::string>& texturePaths)
{
    if (!node)
        return false;

    TextureCollector collector(texturePaths);
    node->accept(collector);
    return true;
}

Mte3DServiceNew::Mte3DServiceNew(QObject* parent)
    : QObject(parent)
    , _sceneRoot(new osg::Group)
    , _modelSpacing(2.0f)
    , _currentOffset(0.0f)
    , _groundSize(50.0f)
    , _gridSize(50.0f)
    , _axisSize(10.0f)
    , _nextPlatformID(0)
{
    addDefaultGeometry();
    prepareSceneRoots();
}

Mte3DServiceNew::~Mte3DServiceNew()
{
    clearScene();
}

void Mte3DServiceNew::addDefaultGeometry()
{
    // 默认不添加几何体，只在prepareSceneRoots中添加场景元素
}

void Mte3DServiceNew::clearScene()
{
    if (_sceneRoot.valid())
    {
        _sceneRoot->removeChildren(0, _sceneRoot->getNumChildren());
    }
    _currentOffset = 0.0f;
}

void Mte3DServiceNew::prepareSceneRoots()
{
    if (!_sceneRoot.valid()) {
        _sceneRoot = new osg::Group();
        std::cout << "[Mte3DServiceNew] Scene root initialized" << std::endl;
    }

    // 检查场景元素是否已经存在，避免重复添加
    bool hasGround = false;
    bool hasAxis = false;
    bool hasGrid = false;
    bool hasSkybox = false;

    for (unsigned int i = 0; i < _sceneRoot->getNumChildren(); ++i) {
        osg::Node* child = _sceneRoot->getChild(i);
        if (child) {
            if (child->getName() == "ground") {
                hasGround = true;
            } else if (child->getName() == "axis") {
                hasAxis = true;
            } else if (child->getName() == "grid") {
                hasGrid = true;
            } else if (child->getName() == "sky") {
                hasSkybox = true;
            }
        }
    }

    // 添加地面
    if (!hasGround) {
        osg::ref_ptr<osg::Geode> ground = createGround(_groundSize);
        ground->setName("ground");
        _sceneRoot->addChild(ground);
    }
    
    // 添加坐标轴
    if (!hasAxis) {
        osg::ref_ptr<osg::Geode> axis = createAixs(_axisSize);
        axis->setName("axis");
        _sceneRoot->addChild(axis);
    }
    
    // 添加网格
    if (!hasGrid) {
        osg::ref_ptr<osg::Geode> grid = createGrid(_gridSize, _gridSize / 10.0f);  // 步长为网格大小的1/10
        grid->setName("grid");
        _sceneRoot->addChild(grid);
    }

    // 1. 先添加天空盒（最先渲染）
    if (!hasSkybox) {
        osg::ref_ptr<osg::Node> skybox = createSkyBox("data/Images/sky/skymap.jpg"); // 单张全景图路径
        if (skybox) {
           _sceneRoot->addChild(skybox);
        } else {
           OSG_WARN << "天空盒创建失败，请检查skymap.jpg路径！" << std::endl;
        }
    }
}

osg::Quat Mte3DServiceNew::eulerHPRAnglesToQuat(double headingDeg, double pitchDeg, double rollDeg)
{
    double h = osg::DegreesToRadians(headingDeg);
    double p = osg::DegreesToRadians(pitchDeg);
    double r = osg::DegreesToRadians(rollDeg);
    osg::Quat q_heading(h, osg::Z_AXIS);
    osg::Quat q_pitch(p, osg::Y_AXIS);
    osg::Quat q_roll(r, osg::X_AXIS);
    return q_heading * q_pitch * q_roll;
}

int Mte3DServiceNew::addPlatform(const QString& modelPath, QVector3D position, QVector3D posture, float scale)
{
    return addPlatform(modelPath, position, posture, scale, false);
}

int Mte3DServiceNew::addPlatform(const QString& modelPath, QVector3D position, QVector3D posture, float scale, bool fast)
{
    int platformID = _nextPlatformID++;

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

        if (fast) {
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

    _sceneRoot->addChild(pat);

    platformMatrixMap[platformID] = pat;
    platformModelMap[platformID] = node;
    platformPathMap[platformID] = modelPath;

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

void Mte3DServiceNew::setPlatformMatrix(int platformID, QVector3D scale, QVector3D posture, QVector3D translation)
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

bool Mte3DServiceNew::saveSceneToObj(const QString& filePath)
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

        if (platformPathMap.contains(platformID)) {
            std::cout << "  Model file: " << platformPathMap[platformID].toStdString() << std::endl;
        }
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

