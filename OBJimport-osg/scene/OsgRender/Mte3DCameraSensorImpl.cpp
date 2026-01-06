// Mte3DCameraSensorImpl.cpp
// 相机传感器功能实现

#include "Mte3DService.h"


// 创建相机模型节点
osg::ref_ptr<osg::Node> Mte3DService::createCameraModel()
{
    // 创建一个简单的相机模型：一个锥体代表镜头，一个立方体代表相机主体
    osg::ref_ptr<osg::Group> cameraGroup = new osg::Group;
    
    // 创建矩阵变换用于缩放模型 - 增加尺寸以适应地球场景
    osg::ref_ptr<osg::MatrixTransform> scaleTransform = new osg::MatrixTransform;
    
    // 地球场景中需要更大的尺寸，设置缩放因子为10
    scaleTransform->setMatrix(osg::Matrix::scale(10.0f, 10.0f, 10.0f));
    
    // 创建相机主体（立方体）- 增加基础尺寸
    osg::ref_ptr<osg::Geode> bodyGeode = new osg::Geode;
    osg::ref_ptr<osg::ShapeDrawable> bodyShape = new osg::ShapeDrawable(new osg::Box(osg::Vec3(0, 0, 0), 1.0f, 1.0f, 0.6f));
    bodyShape->setColor(osg::Vec4(0.7f, 0.7f, 0.7f, 1.0f));
    bodyGeode->addDrawable(bodyShape);
    
    // 创建镜头（锥体）- 增加基础尺寸
    osg::ref_ptr<osg::Geode> lensGeode = new osg::Geode;
    osg::ref_ptr<osg::ShapeDrawable> lensShape = new osg::ShapeDrawable(new osg::Cone(osg::Vec3(0, 0, -0.6f), 0.6f, 0.8f));
    lensShape->setColor(osg::Vec4(1.2f, 1.2f, 1.3f, 1.0f));
    lensGeode->addDrawable(lensShape);
    
    // 添加到缩放变换节点
    scaleTransform->addChild(bodyGeode);
    scaleTransform->addChild(lensGeode);
    
    // 添加缩放变换到组节点
    cameraGroup->addChild(scaleTransform);
    
    // 设置状态集
    osg::ref_ptr<osg::StateSet> ss = cameraGroup->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    ss->setMode(GL_LIGHT0, osg::StateAttribute::ON);
    
    // 确保模型在地球场景中可见
    ss->setRenderBinDetails(10, "RenderBin");
    
    return cameraGroup;
}

// 添加相机传感器
void Mte3DService::addCameraSensor(const osg::Vec3d& position, const osg::Vec3d& orientation)
{
    qDebug() << "添加相机传感器";
    qDebug() << "初始位置: " << position.x() << ", " << position.y() << ", " << position.z();
    qDebug() << "初始朝向: " << orientation.x() << ", " << orientation.y() << ", " << orientation.z();
    
    // 先删除已存在的相机传感器（如果有）
    if (cameraSensor.valid()) {
        deleteCameraSensor();
    }
    
    // 创建相机模型
    cameraModel = createCameraModel();
    
    // 创建地理变换节点
    cameraSensor = new osgEarth::GeoTransform();
    
    // 创建矩阵变换节点用于设置朝向
    osg::ref_ptr<osg::MatrixTransform> matrixTransform = new osg::MatrixTransform();
    matrixTransform->addChild(cameraModel);
    
    // 将矩阵变换节点添加到地理变换节点
    cameraSensor->addChild(matrixTransform);
    
    // 获取地图节点
    osg::ref_ptr<osgEarth::MapNode> mapNode = osgContext::getInstance()->getMapNode();
    if (!mapNode) {
        qDebug() << "错误: 无法获取地图节点";
        return;
    }
    
    // 设置初始位置
    osgEarth::GeoPoint geoPos(mapNode->getMapSRS(), position.x(), position.y(), position.z(), osgEarth::ALTMODE_ABSOLUTE);
    cameraSensor->setPosition(geoPos);
    
    // 设置初始朝向
    osg::Quat q = eulerHPRAnglesToQuat(orientation.x(), orientation.y(), orientation.z());
    osg::Matrix rotationMatrix;
    rotationMatrix.makeRotate(q);
    matrixTransform->setMatrix(rotationMatrix);
    
    // 设置状态集
    osg::StateSet* ss = cameraSensor->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    
    // 添加包围盒用于标识
    osg::ref_ptr<osg::Geode> boundingBox = createBoundingBoxGeode(cameraModel);
    boundingBox->setNodeMask(0); // 默认隐藏
    cameraSensor->addChild(boundingBox);
    
    // 将相机传感器添加到场景
    osg::ref_ptr<osg::Group> rootNode = osgContext::getInstance()->getRootNode();
    if (rootNode) {
        rootNode->addChild(cameraSensor);
    }
    
    // 存储相机传感器信息
    cameraPosition = position;
    cameraOrientation = orientation;
    cameraFOV = 60.0; // 默认视场角60度
    cameraSensorSelected = false;
    
    // 设置可见性
    if (cameraSensorVisible) {
        cameraSensor->setNodeMask(0xFFFFFF);
    } else {
        cameraSensor->setNodeMask(0);
    }
}

