/**************************************************************
 * 版权:北京摩弈信息科技有限公司
 * 部门:隐身事业部
 * 创建:DingYW
 * 日期:2024-3-1
 * 描述:三维库对外接口
**************************************************************/

#pragma once

#include <QObject>
#include <QByteArray>
#include <QVector3D>
#include <QDateTime>
#include "MyOsgEarth.h"
#include "../Common/MteStructDef.h"


class osgQtViewer;
class MouseIntersectionHandler;

class Mte3DService  : public QObject
{
	Q_OBJECT

private:
	Mte3DService(QObject *parent=nullptr);
public:
	~Mte3DService();
	static Mte3DService& getInstance();

private:
    // 记录地面和网格的大小
    float _groundSize;
    float _gridSize;
    float _axisSize;

////////////////////////////// 地球节点创建相关
public:
    //添加地球并返回视口
    // QWidget* set3dViewer();

    void prepareSceneRoots();

    // 新增：场景根与访问接口（从 osgContext 移动过来）
private:
    osg::ref_ptr<osg::Group>                    _top3Droot;    // 场景顶层 root（原 osgContext）
    // 存储每个平台的原始几何中心
    QMap<int, osg::Vec3d>                      platformCenterMap;

public:

    // 访问器（线程安全：仅返回 ref_ptr）
    osg::Group* getTopRoot() const { return _top3Droot.get(); }
    
    // 保存场景到Obj文件
    bool saveSceneToObj(const QString& filePath);
    
    // 保存选中平台到Obj文件
    // bool saveSelectedPlatformToObj(const QString& filePath);


public:
    //鼠标位置经纬度显示
//    void switchMouseLocation(bool is_on);

    // 选中/取消选中平台（显示/隐藏包围盒）
    // void selectPlatform(int platformID);
    // void deselectPlatform();
    // int  getSelectedPlatformId() const;


public://********************************************传感器 相机相关****************************************************

    // void positionPlatform(int platformID);

public:

	//创建包围盒
    // osg::Geode* createBoundingBoxGeode(osg::Node* node);

    // void updateBoundingBoxForPlatform(int platformID);


    // osg::AnimationPath* createAnimate(osg::Vec3Array* keyPoint, float speed, double scale);
    // void calPosture(osg::Vec3d First, osg::Vec3d Second, double& pitchAngle, double& rollAngle, double& azAngle);
    // double getDistance(const osg::Vec3d& start, const osg::Vec3d& end);

    // void addObjNode(int nodeID, std::string strNode, osg::Vec3d trans_Pos);
    // void addObjNode(int node_ID, osg::ref_ptr<osg::Node>node, osg::Vec3d trans_Pos);
    // void removeObjNode(int nodeID);

    // void updateTransmit(float m_trans);

public:
    
private:

    int                                                       lightID=1;                                  //光照ID
    QMap<int,osg::Group*>                                     lightMap;                                   //存储光源ID

signals:

    void sig_mousePos(double,double,double);
    // 平台删除信号，用于转发osgQtCompositeViewer的删除信号
    void sig_platformRemoved(int platformId);

/////////////////////////////////平台目标相关
public:

       // 成员变量：映射表（存储所有平台数据）
    QMap<int, osg::ref_ptr<osg::Geode>>                       platformBoxMap;
    QMap<int, MtePlatformStru>                                platformMap;
    QMap<int, osg::ref_ptr<osg::Node>>                        platformModelMap;                           //平台模型管理
    QMap<int, osg::ref_ptr<osg::MatrixTransform>>             platformMatrixMap;                          //平台矩阵管理
    QMap<int, osg::Vec3d>                                     platformScaleMap;                           //缩放保存
    QMap<int, osg::Vec3d>                                     platformPostureMap;                         //姿态保存
    QMap<int, osg::Vec3d>                                     platformTransMap;                           //位置保存


    int                                                       m_selectedPlatformId = -1;

signals:
    void sig_platformSelected(int platformID); // 平台被选中/取消选中（-1 表示取消）//鼠标点击经纬高信号

    // 新增：当平台变换（位置/姿态/缩放）被应用时发出，用于属性面板刷新
    void sig_platformTransformChanged(int platformID, double lon, double lat, double alt,
        double headingDeg, double pitchDeg, double rollDeg,
        double scale);

    // 新增：平台列表（添加/删除/批量更新）已改变，UI 可刷新下拉列表
    void sig_platformListChanged();

public:

    // 单平台添加
    int addPlatform(const MtePlatformStru& platform);

