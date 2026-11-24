///
/// GimbalModel.cpp —— 扫描机构纯函数组件实现 (设计文档 §7.2, B7)。
///
/// 8 模式机体系偏转时间函数 (t = 相对扫描启动时刻, s):
///   fixed    常值 (az_center, el_center) —— 退化为现状 aperture.direction;
///   sector   方位三角波往复: [center−extent/2, center+extent/2], 速率 az_rate;
///   circular 方位匀速周扫: rate 为 rpm (°/s = rpm×6);
///   raster   方位三角波 (同 sector) + 俯仰步进 rows 行铺满 el_extent;
///   conical  视线绕中心轴锥转 (小角近似): az/el 正交正弦, 锥角 conical
///            由 az_extent 承载 (°), 锥频 = az_rate (Hz);
///   rosette  玫瑰曲线 r=cos(kθ): 双频正交, 玫瑰频率 = az_rate (Hz), k=3;
///   spiral   阿基米德螺线: 半径 el_extent/2→az_extent/2 锯齿 (周期 T=1/f_in),
///            角速率 az_rate (°/s)。
/// slaved 不走时间函数 (状态机 TRACK 态 + slew 限幅)。
#include "rocky_env/GimbalModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rockyenv {
namespace sensor {

namespace {
constexpr double kPi = 3.14159265358979323846;

double wrap180(double deg) {
    double x = std::fmod(deg + 180.0, 360.0);
    if (x < 0.0) x += 360.0;
    return x - 180.0;
}

/// 三角波往复: [c−e/2, c+e/2], 周期 2e/|rate| (e=全幅, °; rate °/s)。
double triangle(double t, double c, double e, double rate) {
    if (e <= 0.0 || rate == 0.0) return c;
    const double span = 2.0 * e;                 // 去程+回程
    double x = std::fmod(std::abs(rate) * t, span);
    if (x < 0.0) x += span;
    const double off = (x <= e) ? x : span - x;  // [0, e]
    return c - e / 2.0 + off;
}
} // namespace

void GimbalModel::configure(const ScanSpec& scan, double beam_width_rad) {
    scan_ = scan;
    beamWidth_ = std::max(beam_width_rad, 1e-4);
    const std::string& m = scan_.mode;
    if (m != "fixed" && m != "raster" && m != "sector" && m != "circular" &&
        m != "rosette" && m != "conical" && m != "spiral" && m != "slaved") {
        std::fprintf(stderr, "[GimbalModel] 未知扫描模式 '%s' → fixed\n", m.c_str());
        scan_.mode = "fixed";
    }
    state_ = State::Search;
    slewInit_ = false;
    lastT_ = 0.0;
}

GimbalLos GimbalModel::scanLos(double t) const {
    const ScanSpec& s = scan_;
    GimbalLos los{s.az_center, s.el_center};
    if (s.mode == "sector" || s.mode == "raster") {
        los.az_deg = triangle(t, s.az_center, s.az_extent, s.az_rate);
        if (s.mode == "raster" && s.el_extent > 0.0 && s.az_rate != 0.0) {
            // 行扫描: 方位一行时间 = 2·az_extent/|az_rate|, 每行 el 步进
            const double rowTime = (2.0 * s.az_extent) / std::abs(s.az_rate);
            const uint32_t rows = std::max<uint32_t>(1u, uint32_t(s.el_extent > 0 ? 4 : 1));
            double row = std::fmod(t / std::max(rowTime, 1e-9), double(rows));
            los.el_deg = s.el_center - s.el_extent / 2.0 + s.el_extent * (row / double(rows));
        }
    } else if (s.mode == "circular") {
        los.az_deg = wrap180(s.az_center + s.az_rate * 6.0 * t);   // rpm→°/s
    } else if (s.mode == "conical") {
        // 锥转 (小角近似): 锥角 = az_extent/2, 锥频 = az_rate Hz
        const double cone = s.az_extent / 2.0 * kPi / 180.0;
        const double w = 2.0 * kPi * s.az_rate;
        los.az_deg = s.az_center + (cone * std::cos(w * t)) * 180.0 / kPi;
        los.el_deg = s.el_center + (cone * std::sin(w * t)) * 180.0 / kPi;
    } else if (s.mode == "rosette") {
        // 玫瑰 r=cos(kθ): θ=2πf·t, k=3 (6 瓣); 幅度 = extent/2
        const double R = s.az_extent / 2.0;
        const double w = 2.0 * kPi * s.az_rate;
        const double th = w * t;
        const double r = std::cos(3.0 * th);
        los.az_deg = s.az_center + R * r * std::cos(th);
        los.el_deg = s.el_center + R * r * std::sin(th);
    } else if (s.mode == "spiral") {
        // 阿基米德: 半径 [0, extent/2] 锯齿 (周期 = 2π/ω), 角速率 az_rate
        const double R = s.az_extent / 2.0;
        const double w = std::abs(s.az_rate) * kPi / 180.0;
        const double cyc = (w > 1e-9) ? std::fmod(t * w / (2.0 * kPi), 1.0) : 0.0;
        const double r = R * cyc;
        los.az_deg = s.az_center + r * std::cos(w * t);
        los.el_deg = s.el_center + r * std::sin(w * t);
    }
    return los;
}

GimbalLos GimbalModel::los(double t) const {
    switch (state_) {
    case State::Search:
        cur_ = scanLos(t);
        lastT_ = t;
        slewInit_ = false;
        return cur_;
    case State::Acquire:
        return hold_;                    // 停驻 (检测确认窗)
    case State::Track: {
        // slaved 指向 + 速率限幅: cur_ 朝 target 以 slewRate 推进
        // (起步指向 = holdAt 注入值; 未注入则取进入 TRACK 时的扫描位置)
        if (!slewInit_) {
            cur_ = scanLos(lastT_);
            slewInit_ = true;
        }
        const double dt = std::max(t - lastT_, 0.0);
        lastT_ = t;
        const double maxStep = slewRate_ * dt;
        const double dAz = wrap180(targetAz_ - cur_.az_deg);
        const double dEl = targetEl_ - cur_.el_deg;
        cur_.az_deg = wrap180(cur_.az_deg + std::clamp(dAz, -maxStep, maxStep));
        cur_.el_deg += std::clamp(dEl, -maxStep, maxStep);
        return cur_;
    }
    }
    return cur_;
}

void GimbalModel::setTargetAzEl(double az_deg, double el_deg) {
    targetAz_ = az_deg;
    targetEl_ = el_deg;
}

void GimbalModel::holdAt(const GimbalLos& los) {
    hold_ = los;
    cur_ = los;          // ACQUIRE 停驻 / TRACK 起步指向
    slewInit_ = true;
}

double GimbalModel::cpiSweepRad(double t_cpi) const {
    const ScanSpec& s = scan_;
    double rateDps = 0.0;   // 方位角速率峰值
    if (s.mode == "sector" || s.mode == "raster") rateDps = std::abs(s.az_rate);
    else if (s.mode == "circular") rateDps = std::abs(s.az_rate) * 6.0;
    else if (s.mode == "conical") rateDps = (s.az_extent / 2.0) * (2.0 * kPi * s.az_rate);  // 峰值角速率 = cone·ω
    else if (s.mode == "rosette") rateDps = (s.az_extent / 2.0) * (2.0 * kPi * s.az_rate) * 4.0;  // r'=−3sin(3θ)cosθ−... 峰值近似 ×4
    else if (s.mode == "spiral") rateDps = std::abs(s.az_rate) * 2.0;   // 半径项贡献近似
    return rateDps * (kPi / 180.0) * t_cpi;
}

uint32_t GimbalModel::clampCpiPulses(uint32_t nPulses, double prf_hz) const {
    uint32_t n = nPulses;
    while (n > 1 && cpiSweepRad(double(n) / prf_hz) > beamWidth_) n >>= 1;   // pow2 缩短
    return n;
}

vsg::dvec3 GimbalModel::azElToDir(double az_deg, double el_deg) {
    const double az = az_deg * kPi / 180.0, el = el_deg * kPi / 180.0;
    return vsg::dvec3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
}

void GimbalModel::dirToAzEl(const vsg::dvec3& d, double& az_deg, double& el_deg) {
    const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-12) { az_deg = el_deg = 0.0; return; }
    el_deg = std::asin(std::clamp(d.y / len, -1.0, 1.0)) * 180.0 / kPi;
    az_deg = std::atan2(d.z, d.x) * 180.0 / kPi;
}