// 删除相机传感器
void Mte3DService::deleteCameraSensor()
{
    if (!cameraSensor.valid())
        return;
    
    qDebug() << "删除相机传感器";
    
    // 从场景中移除相机传感器
    osg::ref_ptr<osg::Group> rootNode = osgContext::getInstance()->getRootNode();
    if (rootNode) {
        rootNode->removeChild(cameraSensor);
    }
    
    // 如果是当前活动相机，切换到全局视角
    if (currentCameraActive) {
        switchToGlobalView();
    }
    
    // 释放资源
    cameraSensor = nullptr;
    cameraModel = nullptr;
    cameraSensorSelected = false;
}

// 选中相机传感器
void Mte3DService::selectCameraSensor()
{
    // 先取消之前选中的相机
    if (cameraSensorSelected) {
        deselectCameraSensor();
    }

    // 选中新相机
    if (cameraSensor) {
        cameraSensorSelected = true;
        
        // 显示相机包围盒
        if (cameraSensor && cameraSensor->getNumChildren() > 0) {
            osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(cameraSensor->getChild(0));
            if (mt && mt->getNumChildren() > 0) {
                osg::Node* model = mt->getChild(0);
                if (model) {
                    // 创建包围盒
                    osg::BoundingBox bb = model->getBound();
                    osg::ref_ptr<osg::Geode> geode = new osg::Geode();
                    osg::ref_ptr<osg::ShapeDrawable> sd = new osg::ShapeDrawable(new osg::Box(bb.center(), bb.xMax() - bb.xMin(), bb.yMax() - bb.yMin(), bb.zMax() - bb.zMin()));
                    sd->setColor(osg::Vec4(1.0, 0.0, 0.0, 0.3)); // 红色半透明
                    sd->getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);
                    sd->getOrCreateStateSet()->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
                    geode->addDrawable(sd.get());
                    
                    // 添加到场景中
                    cameraSensor->addChild(geode.get());
                    selectedCameraBoundingBox = geode; // 保存包围盒引用
                }
            }
        }
    }
}

// 取消选中相机传感器
void Mte3DService::deselectCameraSensor()
{
    if (cameraSensorSelected && selectedCameraBoundingBox && cameraSensor) {
        // 移除包围盒
        cameraSensor->removeChild(selectedCameraBoundingBox);
        
        // 重置选中状态
        selectedCameraBoundingBox = nullptr;
        cameraSensorSelected = false;
    }
}

// 获取当前是否选中了相机传感器
bool Mte3DService::isCameraSensorSelected()
{
    return cameraSensorSelected;
}

// 通过GeoTransform获取相机传感器ID
int Mte3DService::getCameraSensorIDByGeoTransform(osgEarth::GeoTransform* geoTransform)
{
    // 由于现在只有一个相机传感器，直接比较即可
    if (geoTransform && geoTransform == cameraSensor) {
        return 0; // 返回固定ID 0
    }
    
    return -1; // 未找到对应的传感器ID
}