    int addPlatform(int id, const QString& modelPath, QVector3D position, QVector3D posture, float scale, bool fast);

    // 单平台添加（带快速加载选项）
    // int addPlatform(const MtePlatformStru& platform, bool fast);

    // 单平台删除
    // int deletePlatform(int platformId);
    void setPlatformMatrix(int platformID, QVector3D scale, QVector3D posture, QVector3D translation);

    //获取所有平台
    // QMap<int, MtePlatformStru> getAllPlatformInfo();

    //获取平台实时位置，该接口为通用接口，获取数据为经纬高数据，单位°
    // QVector3D getCurPlatPos(int platformID);

     // osg::ref_ptr<osg::Node>  getPlatformModel(int PlatformId) const;


     // int getPlatformBoxHeight(int platformId);

     // 按增量旋转平台（度），并同步内部数据与包围盒
     // void rotatePlatformByDelta(int platformID, double deltaHeadingDeg, double deltaPitchDeg, double deltaRollDeg);
     // 按缩放因子缩放平台（相对于当前 scale），并同步内部数据与包围盒
     // void scalePlatformByFactor(int platformID, double factor);

     // void translatePlatformByDelta(int platformID, double deltaX, double deltaY, double deltaZ);

     // 获取平台当前经纬高（lat, lon, alt）
     // osg::Vec3d getPlatformTrans(int platformID) const;

     //平台变换
     // void setPlatformMatrix(int platformID, osg::Vec3d mscale, osg::Vec3d mposture, osg::Vec3d mtrans);

     //获取平台缩放信息
     // QMap<int, osg::Vec3d>      getPlatformScaleMap();

     //获取平台姿态信息
     // QMap<int, osg::Vec3d>      getPlatformPostureMap();

     //获取平台位置信息
     // QMap<int, osg::Vec3d>      getPlatformTransMap();

     //获取成像盒
     // QMap<int, osg::ref_ptr<osg::Geode>> getModelBox();

     // //隐藏平台
     // void setPlatformHidden(int platformID,bool is_hidden);
     // //更新平台位置
     // void updatePlatformPos(int platformID, const QVector3D& platformPos);
     // //更新平台姿态
     // void updatePlatformPosture(int platformID,const QVector3D& platPostureDeg);
     // //更新平台位置和姿态
     // void updatePlatformPosPosture(int platformID, const QVector3D& platformPos, const QVector3D& platformPosture);
     // //设置平台缩放
     // void setPlatformScale(int platformID,double m_scale);
     // //获取平台位置信息数据为经纬度 角度信息
     // QVector3D getPlatformPos(int platformID);
     // //获取平台姿态角信息
     // QVector3D getPlatformPosture(int platformID);
     // //获取平台缩放倍数
     // QVector3D getPlatformScale(int platformID);
     // //获取平台矩阵
     // osg::ref_ptr<osg::MatrixTransform>getPlatformMatrix(int platformID);
     // //获取模型ID
     // int getPlatformMatrixID(osg::ref_ptr<osg::MatrixTransform> m_matrix);
     // //获取平台信息
     // MtePlatformStru getPlatformInfo(int platformID);


public:

    // 分组管理相关方法
    // 根据ModelType和m_path创建分组名称
    // QString createGroupName(ModelType modelType, const QString& path);
    // 根据组名获取分组节点
    // osg::ref_ptr<osg::Group> getOrCreateGroupNode(const QString& groupName);
    // 按分组名称删除节点（优化版）
    // void deletePlatformsByGroup(const QString& groupName);
    // 按模型类型清空所有该类型的分组和节点
    // void deletePlatformsByModelType(ModelType modelType);
    // 获取所有分组名称
    // QStringList getAllGroupNames();
    // 获取指定分组下的所有平台ID
    // QVector<int> getPlatformIdsByGroup(const QString& groupName);
    // 获取指定模型类型下的所有分组名称
    // QStringList getGroupNamesByModelType(ModelType modelType);
    // 获取平台所属的分组名称
    // QString getPlatformGroupName(int platformID);

    // 更新天空节点大小
    // void updateSkyNodeSize(float newSize);
    // 更新地面、坐标轴和网格的大小
    // void updateGroundAndGridSize(float newSize);
    // 计算场景中所有模型的最大包围盒大小
    // float calculateMaxModelSize();
    
    
private:
    // 存储分组节点的映射表，键为分组名称，值为分组节点
    // QMap<QString, osg::ref_ptr<osg::Group>> groupNodeMap;

    // 存储平台ID到分组名称的映射表
    // QMap<int, QString>                      platformToGroupMap;
};
