/**************************************************************
 * 部门:隐身事业部
 * 创建:DingYW
 * 日期:2024-3-2
 * 描述:经纬度标签
**************************************************************/

#pragma once
#include "MyOsgEarth.h"

#include <QObject>
#include "OsgContext.h"

/**************************************************************
 * 部门:隐身事业部
 * 创建:DingYW
 * 日期:2024-3-2
 * 描述:经纬度标签
**************************************************************/

// 鼠标拣选与交互处理（支持选中 + 左键拖动改变位置 + 中键拖动改变姿态 + 滚轮缩放）
class  MouseIntersectionHandler :public QObject, public osgGA::GUIEventHandler
{
    Q_OBJECT
public:
    MouseIntersectionHandler(QObject* parent = nullptr);
    ~MouseIntersectionHandler();
public:
    void setEnabled(bool isEnabled);
protected:
    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)override;
    //鼠标位置（保留）
    void mousePos(const osgGA::GUIEventAdapter& ea);
    //模型拣选，返回 platformID 或 -1
    int pickPlatformID(osgViewer::View* view, const osgGA::GUIEventAdapter& ea);

private:
    bool m_isEnabeld;
    // 编辑/拖拽态变量
    bool m_editMode = false;           // 是否处于编辑态（选中模型后为 true，点击空白处设为 false）
    bool m_draggingPos = false;        // 正在按住左键拖拽以改变位置
    bool m_draggingRot = false;        // 正在按住中键拖拽以改变姿态
    float m_lastX = 0.0f;
    float m_lastY = 0.0f;
    double m_rotationSensitivity = 0.15; // 度/像素，水平控制 heading, 垂直控制 pitch
    double m_scaleStep = 1.01;          // 每格滚轮放大倍数，改小以实现平滑持续缩放

private:
    osg::Vec3d m_lastWorldPos;   // 上一帧鼠标对应的世界坐标点（用于位置拖动）

signals:
    void sig_mousePos(double, double, double);
};