void gimbalSelfCheck() {
    ScanSpec s;
    s.mode = "sector"; s.az_center = 10.0; s.az_extent = 60.0; s.az_rate = 30.0;
    GimbalModel g;
    g.configure(s, 0.05);
    // sector 三角波: t=0 → 左端点 center−extent/2 = −20
    const GimbalLos a0 = g.los(0.0);
    const GimbalLos a1 = g.los(1.0);      // 30°/s×1s = 30 → −20+30 = 10 (中心)
    const GimbalLos a2 = g.los(2.0);      // 60 → 右端点 +40 (折返点)
    const GimbalLos a3 = g.los(3.0);      // 回程 10
    auto ok = [](double v, double ref, double tol) { return std::abs(v - ref) <= tol; };
    const bool pass = ok(a0.az_deg, -20.0, 1e-6) && ok(a1.az_deg, 10.0, 1e-6) &&
                      ok(a2.az_deg, 40.0, 1e-6) && ok(a3.az_deg, 10.0, 1e-6);
    // circular: rpm→°/s
    s.mode = "circular"; s.az_center = 0.0; s.az_rate = 10.0;   // 10 rpm = 60°/s
    g.configure(s, 0.05);
    const GimbalLos c1 = g.los(1.5);
    // conical: 锥角 4° 锥频 2Hz, t=1/8 周期 → 45° 相位
    s.mode = "conical"; s.az_extent = 8.0; s.az_rate = 2.0; s.az_center = 0.0; s.el_center = 0.0;
    g.configure(s, 0.05);
    const GimbalLos co = g.los(1.0 / 16.0);   // ωt = π/4 → cos=sin=0.7071 → ±2.83°
    // 相干性: 30°/s × T_cpi vs 波束 0.05rad (2.86°): T_cpi=64ms → 1.92°<2.86° OK;
    // T_cpi=128ms → 3.84°>2.86° → 缩短 (64ms 满足, pow2 一步到位)
    s.mode = "sector"; s.az_rate = 30.0; s.az_extent = 60.0;
    g.configure(s, 0.05);
    const uint32_t clamped = g.clampCpiPulses(128, 1000.0);   // 128ms → 不满足 → 64 (64ms)
    // slew 限幅: TRACK 90°/s, 目标 90° 远 → 0.1s 走 9°
    g.setState(GimbalModel::State::Track);
    g.holdAt(GimbalLos{0.0, 0.0});
    g.setSlewRate(90.0);
    g.setTargetAzEl(90.0, 0.0);
    g.los(0.0);                       // 初始化起点
    const GimbalLos tr = g.los(0.1);  // → 9°
    const bool pass2 = ok(tr.az_deg, 9.0, 1e-6) && clamped == 64 &&
                       ok(c1.az_deg, 90.0, 1e-6) &&
                       ok(co.az_deg, 2.8284271, 1e-4) && ok(co.el_deg, 2.8284271, 1e-4);
    std::printf("[gimbal] 自检: sector 三角波(−20/10/40/10)%s circular(rpm)%s "
                "conical(锥转)%s 相干性缩短(128→%u)%s slew 限幅(9°)%s\n",
                pass ? "√" : "×", ok(c1.az_deg, 90.0, 1e-6) ? "√" : "×",
                (ok(co.az_deg, 2.8284271, 1e-4) && ok(co.el_deg, 2.8284271, 1e-4)) ? "√" : "×",
                clamped, clamped == 64 ? "√" : "×",
                ok(tr.az_deg, 9.0, 1e-6) ? "√" : "×");
    if (!pass || !pass2) std::fprintf(stderr, "[gimbal] 自检失败!\n");
}

} // namespace sensor
} // namespace rockyenv
