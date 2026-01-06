#include "coordconvert.h"
#include "Mte3DService.h"

const double epsilon = 0.000000000000001;
const double pi = 3.14159265358979323846;
const double d2r = pi / 180;
const double r2d = 180 / pi;

const double a = 6378137.0;		//椭球长半轴
const double f_inverse = 298.257223563;			//扁率倒数
const double b = a - a / f_inverse;
const double e = sqrt(a * a - b * b) / a;


//墨卡托范围[-PI, PI]->大地纬度范围[-PI/2, PI/2]
double CoordConvert::mercatorAngleToGeodeticLatitude(double mercatorAngle)
{
    return pi / 2.0 - (2.0 * atan(exp(-mercatorAngle)));
}

//Web墨卡托投影所支持的最大纬度（北和南）
double CoordConvert::maximumLatitude = CoordConvert::mercatorAngleToGeodeticLatitude(pi/2);

//大地纬度范围[-PI/2, PI/2]->墨卡托范围[-PI, PI]
double CoordConvert::geodeticLatitudeToMercatorAngle(double latitude)
{
    // Clamp the latitude coordinate to the valid Mercator bounds.
    if (latitude > maximumLatitude)
    {
        latitude = maximumLatitude;
    }
    else if (latitude < -maximumLatitude)
    {
        latitude = -maximumLatitude;
    }
    double sinLatitude = sin(latitude);
    return 0.5 * log((1.0 + sinLatitude) / (1.0 - sinLatitude));
}

CoordConvert::CoordConvert()
{
   em = new osg::EllipsoidModel;
}


bool CoordConvert::RadianLLH2XYZ(const osg::Vec3d& vecLLH, osg::Vec3d& vecXYZ)
{
    if (em)
    {
        em->convertLatLongHeightToXYZ(vecLLH.y(), vecLLH.x(), vecLLH.z(), vecXYZ.x(), vecXYZ.y(), vecXYZ.z());
        return true;
    }

    return false;
}

bool CoordConvert::DegreeLLH2XYZ(const osg::Vec3d& vecLLH, osg::Vec3d& vecXYZ)
{
    if (em)
    {
        // osgEarth::MapNode* mapNode = Mte3DService::getInstance().getMapNode();
        // osgEarth::GeoPoint geoPoint(mapNode->getMapSRS(), vecLLH.x(), vecLLH.y(), vecLLH.z(), osgEarth::ALTMODE_ABSOLUTE);
        // osg::Vec3d worldPoint;
        // geoPoint.toWorld(worldPoint);
        // vecXYZ=worldPoint;

        return true;
    }

    return false;
}

bool CoordConvert::XYZ2RadianLLH(const osg::Vec3d& vecXYZ, osg::Vec3d& vecLLH)
{
    if (em)
    {
        em->convertXYZToLatLongHeight(vecXYZ.x(), vecXYZ.y(), vecXYZ.z(), vecLLH.y(), vecLLH.x(), vecLLH.z());
        return true;
    }

    return false;
}

bool CoordConvert::XYZ2DegreeLLH(const osg::Vec3d& vecXYZ, osg::Vec3d& vecLLH)
{
    if (em)
    {
        em->convertXYZToLatLongHeight(vecXYZ.x(), vecXYZ.y(), vecXYZ.z(), vecLLH.y(), vecLLH.x(), vecLLH.z());
        vecLLH.x() = osg::RadiansToDegrees(vecLLH.x());
        vecLLH.y() = osg::RadiansToDegrees(vecLLH.y());
        return true;
    }

    return false;
}

bool CoordConvert::ScreenXY2DegreeLLH(float fX, float fY, osg::Vec3d& vecXYZ, osg::Vec3d& vecLLH)
{
    if (ScreenXY2RadiaLLH(fX, fY, vecXYZ, vecLLH))
    {
        return true;
    }

    return false;
}

bool CoordConvert::ScreenXY2DegreeLLH(float fX, float fY, double& dLon, double& dLat, double& dHei)
{
    if (ScreenXY2RadiaLLH(fX, fY, dLon, dLat, dHei))
    {
        dLon = osg::RadiansToDegrees(dLon);
        dLat = osg::RadiansToDegrees(dLat);
        return true;
    }

    return false;
}