// 获取相机传感器位置
osg::Vec3d Mte3DService::getCameraPosition()
{
    return cameraPosition;
}

// 获取相机传感器朝向
osg::Vec3d Mte3DService::getCameraOrientation()
{
    return cameraOrientation;
}

// 获取相机传感器视场角
double Mte3DService::getCameraFOV()
{
    return cameraFOV;
}

// 切换相机传感器显示/隐藏
void Mte3DService::toggleCameraSensorVisibility()
{
    if (cameraSensor) {
        cameraSensorVisible = !cameraSensorVisible;
        int mask = cameraSensorVisible ? 0xFFFFFF : 0;
        cameraSensor->setNodeMask(mask);
        qDebug() << "相机传感器显示状态已切换: " << (cameraSensorVisible ? "显示" : "隐藏");
    }
}

// 设置相机传感器可见性
void Mte3DService::setCameraSensorVisibility(bool visible)
{
    cameraSensorVisible = visible;
    
    if (cameraSensor) {
        cameraSensor->setNodeMask(visible ? 0xFFFFFF : 0);
        qDebug() << "相机传感器可见性已设置为: " << (visible ? "显示" : "隐藏");
    }
}

// 更新相机传感器参数
void Mte3DService::updateCameraSensorParams(int sensorId, const osg::Vec3d& position, const osg::Vec3d& orientation, double fov)
{
    if (!cameraSensor)
        return;
    
    qDebug() << "更新相机传感器参数，ID: " << sensorId;
    qDebug() << "新位置: (" << position.x() << ", " << position.y() << ", " << position.z() << ")";
    qDebug() << "新朝向: (" << orientation.x() << ", " << orientation.y() << ", " << orientation.z() << ")";
    qDebug() << "新视场角: " << fov;
    
    // 更新位置
    osg::ref_ptr<osgEarth::GeoTransform> geoTransform = cameraSensor;
    osg::ref_ptr<osgEarth::MapNode> mapNode = osgContext::getInstance()->getMapNode();
    
    if (!mapNode) {
        qDebug() << "错误: 无法获取地图节点";
        return;
    }
    
    // 确保使用正确的坐标系（经纬度高度，绝对高度模式）
    osgEarth::GeoPoint geoPos(mapNode->getMapSRS(), position.x(), position.y(), position.z(), osgEarth::ALTMODE_ABSOLUTE);
    geoTransform->setPosition(geoPos);
    
    // 确保相机模型在地球场景中正确显示
    osg::StateSet* ss = geoTransform->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF); // 确保不会被面剔除
    
    // 确保相机始终可见
    geoTransform->setNodeMask(0xFFFFFF);
    
    // 更新朝向
    if (cameraSensor && cameraSensor->getNumChildren() > 0)
    {
        osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(cameraSensor->getChild(0));
        if (mt)
        {
            osg::Quat q = eulerHPRAnglesToQuat(orientation.x(), orientation.y(), orientation.z());
            // 使用更明确的旋转矩阵设置
            osg::Matrix rotationMatrix;
            rotationMatrix.makeRotate(q);
            mt->setMatrix(rotationMatrix);
            
            // 确保子节点的状态集也正确设置
            mt->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);
        }
    }
    
    // 更新存储的数据
    cameraPosition = position;
    cameraOrientation = orientation;
    cameraFOV = fov;
    
    // 如果是当前活动相机，更新视场角
    if (currentCameraActive)
    {
        setCameraFOV(16.0 / 9.0, 0.1, 100000.0, fov);
    }
}

