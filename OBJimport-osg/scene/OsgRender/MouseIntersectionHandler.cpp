#include "MouseIntersectionHandler.h"
#include <QString>
#include <QVector3D>
#include "Mte3DService.h"

#include <cmath>
#include "osgQtCompositeViewer.h"

MouseIntersectionHandler::MouseIntersectionHandler(QObject * parent) :
    m_isEnabeld(false),
    m_editMode(false),
    m_draggingPos(false),
    m_draggingRot(false),
    m_lastX(0.0f),
    m_lastY(0.0f),
    m_rotationSensitivity(0.15),
    m_scaleStep(1.01)
{
}

MouseIntersectionHandler::~MouseIntersectionHandler()
{
}

void MouseIntersectionHandler::setEnabled(bool isEnabled)
{
    m_isEnabeld = isEnabled;
}

// 返回拾取到的 platform id（没有返回 -1）
int MouseIntersectionHandler::pickPlatformID(osgViewer::View* view, const osgGA::GUIEventAdapter& ea)
{
    if (!view) return -1;

    // osgUtil::LineSegmentIntersector::Intersections intersections;
    // if (!view->computeIntersections(ea, intersections)) return -1;

    // // 先按 MatrixTransform 查找（直接命中模型）
    // for (auto& hit : intersections)
    // {
    //     const osg::NodePath& nodePath = hit.nodePath;
        
    //     // 遍历节点路径，跳过基础节点和天空节点
    //     for (auto it = nodePath.rbegin(); it != nodePath.rend(); ++it)
    //     {
    //         // 检查是否为基础节点或天空节点，如果是则跳过
    //         std::string nodeName = (*it)->getName();
    //         if (nodeName == "ground" || nodeName == "grid" || nodeName == "axis" || nodeName == "sky")
    //         {
    //             // 跳过基础节点和天空节点
    //             continue;
    //         }
            
    //         // 查找MatrixTransform节点，检查是否为平台节点
    //         osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(*it);
    //         if (mt)
    //         {
    //             int modelID = Mte3DService::getInstance().getPlatformMatrixID(mt);
    //             if (modelID >= 0)
    //             {

    //                 return modelID;

    //             }
    //         }
    //     }
    // }

    return -1;
}

