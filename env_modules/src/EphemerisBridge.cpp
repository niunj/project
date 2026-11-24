#include "EphemerisBridge.h"

#include <algorithm>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>   // vsg::inverse

#include <cmath>

namespace rockyenv {

namespace {

// Howard Hinnant 公历算法: 公历日期 → 自 1970-01-01 起的天数 (支持任意年/月)。
long long civilToDays(int y, int m, int d)
{
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>(153 * (m + (m > 2 ? -3 : 9)) + 2) / 5
                         + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;
}

// 自 1970-01-01 起的天数 → 公历日期。
void daysToCivil(long long z, int& y, int& m, int& d)
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long yy = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
    y = static_cast<int>(yy + (m <= 2));
}

}  // namespace

void EphemerisBridge::setDateTime(int year, int month, int day, double hoursUtc)
{
    // 归一化: hoursUtc 允许越界 (如 25.5 / -2.0), 正确进位到日/月/年。
    // 旧实现不处理月/年边界, 长时仿真倍速推进后日期会非法。
    long long dayShift = static_cast<long long>(std::floor(hoursUtc / 24.0));
    double h = hoursUtc - static_cast<double>(dayShift) * 24.0;

    long long days = civilToDays(year, month, day) + dayShift;
    int ny, nm, nd;
    daysToCivil(days, ny, nm, nd);

    year_ = ny;
    month_ = nm;
    day_ = nd;
    hoursUtc_ = h;
    // Low-precision solar/lunar orbital elements and lunar perturbations adapted
    // from Rocky Ephemeris (Copyright 2023 Pelican Mapping, MIT; third_party/rocky/LICENSE.txt).
    // Distances below are geocentric radii. Topocentric parallax is applied by bodyToENU.
    const double t=days-civilToDays(1999,12,31)+h/24.;
    constexpr double rad=3.14159265358979323846/180.;
    const double ms=(356.0470+.9856002585*t)*rad,ws=(282.9404+4.70935e-5*t)*rad;
    const double ls=ms+ws,tilt=(23.4393-3.563e-7*t)*rad;
    // USNO approximate GMST with UTC as a proxy for UT1. Keep the same
    // Earth rotation for Sun, Moon and the J2000 stellar background.
    const double midnight=days-civilToDays(2000,1,1)-.5;
    const double centuries=(midnight+h/24.)/36525.;
    sidereal_=15.*rad*std::remainder(6.697375+.065709824279*midnight+1.0027379*h+
                                    .0000258*centuries*centuries,24.);
    const double rotationCos=std::cos(sidereal_),rotationSin=std::sin(sidereal_);
    const auto toEarthFixed=[&](double longitude,double latitude,double radius) {
        const double x=radius*cos(longitude)*cos(latitude),y=radius*sin(longitude)*cos(latitude),z=radius*sin(latitude);
        const double equatorialY=y*cos(tilt)-z*sin(tilt);
        return vsg::dvec3(x*rotationCos+equatorialY*rotationSin,
                         -x*rotationSin+equatorialY*rotationCos,y*sin(tilt)+z*cos(tilt));
    };
    const auto orbit=[](double anomaly,double eccentricity,double axis) {
        double e=anomaly;
        for(int i=0;i<5;++i) e-=(e-eccentricity*sin(e)-anomaly)/(1.-eccentricity*cos(e));
        const double x=axis*(cos(e)-eccentricity),y=axis*sqrt(1.-eccentricity*eccentricity)*sin(e);
        return std::pair<double,double>{atan2(y,x),hypot(x,y)};
    };
    const auto sun=orbit(ms,.016709-1.151e-9*t,149597870700.);
    sun_ecef_=toEarthFixed(sun.first+ws,0.,sun.second);
    const double node=(125.1228-.0529538083*t)*rad,wm=(318.0634+.1643573223*t)*rad;
    const double mm=(115.3654+13.0649929509*t)*rad,inc=5.1454*rad;
    const auto moon=orbit(mm,.0549,60.2666);
    const double u=moon.first+wm;
    const double x=cos(node)*cos(u)-sin(node)*sin(u)*cos(inc);
    const double y=sin(node)*cos(u)+cos(node)*sin(u)*cos(inc);
    const double lm=node+wm+mm,D=lm-ls,F=lm-node;
    const double longitude=atan2(y,x)+rad*(-1.274*sin(mm-2*D)+.658*sin(2*D)-.186*sin(ms)
        -.059*sin(2*mm-2*D)-.057*sin(mm-2*D+ms)+.053*sin(mm+2*D)+.046*sin(2*D-ms)
        +.041*sin(mm-ms)-.035*sin(D)-.031*sin(mm+ms)-.015*sin(2*F-2*D)+.011*sin(mm-4*D));
    const double latitude=atan2(sin(u)*sin(inc),hypot(x,y))+rad*(-.173*sin(F-2*D)
        -.055*sin(mm-F-2*D)-.046*sin(mm+F-2*D)+.033*sin(F+2*D)+.017*sin(2*mm+F));
    moon_ecef_=toEarthFixed(longitude,latitude,6378137.*(moon.second-.58*cos(mm-2*D)-.46*cos(2*D)));
    valid_ = true;
}