// 切换到相机传感器视角
void Mte3DService::switchToCameraView()
{
    if (!cameraSensor)
        return;
    
    // 保存当前相机状态
    currentCameraActive = true;
    
    // 获取相机位置和朝向
    osg::Vec3d position = cameraPosition;
    osg::Vec3d orientation = cameraOrientation;
    double fov = cameraFOV;
    
    // 设置相机视场角
    setCameraFOV(16.0 / 9.0, 0.1, 100000.0, fov);
    
    // 计算相机看向的目标点（在相机前方一定距离）
    osg::Matrixd rotMatrix = osg::Matrixd::rotate(
        osg::DegreesToRadians(orientation.y()), osg::Y_AXIS,
        osg::DegreesToRadians(orientation.x()), osg::Z_AXIS,
        osg::DegreesToRadians(orientation.z()), osg::X_AXIS
    );
    
    osg::Vec3d forward = osg::Vec3d(0, 0, -1) * rotMatrix;
    forward.normalize();
    
    // 将经纬度高度转换为世界坐标
    osg::ref_ptr<osgEarth::MapNode> mapNode = osgContext::getInstance()->getMapNode();
    osgEarth::GeoPoint cameraGeoPoint(mapNode->getMapSRS(), position.x(), position.y(), position.z());
    osg::Vec3d cameraWorldPos;
    cameraGeoPoint.toWorld(cameraWorldPos);
    
    // 计算目标点世界坐标
    osg::Vec3d targetWorldPos = cameraWorldPos + forward * 1000.0; // 目标点在相机前方1000米
    
    // 将目标点世界坐标转换回经纬度高度
    osgEarth::GeoPoint targetGeoPoint(mapNode->getMapSRS());
    targetGeoPoint.fromWorld(mapNode->getMapSRS(), targetWorldPos);
    
    // 设置相机视角
    osgEarth::Util::EarthManipulator* manipulator = osgContext::getInstance()->getEarthManipulator();
    if (manipulator)
    {
        osgEarth::Viewpoint vp;
        vp.setFocalPoint(osgEarth::GeoPoint(mapNode->getMapSRS(), targetGeoPoint.x(), targetGeoPoint.y(), targetGeoPoint.z()));
        vp.setHeading(osgEarth::Angle(orientation.x(), osgEarth::Units::DEGREES));
        vp.setPitch(osgEarth::Angle(orientation.y(), osgEarth::Units::DEGREES));
        vp.setRange(osgEarth::Distance(1000.0, osgEarth::Units::METERS));
        
        manipulator->setViewpoint(vp, 0.5); // 0.5秒内平滑过渡
    }
}

// 切换到全局视角
void Mte3DService::switchToGlobalView()
{
    // 重置当前活动相机状态
    currentCameraActive = false;
    
    // 恢复默认视场角
    setCameraFOV(16.0 / 9.0, 0.1, 1000000.0, 45.0);
    
    // 设置全局视角
    osgEarth::Util::EarthManipulator* manipulator = osgContext::getInstance()->getEarthManipulator();
    if (manipulator)
    {
        // 获取当前相机位置，然后拉远视角
        osgEarth::Viewpoint vp = manipulator->getViewpoint();
        osgEarth::Distance range(1.0, osgEarth::Units::KILOMETERS); // 1千米
        vp.setRange(range); // 传入Distance对象，内部自动处理单位转换

        manipulator->setViewpoint(vp, 0.5); // 0.5秒内平滑过渡
    }
}

// 获取当前是否有活动相机
bool Mte3DService::isCameraActive()
{
    return currentCameraActive;
}

// 按增量旋转相机传感器（度），并更新朝向
void Mte3DService::rotateCameraSensorByDelta(double deltaHeadingDeg, double deltaPitchDeg, double deltaRollDeg)
{
    if (!cameraSensor)
        return;

    osg::Vec3d cur = cameraOrientation; // (h,p,r)
    osg::Vec3d next(cur.x() + deltaHeadingDeg, cur.y() + deltaPitchDeg, cur.z() + deltaRollDeg);

    // 限制 pitch 避免翻转
    if (next.y() > 89.0) next.y() = 89.0;
    if (next.y() < -89.0) next.y() = -89.0;

    // 限制 heading 在 0-360 度范围内
    while (next.x() < 0.0) next.x() += 360.0;
    while (next.x() >= 360.0) next.x() -= 360.0;

    cameraOrientation = next;

    // 应用变换
    setCameraSensorMatrix(cameraPosition, next, cameraFOV);
}

