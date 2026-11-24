/// UTC ephemeris shared by Earth lighting, atmosphere and measurement shaders.
#ifndef ROCKY_ENV_EPHEMERIS_BRIDGE_H
#define ROCKY_ENV_EPHEMERIS_BRIDGE_H

#include <rocky_env/EnvApi.h>

namespace rockyenv {

class EphemerisBridge {
public:
    EphemerisBridge() = default;

    /// 设置 UTC 日期时间 (与 SkyNode::setDateTime 同一时刻)。
    /// hoursUtc 允许越界 (负/超 24), 内部归一化到合法公历日期。
    void setDateTime(int year, int month, int day, double hoursUtc);

    /// 归一化后的当前日期时间 (供 SkyModule 回写 rocky::DateTime)。
    int    year()  const { return year_; }
    int    month() const { return month_; }
    int    day()   const { return day_; }
    double hoursUtc() const { return hoursUtc_; }
    double greenwichSiderealRadians() const { return sidereal_; }

    /// 计算太阳方向。
    /// @param enuToEcef  EnvFrame::model (锚点 ENU→ECEF; 列为 East/Up/-North/锚点)。
    /// @param out        输出 ECEF/ENU 方向、高度角、方位角。
    /// @return true 成功。
    bool sunInfo(const vsg::dmat4& enuToEcef, EnvSunInfo& out,float* angularRadius=nullptr) const;

    /// 计算月亮方向与月相 (0=朔, 1=望)。参数语义同 sunInfo。
    bool moonInfo(const vsg::dmat4& enuToEcef, EnvSunInfo& out, float& phase,float* angularRadius=nullptr) const;

    bool valid() const { return valid_; }

private:
    vsg::dvec3 sun_ecef_{},moon_ecef_{};
    bool   valid_ = false;
    int    year_  = 2026;
    int    month_ = 9;
    int    day_   = 10;
    double hoursUtc_ = 4.0;
    double sidereal_ = 0.0;
};

}  // namespace rockyenv

#endif  // ROCKY_ENV_EPHEMERIS_BRIDGE_H
