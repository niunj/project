#pragma once


#include <QString>
#include <QVector>
#include <QMap>
#include <QColor>
#include <QJsonObject>
#include <QJsonDocument>
#include <QVector3D>


#include <QDataStream>
#include <QList>


//---------------------------------------------
//  物理常数定义
//---------------------------------------------
constexpr double PI = 3.14159265358979323846;
constexpr double SIGMA = 5.670374419e-8;      // 斯蒂芬-玻尔兹曼常数 [W/m²/K⁴]
constexpr double SOLAR_CONSTANT = 1361.0;     // 太阳常数 [W/m²]

// ===== 物理常数宏定义 =====
constexpr double PARAMETER_C1  = 374180000.0;              // 第一辐射常数，单位：W·μm⁴·sr⁻¹·m⁻²
constexpr double PARAMETER_C2  = 14388.0;                  // 第二辐射常数，单位：μm·K
constexpr double absolute_zero = -273.15;                 // 绝对零度，单位：摄氏度



struct ObjectInfoStru
{
    double  m_x     = 0;                          //经度double
    double  m_y     = 0;                          //纬度double
    double  m_z     = 0;                          //高度double
    double  m_h     = 0;                          //俯仰角 pitch
    double  m_p     = 0;                          //偏航角 yaw
    double  m_r     = 0;                          //翻滚角 roll
    double  m_scale = 1;                          //缩放系数
    double  m_speed;                              //速度
    
    // 序列化函数
    friend QDataStream& operator<<(QDataStream& out, const ObjectInfoStru& obj) {
        out << obj.m_x;
        out << obj.m_y;
        out << obj.m_z;
        out << obj.m_h;
        out << obj.m_p;
        out << obj.m_r;
        out << obj.m_scale;

        return out;
    }
    
    // 反序列化函数
    friend QDataStream& operator>>(QDataStream& in, ObjectInfoStru& obj) {
        in >> obj.m_x;
        in >> obj.m_y;
        in >> obj.m_z;
        in >> obj.m_h;
        in >> obj.m_p;
        in >> obj.m_r;
        in >> obj.m_scale;
        in >> obj.m_speed;
        return in;
    }
};



enum ModelType
{
    OBJECT,
    BACKGROUND,
    GEO
};


struct ModelTrackData {
    double model_Lon    = 0;
    double model_Lat    = 0;
    double model_Alt    = 0;
    double model_Az     = 0;
    double model_El     = 0;
    double model_Roll   = 0;
    double model_Speed  = 200;

    // 序列化函数
    friend QDataStream& operator<<(QDataStream& out, const ModelTrackData& track) {
        // 1. 序列化 QString 和 int（基础类型）

        out <<   track.model_Lon;
        out <<   track.model_Lat;
        out <<   track.model_Alt;
        out <<   track.model_Az;
        out <<   track.model_El;
        out <<   track.model_Roll;
        out <<   track.model_Speed;

        return out;
    }

    // 反序列化函数
    friend QDataStream& operator>>(QDataStream& in, ModelTrackData& track) {
        // 1. 反序列化 QString 和 int
        in >>   track.model_Lon;
        in >>   track.model_Lat;
        in >>   track.model_Alt;
        in >>   track.model_Az;
        in >>   track.model_El;
        in >>   track.model_Roll;
        in >>   track.model_Speed;

        return in;
    }

};


struct MteTrackStru {
    QColor trackColor;              // 轨迹颜色
    double trackWidth;              // 轨迹线宽
    bool isVisible;                 // 是否可见
    QVector<ModelTrackData>         trackData;

    // 序列化函数
    friend QDataStream& operator<<(QDataStream& out, const MteTrackStru& track) {
        // 1. 序列化 QString 和 int（基础类型）
        out << track.trackColor;              // 轨迹颜色
        out << track.trackWidth;              // 轨迹线宽
        out << track.isVisible;                 // 是否可见
        out << track.trackData;


        return out;
    }

    // 反序列化函数
    friend QDataStream& operator>>(QDataStream& in, MteTrackStru& track) {
        // 1. 反序列化 QString 和 int
        in >> track.trackColor;              // 轨迹颜色
        in >> track.trackWidth;              // 轨迹线宽
        in >> track.isVisible;                 // 是否可见
        in >> track.trackData;

        return in;
    }
};

struct MtePlatformStru
{
    int                 m_id;                               //平台标识int
    ModelType           modelType;                          //目标、背景、地形
    QString             m_name;                             //模型名称
    QString             m_path;                             //模型路径----三维模型
    QString             mlsPath;                            //网格和材质文件路径
    QString             textPath;                           //纹理文件路径
    QString             mcmPath;                            //材质文件路径
    QString             meshPath;                           //网格文件路径

    // MaterialInfoStru    m_material;                         //模型材质信息

    ObjectInfoStru      m_attribute;                        //模型属性

    MteTrackStru        m_track;                            //平台轨迹信息
public:
	MtePlatformStru()
	{
		m_id = -1;
		m_name = "";
		m_path = "";
        mlsPath = "";
	}
	MtePlatformStru operator=(const MtePlatformStru& other)
	{
        m_id        = other.m_id;
        modelType   = other.modelType;
        m_name      = other.m_name;
        m_path      = other.m_path;
        mlsPath     = other.mlsPath;
        // m_material  = other.m_material;
		m_attribute = other.m_attribute;

		return *this;
	}
    