// 按增量平移相机传感器（世界坐标增量）
void Mte3DService::translateCameraSensorByDelta(double deltaX, double deltaY, double deltaZ)
{
    // 调试信息
    qDebug() << "平移相机传感器";
    qDebug() << "增量: (" << deltaX << ", " << deltaY << ", " << deltaZ << ")";
    qDebug() << "当前相机是否选中: " << cameraSensorSelected;
    
    // 检查相机是否存在
    if (!cameraSensor) {
        qDebug() << "错误: 相机传感器不存在";
        return;
    }

    // 获取当前经纬高
    osg::Vec3d curLLH = cameraPosition; // (lon, lat, height)
    qDebug() << "平移前位置(经纬度高): (" << curLLH.x() << ", " << curLLH.y() << ", " << curLLH.z() << ")";

    // 1. 将当前经纬高转换为世界坐标（XYZ），用于计算增量后的世界坐标
    osg::Vec3d curXYZ;
    CoordConvert::getInstance().DegreeLLH2XYZ(curLLH, curXYZ);

    // 2. 计算增量后的世界坐标
    osg::Vec3d nextXYZ(
        curXYZ.x() + deltaX,
        curXYZ.y() + deltaY,
        curXYZ.z() + deltaZ
    );

    // 3. 将增量后的世界坐标转回经纬高（得到新的经纬高）
    osg::Vec3d nextLLH;
    CoordConvert::getInstance().XYZ2DegreeLLH(nextXYZ, nextLLH);

    // 4. 限制坐标范围
    // 经度限制在 -180 到 180 度
    while (nextLLH.x() < -180.0) nextLLH.x() += 360.0;
    while (nextLLH.x() > 180.0) nextLLH.x() -= 360.0;
    // 纬度限制在 -85 到 85 度（避免极点问题）
    nextLLH.y() = std::max(nextLLH.y(), -85.0);
    nextLLH.y() = std::min(nextLLH.y(), 85.0);
    // 高度限制在至少 1 米
    nextLLH.z() = std::max(nextLLH.z(), 1.0);

    // 5. 更新位置
    cameraPosition = nextLLH;

    // 6. 应用变换
    qDebug() << "平移后位置(经纬度高): (" << nextLLH.x() << ", " << nextLLH.y() << ", " << nextLLH.z() << ")";
    setCameraSensorMatrix(nextLLH, cameraOrientation, cameraFOV);
    
    // 如果是选中的相机，确保高亮状态更新
    if (cameraSensorSelected) {
        qDebug() << "更新选中相机的高亮状态";
        // 重新选中以确保高亮状态正确
        selectCameraSensor(sensorId);
    }
}

// 设置相机传感器矩阵（更新位置和朝向）
void Mte3DService::setCameraSensorMatrix(osg::Vec3d position, osg::Vec3d orientation, double fov)
{
    if (!cameraSensor)
        return;

    // 设置地理变换节点的位置
    osgEarth::MapNode* mapNode = osgContext::getInstance()->getMapNode();
    if (!mapNode) return;

    // 使用正确的坐标系（经纬度高度，绝对高度模式）
    osgEarth::GeoPoint geoPos(mapNode->getMapSRS(), position.x(), position.y(), position.z(), osgEarth::ALTMODE_ABSOLUTE);

    // 更新 GeoTransform 位置
    osg::ref_ptr<osgEarth::GeoTransform> geoTransform = cameraSensor;
    geoTransform->setPosition(geoPos);

    // 更新局部矩阵（rotation from heading/pitch/roll）
    if (geoTransform->getNumChildren() > 0)
    {
        osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(geoTransform->getChild(0));
        if (mt)
        {
            osg::Quat q = eulerHPRAnglesToQuat(orientation.x(), orientation.y(), orientation.z());
            osg::Matrix rotationMatrix;
            rotationMatrix.makeRotate(q);
            mt->setMatrix(rotationMatrix);
        }
    }

    // 更新状态集确保相机正确显示
    osg::StateSet* ss = geoTransform->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    geoTransform->setNodeMask(0xFFFFFF); // 确保相机始终可见

    // 更新存储的数据
    cameraPosition = position;
    cameraOrientation = orientation;
    cameraFOV = fov;

    // 如果是当前活动相机，更新视场角
    if (currentCameraActive)
    {
        setCameraFOV(16.0 / 9.0, 0.1, 100000.0, fov);
    }
}
