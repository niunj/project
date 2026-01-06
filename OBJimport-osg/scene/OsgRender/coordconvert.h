#ifndef COORDCONVERT_H
#define COORDCONVERT_H

#include "MyOsgEarth.h"
#include <QVector3D>



class CoordConvert
{
private:
    CoordConvert();
    // 禁止拷贝构造函数
    CoordConvert(const CoordConvert&) = delete;
    // 禁止赋值运算符
    CoordConvert& operator=(const CoordConvert&) = delete;
public:
    // 静态成员函数，用于获取单例实例
    static CoordConvert& getInstance() {
        // 静态成员变量，保存单例实例
        static CoordConvert instance;
        return instance;
    }
public:
    /**
    *@note: 转换本地经纬坐标为世界坐标系，本地坐标采用弧度表示
    */
    bool RadianLLH2XYZ(const osg::Vec3d& vecLLH, osg::Vec3d& vecXYZ);

    /**
    *@note: 转换本地经纬坐标为世界坐标系，本地坐标采用角度表示
    */
    bool DegreeLLH2XYZ(const osg::Vec3d& vecLLH, osg::Vec3d& vecXYZ);

    /**
    *@note: 转换世界坐标系为本地经纬坐标，本地坐标采用弧度表示
    */
    bool XYZ2RadianLLH(const osg::Vec3d& vecXYZ, osg::Vec3d& vecLLH);

    /**
    *@note: 转换世界坐标系为本地经纬坐标，本地坐标采用角度表示
    */
    bool XYZ2DegreeLLH(const osg::Vec3d& vecXYZ, osg::Vec3d& vecLLH);

    /**
    *@note: 转换屏幕坐标为本地经纬坐标，本地坐标采用角度表示
    */
    bool ScreenXY2DegreeLLH(float fX, float fY,osg::Vec3d&vecXYZ, osg::Vec3d& vecLLH);
    bool ScreenXY2DegreeLLH(float fX, float fY, double& dLon, double& dLat, double& dHei);

    /**
    *@note: 转换屏幕坐标系为本地经纬坐标，本地坐标采用弧度表示
    */
    bool ScreenXY2RadiaLLH(float fX, float fY,osg::Vec3d&vecXYZ, osg::Vec3d& vecLLH);
    bool ScreenXY2RadiaLLH(float fX, float fY, double& dLon, double& dLat, double& dHei);
    /**
    *@note: 转换屏幕坐标系为世界坐标
    */
    bool ScreenXY2XYZ(float fX, float fY, osg::Vec3d& vecXYZ);
    bool ScreenXY2XYZ(float fX, float fY, double& dX, double& dY, double& dZ);
    /**
    *@note: 世界坐标转屏幕坐标
    */
    bool XYZ2Screen(const osg::Vec3d vecXYZ, osg::Vec3d& vecScreen);
    /**
    *@note: 转换本地经纬坐标为Matrix，本地坐标采用弧度表示
    */
    bool RadiaLLH2Matrix(const osg::Vec3d& vecLLH, osg::Matrix& matrix);

    /**
    *@note: 转换本地经纬坐标为Matrix，本地坐标采用角度表示
    */
    bool DegreeLLH2Matrix(const osg::Vec3d& vecLLH, osg::Matrix& matrix);

    /**
    *@note: 转换XYZ坐标为Matrix，本地坐标采用角度表示
    */
    bool XYZ2Matrix(const osg::Vec3d& vecXYZ, osg::Matrix& matrix);
    /**
    *@note: 局部世界坐标转成屏幕坐标
    */
    bool ConvertLocalWorldCoordToScreen(const osg::Vec3d& pos, osg::Vec2d& screenPos);
    /**
    *@note: 获得GeoDistance距离，获得地球坐标下的两点距离,输入的经纬度采用弧度
    */
    double GetGeoDistance(double dSLon, double dSLat, double dELon, double dELat);

    //计算两点之间距离、方位、俯仰
    void calTwoPointDisPosure(QVector3D srcLLH_Deg, QVector3D dstLLH_Deg, double& dis, double& Az, double& El, double& Roll);
    //计算两点俯仰角
    double calElDeg(osg::Vec3d startDeg, osg::Vec3d endDeg);

public:
    //墨卡托范围[-PI, PI]->大地纬度范围[-PI/2, PI/2]
    static double mercatorAngleToGeodeticLatitude(double mercatorAngle);
    //大地纬度范围[-PI/2, PI/2]->墨卡托范围[-PI, PI]
    static double geodeticLatitudeToMercatorAngle(double latitude);

    //经纬高转墨卡托
    void LLH2Mercator(double &x, double &y, double &z);
    //经纬高转墨卡托
    osg::Vec3d LLH2Mercator(osg::Vec3d pos);
    //墨卡托转经纬高
    void Mercator2LLH(double &x, double &y, double &z);
private:
    osg::ref_ptr<osg::EllipsoidModel>em;
    //Web墨卡托投影所支持的最大纬度（北和南）
    static double maximumLatitude;
};

#endif // COORDCONVERT_H