bool CoordConvert::ScreenXY2RadiaLLH(float fX, float fY, osg::Vec3d& vecXYZ, osg::Vec3d& vecLLH)
{
    // if (Mte3DService::getInstance().getMapNode())
    // {
    //     osg::Vec3d vecWorld;
    //     if (Mte3DService::getInstance().getMapNode()->getTerrain()->getWorldCoordsUnderMouse(
    //                 osgContext::getInstance()->getView(), fX, fY, vecWorld))
    //     {
    //         vecXYZ = vecWorld;
    //         vecLLH = Mte3DService::getInstance().getMapNode()->getMapSRS()->getEllipsoid().geocentricToGeodetic(
    //             osg::Vec3d(vecWorld.x(), vecWorld.y(), vecWorld.z()));

    //         return true;
    //     }
    // }

    return false;
}

bool CoordConvert::ScreenXY2RadiaLLH(float fX, float fY, double& dLon, double& dLat, double& dHei)
{
    // if (Mte3DService::getInstance().getMapNode())
    // {
    //     osg::Vec3d vecWorld;
    //     if (Mte3DService::getInstance().getMapNode()->getTerrain()->getWorldCoordsUnderMouse(
    //                 osgContext::getInstance()->getView(), fX, fY, vecWorld))
    //     {
    //         Mte3DService::getInstance().getMapNode()->getMapSRS()->getEllipsoid().geocentricToGeodetic(
    //             osg::Vec3d(vecWorld.x(), vecWorld.y(), vecWorld.z()));
    //         return true;
    //     }
    // }

    return false;
}

bool CoordConvert::ScreenXY2XYZ(float fX, float fY, osg::Vec3d& vecXYZ)
{
    osg::Vec3d vecLLH;
    if (ScreenXY2DegreeLLH(fX, fY, vecXYZ,vecLLH))
    {
        //DegreeLLH2XYZ(vecLLH, vecXYZ);
        return true;
    }
    return false;
}

bool CoordConvert::ScreenXY2XYZ(float fX, float fY, double& dX, double& dY, double& dZ)
{
    osg::Vec3d vecXYZ;
    if (ScreenXY2XYZ(fX, fY, vecXYZ))
    {
        dX = vecXYZ.x();
        dY = vecXYZ.y();
        dZ = vecXYZ.z();

        return true;
    }
    return false;
}

bool CoordConvert::XYZ2Screen(const osg::Vec3d vecXYZ, osg::Vec3d& vecScreen)
{
    // if (Mte3DService::getInstance().getMapNode())
    // {
    //     osg::Camera* camera = osgContext::getInstance()->getCamera();
    //     osg::Matrix VPW     = camera->getViewMatrix() * camera->getProjectionMatrix() * camera->getViewport()->computeWindowMatrix();
    //     vecScreen = vecXYZ * VPW;

    //     return true;
    // }
    return false;
}

bool CoordConvert::RadiaLLH2Matrix(const osg::Vec3d& vecLLH, osg::Matrix& matrix)
{
    if (em)
    {
        em->computeLocalToWorldTransformFromLatLongHeight(vecLLH.y(), vecLLH.x(), vecLLH.z(), matrix);

        return true;
    }

    return false;
}

bool CoordConvert::DegreeLLH2Matrix(const osg::Vec3d& vecLLH, osg::Matrix& matrix)
{
    return RadiaLLH2Matrix(osg::Vec3d(osg::DegreesToRadians(vecLLH.x()), osg::DegreesToRadians(vecLLH.y()), vecLLH.z()), matrix);
}

bool CoordConvert::XYZ2Matrix(const osg::Vec3d& vecXYZ, osg::Matrix& matrix)
{
    osg::Vec3d vecLLHRadia;
    if (XYZ2RadianLLH(vecXYZ, vecLLHRadia))
    {
        return RadiaLLH2Matrix(vecLLHRadia, matrix);
    }

    return false;
}

