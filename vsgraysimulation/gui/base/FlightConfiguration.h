#pragma once
#include <QString>
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>

namespace vsgraysim::gui {
struct FlightParameter {
    const char* key;
    const char* label;
    const char* unit;
    double initial, minimum, maximum;
};
inline const std::array<FlightParameter, 9>& flightParameters() {
    static const std::array<FlightParameter, 9> values{{
        {"min_speed", "最小速度", "m/s", 50, .1, 2000},
        {"cruise_speed", "巡航速度", "m/s", 200, .1, 2000},
        {"max_speed", "最大速度", "m/s", 300, .1, 2000},
        {"min_altitude", "最低局部高度", "m", 10, -100000, 1000000},
        {"max_altitude", "最高局部高度", "m", 15000, -100000, 1000000},
        {"max_roll_angle", "最大滚转角", "°", 60, .1, 89},
        {"max_pitch_rate", "最大俯仰率", "°/s", 20, .1, 360},
        {"max_roll_rate", "最大滚转率", "°/s", 60, .1, 360},
        {"max_yaw_rate", "最大偏航率", "°/s", 30, .1, 360},
    }};
    return values;
}
inline QString validateFlightConfiguration(const nlohmann::json& configuration) {
    if (!configuration.is_object()) return QStringLiteral("飞行参数必须为对象");
    if (configuration.contains("flight_control_enabled") && !configuration["flight_control_enabled"].is_boolean())
        return QStringLiteral("显控接管许可必须为布尔值");
    auto value = [&](const char* key, double initial) { return configuration.value(key, initial); };
    for (const auto& parameter : flightParameters()) {
        if (!configuration.contains(parameter.key)) continue;
        const auto& item = configuration[parameter.key];
        if (!item.is_number() || !std::isfinite(item.get<double>()) ||
            item.get<double>() < parameter.minimum || item.get<double>() > parameter.maximum)
            return QStringLiteral("%1必须在 [%2, %3] %4 范围内")
                .arg(QString::fromUtf8(parameter.label)).arg(parameter.minimum).arg(parameter.maximum)
                .arg(QString::fromUtf8(parameter.unit));
    }
    if (value("min_speed", 50) > value("cruise_speed", 200) ||
        value("cruise_speed", 200) > value("max_speed", 300))
        return QStringLiteral("速度必须满足：最小速度 ≤ 巡航速度 ≤ 最大速度");
    if (value("min_altitude", 10) >= value("max_altitude", 15000))
        return QStringLiteral("最低局部高度必须小于最高局部高度");
    return {};
}
} // namespace vsgraysim::gui