bool MouseIntersectionHandler::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
{
    osgViewer::View* view = dynamic_cast<osgViewer::View*>(&aa);

    switch (ea.getEventType())
    {
    case osgGA::GUIEventAdapter::PUSH:
    {
        if (ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
        {
            // 总是处理鼠标点击，去掉m_isEnabeld检查
            
            int pickedId = pickPlatformID(view, ea);

            if (pickedId >= 0)
            {
                // 选中新平台并进入编辑态
                // Mte3DService::getInstance().selectPlatform(pickedId);
                m_editMode = true;
                m_lastX = ea.getX();
                m_lastY = ea.getY();

                // 初始化位置拖动的世界坐标
                osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector = 
                        new osgUtil::LineSegmentIntersector(osgUtil::Intersector::WINDOW, (float)m_lastX, (float)m_lastY);
                osgUtil::IntersectionVisitor iv(intersector);

                if (view && view->getCamera()) view->getCamera()->accept(iv);
                if (intersector->containsIntersections())
                {
                    m_lastWorldPos = intersector->getIntersections().begin()->getWorldIntersectPoint();
                }

                m_draggingPos = true;
                m_draggingRot = false;

                // 发出当前点击的XYZ坐标
                if (view && view->getCamera())
                {
                    osg::ref_ptr<osgUtil::LineSegmentIntersector> intersectorPos = 
                            new osgUtil::LineSegmentIntersector(osgUtil::Intersector::WINDOW, (float)m_lastX, (float)m_lastY);
                    osgUtil::IntersectionVisitor ivPos(intersectorPos);
                    view->getCamera()->accept(ivPos);

                    if (intersectorPos->containsIntersections())
                    {
                        const auto& result = *(intersectorPos->getIntersections().begin());
                        osg::Vec3d worldPos = result.getWorldIntersectPoint();
                        // 直接发出XYZ坐标，不转换为经纬度
                        emit sig_mousePos(worldPos.x(), worldPos.y(), worldPos.z());
                    }
                }

                return true;
            }
            else
            {
                // 点击空白处：发出XYZ信号，取消选中并退出编辑态
                if (view && view->getCamera())
                {
                    osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector = 
                            new osgUtil::LineSegmentIntersector(osgUtil::Intersector::WINDOW, (float)ea.getX(), (float)ea.getY());
                    osgUtil::IntersectionVisitor iv(intersector);
                    view->getCamera()->accept(iv);
                    if (intersector->containsIntersections())
                    {
                        const auto& result = *(intersector->getIntersections().begin());
                        osg::Vec3d worldPos = result.getWorldIntersectPoint();
                        // 直接发出XYZ坐标，不转换为经纬度
                        emit sig_mousePos(worldPos.x(), worldPos.y(), worldPos.z());
                    }
                }

                // 取消选中并重置状态
                // Mte3DService::getInstance().deselectPlatform();
                m_editMode = false;
                m_draggingPos = false;
                m_draggingRot = false;
                return false;
            }
        }
        // 中键按下进入旋转模式
        else if (ea.getButton() == osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON)
        {
            if (m_editMode)
            {
                m_lastX = ea.getX();
                m_lastY = ea.getY();
                m_draggingRot = true;
                m_draggingPos = false;
                return true;
            }
        }
        // 右键点击取消选中模型状态
        else if (ea.getButton() == osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON)
        {
            // 取消选中并重置状态
            // Mte3DService::getInstance().deselectPlatform();
            m_editMode = false;
            m_draggingPos = false;
            m_draggingRot = false;
            return false;
        }
        break;
    }
    case osgGA::GUIEventAdapter::RELEASE:
    {
        // 左键释放结束位置拖动
        if (ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
        {
            if (m_draggingPos)
            {
                m_draggingPos = false;
                return true;
            }
        }
        // 非左键释放结束姿态调整
        else
        {
            if (m_draggingRot)
            {
                m_draggingRot = false;
                return true;
            }
        }
        break;
    }
    case osgGA::GUIEventAdapter::DRAG:
    {
        if (m_editMode)
        {
            // 左键拖动改变位置（只改变X,Y坐标）
            if (m_draggingPos)
            {
                float curX = ea.getX();
                float curY = ea.getY();

                // 计算当前鼠标对应的世界坐标
                osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector = 
                        new osgUtil::LineSegmentIntersector(osgUtil::Intersector::WINDOW, curX, curY);
                osgUtil::IntersectionVisitor iv(intersector);
                if (view && view->getCamera()) view->getCamera()->accept(iv);

                if (intersector->containsIntersections())
                {
                    osg::Vec3d curWorldPos = intersector->getIntersections().begin()->getWorldIntersectPoint();
                    // 计算世界坐标偏移量
                    osg::Vec3d worldDelta = curWorldPos - m_lastWorldPos;

                    int selId;// = Mte3DService::getInstance().getSelectedPlatformId();
                    if (selId >= 0)
                    {
                        // 获取平台的MatrixTransform节点
                        osg::ref_ptr<osg::MatrixTransform> platformMT;// = Mte3DService::getInstance().getPlatformMatrix(selId);
                        if (platformMT)
                        {
                            // 获取平台当前的矩阵
                            osg::Matrix currentMatrix = platformMT->getMatrix();
                            
                            // 分解矩阵，获取旋转部分
                            osg::Vec3 trans, scale;
                            osg::Quat rot, so; // so = scale orientation
                            currentMatrix.decompose(trans, rot, scale, so);
                            
                            // 计算旋转矩阵的逆矩阵
                            osg::Matrix invRotMatrix = osg::Matrix::rotate(rot.inverse());
                            
                            // 将世界坐标的delta转换为模型局部坐标系的delta
                            osg::Vec3d localDelta = worldDelta * invRotMatrix;
                            

                            std::cout << "[MouseIntersectionHandler] DRAG: m_draggingRot=true, selId=" << selId
                                      << ", dx=" << localDelta.x() << ", dy=" << localDelta.y()  <<  std::endl;

                            // 只改变X,Y坐标，Z坐标保持不变
                            // Mte3DService::getInstance().translatePlatformByDelta(selId, localDelta.x(), localDelta.y(), localDelta.z());
                        }
                        else
                        {

                            std::cout << "[MouseIntersectionHandler] DRAG: m_draggingRot=true, selId=" << selId
                                      << ", worldDeltaX=" << worldDelta.x() << ", worldDeltaY=" << worldDelta.y()  <<  std::endl;


                            // 如果无法获取MatrixTransform，使用原始方法
                            // Mte3DService::getInstance().translatePlatformByDelta(selId, worldDelta.x(), worldDelta.y(), worldDelta.z());
                        }
                    }

                    // 更新上一帧世界坐标
                    m_lastWorldPos = curWorldPos;
                }

                m_lastX = curX;
                m_lastY = curY;
                return true;
            }
            // 中键或滚轮按下拖动改变姿态（只改变姿态，不改变位置）
            else if (m_draggingRot)
            {
                float curX = ea.getX();
                float curY = ea.getY();
                float dx = curX - m_lastX;
                float dy = curY - m_lastY;
                m_lastX = curX;
                m_lastY = curY;

                int selId;// = Mte3DService::getInstance().getSelectedPlatformId();
                if (selId >= 0)
                {
                    double deltaHeading = dx * m_rotationSensitivity;
                    double deltaPitch = -dy * m_rotationSensitivity;
                    double deltaRoll = 0.0;

                        std::cout << "[MouseIntersectionHandler] DRAG: m_draggingRot=true, selId=" << selId 
                                  << ", dx=" << dx << ", dy=" << dy 
                                  << ", deltaHeading=" << deltaHeading << ", deltaPitch=" << deltaPitch << std::endl;
                        
                        // 只改变姿态，不改变位置
                        // Mte3DService::getInstance().rotatePlatformByDelta(selId, deltaHeading, deltaPitch, deltaRoll);
                    }
                    return true;
                }
        }
        break;
    }
    case osgGA::GUIEventAdapter::SCROLL:
    {
        // 总是处理滚轮缩放，去掉m_isEnabeld检查
        if (m_editMode)
        {
            int selId;// = Mte3DService::getInstance().getSelectedPlatformId();
            if (selId >= 0)
            {
                osgGA::GUIEventAdapter::ScrollingMotion sm = ea.getScrollingMotion();
                double factor = 1.0;
                if (sm == osgGA::GUIEventAdapter::SCROLL_UP) factor = m_scaleStep;
                else if (sm == osgGA::GUIEventAdapter::SCROLL_DOWN) factor = 1.0 / m_scaleStep;

                std::cout << "[MouseIntersectionHandler] Scroll event: selId=" << selId << ", factor=" << factor << std::endl;
                // Mte3DService::getInstance().scalePlatformByFactor(selId, factor);
                return true;
            }
        }
        break;
    }
    default:
        break;
    }

    return false;
}

void MouseIntersectionHandler::mousePos(const osgGA::GUIEventAdapter& ea)
{
//     osgUtil::LineSegmentIntersector::Intersections intersections;
//     osg::NodePath np;
//     np.push_back(Mte3DService::getInstance().getMapNode());

//     // 直接使用getView(0)获取视图实例调用computeIntersections
// if (osgContext::getInstance()->getView(0)->computeIntersections(ea.getX(), ea.getY(), np, intersections))
//     {
//         if (intersections.size() >= 1)
//         {
//             osgUtil::LineSegmentIntersector::Intersections::iterator it = intersections.begin();
//             osg::Vec3d point;
//             point = it->getWorldIntersectPoint();

//             osg::Vec3 LLH = Mte3DService::getInstance().getMapNode()->getMapSRS()->getEllipsoid().geocentricToGeodetic(
//                         osg::Vec3(point.x(), point.y(), point.z()));

//             emit sig_mousePos(LLH.x(), LLH.y(), LLH.z());
//         }
//     }

}