bool CoordConvert::ConvertLocalWorldCoordToScreen(const osg::Vec3d& pos, osg::Vec2d& screenPos)
{
    // if (Mte3DService::getInstance().getMapNode() && osgContext::getInstance()->get3DViewer())
    // {
    //     osg::Camera* cam = osgContext::getInstance()->getCamera();
    //     osg::MatrixList worldMatrixList ;//= cam->getWorldMatrices(Mte3DService::getInstance().getMapNode());
    //     osg::Matrix worldMatrix = worldMatrixList.at(0);
    //     osg::Matrix viewMatrix = cam->getViewMatrix();
    //     osg::Matrix projMatrix = cam->getProjectionMatrix();

    //     osg::Vec4d in = osg::Vec4d(pos.x(), pos.y(), pos.z(), 1.0);
    //     osg::Vec4d out = in * worldMatrix;
    //     out = out * viewMatrix;
    //     out = out * projMatrix;

    //     if (out.w() <= 0.0) return false;  //如果out.w()小于0说明在背面不被拣选

    //     out.x() /= out.w();
    //     out.y() /= out.w();
    //     out.z() /= out.w();

    //     out.x() = out.x() * 0.5 + 0.5;
    //     out.y() = out.y() * 0.5 + 0.5;
    //     out.z() = out.z() * 0.5 + 0.5;

    //     if (!osgContext::getInstance()->getCamera()->getViewport())
    //     {
    //         return false;
    //     }
    //     int nScreenW = osgContext::getInstance()->getCamera()->getViewport()->width();
    //     int nScreenH = osgContext::getInstance()->getCamera()->getViewport()->height();

    //     screenPos.x() = out.x() * nScreenW;
    //     screenPos.y() = out.y() * nScreenH;

    //     return true;
    // }
    return false;
}


double CoordConvert::GetGeoDistance(double dSLon, double dSLat, double dELon, double dELat)
{
    // if (em)
    // {
    //     return osgEarth::GeoMath::distance(dSLat, dSLon, dELat, dELon, em->getRadiusEquator());
    // }

    return  0.0;
}

void CoordConvert::calTwoPointDisPosure(QVector3D srcLLH_Deg, QVector3D dstLLH_Deg, double& dis, double& Az, double& El, double& Roll)
{
    // osg::Vec3d srcPos(osg::Vec3d(srcLLH_Deg.x(), srcLLH_Deg.y(), srcLLH_Deg.z()));
    // osg::Vec3d dstPos(osg::Vec3d(dstLLH_Deg.x(), dstLLH_Deg.y(), dstLLH_Deg.z()));
    // osg::Vec3d srcXYZ, dstXYZ;
    // DegreeLLH2XYZ(srcPos, srcXYZ);
    // DegreeLLH2XYZ(dstPos, dstXYZ);
    // dis = (dstXYZ - srcXYZ).length();
    // double maz = osgEarth::GeoMath::bearing(osg::DegreesToRadians(srcLLH_Deg.y()), osg::DegreesToRadians(srcLLH_Deg.x()), osg::DegreesToRadians(dstLLH_Deg.y()), osg::DegreesToRadians(dstLLH_Deg.x()));
    // Az = osg::RadiansToDegrees(maz);

    // El = calElDeg(srcPos, dstPos);
}

double CoordConvert::calElDeg(osg::Vec3d startDeg, osg::Vec3d endDeg)
{
    double mDeg = 0;
    osg::Vec3d srcXYZ, dstXYZ;
    DegreeLLH2XYZ(startDeg, srcXYZ);
    DegreeLLH2XYZ(endDeg, dstXYZ);

    double earthR = 6371000;
    double L = (dstXYZ - srcXYZ).length();
    double L1 = earthR + startDeg.z();
    double L2 = earthR + endDeg.z();
    double P = (L + L1 + L2) / 2;
    double S = sqrt(P * (P - L) * (P - L1) * (P - L2));
    double theta = asin(2 * S / (L * L1));
    double elThea = 90 - osg::RadiansToDegrees(theta);

    // bool mf = osgEarth::GeoMath::isPointVisible(srcXYZ, dstXYZ);
    // if (mf)
    // {
    //     if (endDeg.z() > startDeg.z())
    //     {
    //         mDeg = elThea;
    //     }
    //     else
    //     {
    //         mDeg = -elThea;
    //     }
    // }
    // else
    // {
    //     mDeg = -elThea;
    // }

    return mDeg;
}

//经纬高转墨卡托
void CoordConvert::LLH2Mercator(double &x, double &y, double &z)
{
    x = x * d2r * a;
    y = geodeticLatitudeToMercatorAngle(y * d2r) * a;
}

//经纬高转墨卡托
osg::Vec3d CoordConvert::LLH2Mercator(osg::Vec3d pos)
{
    osg::Vec3d mct;
    mct.x() = pos.x() * d2r * a;
    mct.y() = geodeticLatitudeToMercatorAngle(pos.y() * d2r) * a;
    mct.z() = pos.z();
    return mct;
}

//墨卡托转经纬高
void CoordConvert::Mercator2LLH(double &x, double &y, double &z)
{
    x = x / a * r2d;
    y = mercatorAngleToGeodeticLatitude(y / a) * r2d;
}