namespace {

// 天体 ECEF 位置 + 锚点 ENU→ECEF 矩阵 → 单位方向 (ECEF/ENU) + 高度角/方位角。
bool bodyToENU(const vsg::dvec3& bodyECEF, const vsg::dmat4& enuToEcef,
               EnvSunInfo& out,double radius,float* angularRadius)
{
    const vsg::dvec3 anchorECEF(enuToEcef[3][0], enuToEcef[3][1], enuToEcef[3][2]);
    vsg::dvec3 dirECEF = bodyECEF - anchorECEF;
    const double len = std::sqrt(dirECEF.x * dirECEF.x +
                                 dirECEF.y * dirECEF.y +
                                 dirECEF.z * dirECEF.z);
    if (len < 1e-9) return false;
    if(angularRadius)*angularRadius=float(std::asin(std::clamp(radius/len,0.,1.)));
    dirECEF = vsg::dvec3(dirECEF.x / len, dirECEF.y / len, dirECEF.z / len);

    const vsg::dmat4 inv = vsg::inverse(enuToEcef);
    const vsg::dvec4 en4 = inv * vsg::dvec4(dirECEF.x, dirECEF.y, dirECEF.z, 0.0);
    vsg::dvec3 dirENU(en4.x, en4.y, en4.z);
    const double lenu = std::sqrt(dirENU.x * dirENU.x +
                                  dirENU.y * dirENU.y +
                                  dirENU.z * dirENU.z);
    if (lenu < 1e-9) return false;
    dirENU = vsg::dvec3(dirENU.x / lenu, dirENU.y / lenu, dirENU.z / lenu);

    constexpr double RAD2DEG = 180.0 / 3.14159265358979323846;
    const double el = std::asin(std::max(-1.0, std::min(1.0, dirENU.y)));
    const double az = std::atan2(dirENU.x, -dirENU.z);

    out.dirECEF = vsg::vec3(static_cast<float>(dirECEF.x),
                            static_cast<float>(dirECEF.y),
                            static_cast<float>(dirECEF.z));
    out.dirENU = vsg::vec3(static_cast<float>(dirENU.x),
                           static_cast<float>(dirENU.y),
                           static_cast<float>(dirENU.z));
    out.elevationDeg = static_cast<float>(el * RAD2DEG);
    out.azimuthDeg   = static_cast<float>(az * RAD2DEG);
    return true;
}

}  // namespace

bool EphemerisBridge::sunInfo(const vsg::dmat4& enuToEcef, EnvSunInfo& out,float* angularRadius) const
{
    return valid_ && bodyToENU(sun_ecef_,enuToEcef,out,695700000.,angularRadius);
}

bool EphemerisBridge::moonInfo(const vsg::dmat4& enuToEcef, EnvSunInfo& out,
                               float& phase,float* angularRadius) const
{
    if(!valid_ || !bodyToENU(moon_ecef_,enuToEcef,out,1737400.,angularRadius)) return false;
    phase=float(.5*(1.-std::clamp(vsg::dot(vsg::normalize(sun_ecef_),vsg::normalize(moon_ecef_)),-1.,1.)));
    return true;
}

}  // namespace rockyenv