    // 序列化函数
    friend QDataStream& operator<<(QDataStream& out, const MtePlatformStru& platform) {
        out << platform.m_id;
        out << static_cast<int>(platform.modelType);
        out << platform.m_name;
        out << platform.m_path;
        out << platform.mlsPath;
        // 序列化MaterialInfoStru
        // out << platform.m_material;

        // 序列化ObjectInfoStru
        out << platform.m_attribute;

        out << platform.m_track;

        return out;
    }
    
    // 反序列化函数
    friend QDataStream& operator>>(QDataStream& in, MtePlatformStru& platform) {
        int modelTypeInt;
        in >> platform.m_id;
        in >> modelTypeInt;
        platform.modelType = static_cast<ModelType>(modelTypeInt);
        in >> platform.m_name;
        in >> platform.m_path;
        in >> platform.mlsPath;

        // 反序列化MaterialInfoStru
        // in >> platform.m_material;

        // 反序列化ObjectInfoStru
        in >> platform.m_attribute;
        in >> platform.m_track;

        return in;
    }

};




struct ViewportSetting
{
    bool useFov = false; //是否使用视场角定义功能
    float fovX;  //横向视场角
    float fovY;  //纵向视场角
    uint pixelNumberX;  //横向像素数
    uint pixelNumberY;  //纵向像素数
    uint targetRayNumber;  //目标区域射线密度;

    uint DivNumber; // 成像区域对角线长度;
    uint LineNumber; // 单个向素射线点数;
    uint SegmentNumber;// 网格段份数;
    QString Noise = "0.00000001";// 有效噪声噪度
public:
    ViewportSetting()
    {
        useFov = false; //是否使用视场角定义功能
        fovX = 20;
        fovY = 20;
        pixelNumberX = 320;  //横向像素数
        pixelNumberY = 256;  //纵向像素数
        targetRayNumber = 1;  //目标区域射线密度;
        DivNumber = 400; // 成像区域对角线长度;
        LineNumber = 20; // 单个向素射线点数;
        SegmentNumber = 1;// 网格段份数;
        Noise = QString::number(1E-8);// 有效噪声噪度
    }
    ViewportSetting operator=(const ViewportSetting& other)
    {
        useFov = other.useFov; //是否使用视场角定义功能
        fovX = other.fovX;  //横向视场角
        fovY = other.fovY;  //纵向视场角
        pixelNumberX = other.pixelNumberX;  //横向像素数
        pixelNumberY = other.pixelNumberY;  //纵向像素数
        targetRayNumber = other.targetRayNumber;  //探测器向目标区域每像素发出的射线数

        DivNumber = other.DivNumber; // 成像区域对角线长度;
        LineNumber = other.LineNumber; // 单个向素射线点数;
        SegmentNumber = other.SegmentNumber;// 网格段份数;
        Noise = other.Noise;// 有效噪声噪度
        return *this;
    }
    
    // 序列化函数
    friend QDataStream& operator<<(QDataStream& out, const ViewportSetting& vs)
    {
        out << vs.useFov;
        out << vs.fovX;
        out << vs.fovY;
        out << vs.pixelNumberX;
        out << vs.pixelNumberY;
        out << vs.targetRayNumber;
        out << vs.DivNumber;
        out << vs.LineNumber;
        out << vs.SegmentNumber;
        out << vs.Noise;
        return out;
    }
    
    // 反序列化函数
    friend QDataStream& operator>>(QDataStream& in, ViewportSetting& vs)
    {
        in >> vs.useFov;
        in >> vs.fovX;
        in >> vs.fovY;
        in >> vs.pixelNumberX;
        in >> vs.pixelNumberY;
        in >> vs.targetRayNumber;
        in >> vs.DivNumber;
        in >> vs.LineNumber;
        in >> vs.SegmentNumber;
        in >> vs.Noise;
        return in;
    }
};




struct SceneInfoStru
{
    QString                     sceneName;
    QString                     scenePath;
    QString                     strDateTime;

    QVector<MtePlatformStru>            terrainVec;          //场景地形
    QVector<MtePlatformStru>            backVec;            //场景背景
    QVector<MtePlatformStru>            modelVec;           //场景目标

    ViewportSetting                     viewportSetting;   //探测器参数;


public:
    SceneInfoStru()
    {
        sceneName   = "";
        scenePath   = "";
        strDateTime = "";

        modelVec.clear();

    }
    SceneInfoStru operator=(const SceneInfoStru& other)
    {
        sceneName      = other.sceneName;
        scenePath      = other.scenePath;
        strDateTime    = other.strDateTime;

        terrainVec     = other.terrainVec;
        backVec        = other.backVec;
        modelVec       = other.modelVec;

        return *this;
    }


    // 序列化函数（简化容器处理）
       friend QDataStream& operator<<(QDataStream& out, const SceneInfoStru& scene) {
           out << scene.sceneName
               << scene.scenePath
               << scene.strDateTime

               << scene.terrainVec // 直接序列化 QVector<MtePlatformStru>（依赖其 operator<<）
               << scene.backVec    // 同上
               << scene.modelVec   // 同上

               << scene.viewportSetting;
           return out;
       }

       // 反序列化函数（简化容器处理）
       friend QDataStream& operator>>(QDataStream& in, SceneInfoStru& scene) {
           in >> scene.sceneName
              >> scene.scenePath
              >> scene.strDateTime
              >> scene.terrainVec // 直接反序列化 QVector<MtePlatformStru>
              >> scene.backVec    // 同上
              >> scene.modelVec   // 同上
              >> scene.viewportSetting;

           return in;
       }

};
