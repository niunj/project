#pragma once
/// @file StateRingBuffer.h —— 物理真值环形缓冲 (设计文档 §3/§7, B4 统一 3D 时序)。
///
/// 物理引擎每物理帧写入带时间戳的真值 (位姿/速度); 传感器链按自身节拍
/// (成像曝光时刻 t_exp / 相干逐脉冲 t_pulse(n)) 插值消费 —— 三时钟域解耦
/// 的粘合点 (§7 保证机制 2 "ring buffer + 插值")。
///
/// 语义:
///   - push: 有限、非负、单调时间; 同时刻替换; 满时覆盖最旧样本;
///   - sample(t): 位置/速度 Hermite 插值; 端点外匀速外推至配置的期限;
///     超出期限返回 false, 不把过期真值冒充成请求时刻的真值。
#ifndef ROCKY_ENV_STATE_RING_BUFFER_H
#define ROCKY_ENV_STATE_RING_BUFFER_H

#include <vsg/maths/vec3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rockyenv {
namespace sensor {

/// 物理真值样本 (一物理帧一行)。
struct StateSample {
    double t = 0.0;                 ///< 仿真时统 (s, 绝对时轴)
    vsg::dvec3 pos{0.0, 0.0, 0.0};  ///< ENU 位置 (m)
    vsg::dvec3 vel{0.0, 0.0, 0.0};  ///< ENU 速度 (m/s, 端点外推用)
};

/// 定容环形缓冲 + 时间插值采样 (header-only, 零依赖 VSG 数学头以外)。
class StateRingBuffer {
public:
    explicit StateRingBuffer(size_t capacity = 1024, double extrapolationLimit = 1.0)
        : buf_(capacity ? capacity : size_t(1)), extrapolation_limit_(extrapolationLimit) {
        if (!std::isfinite(extrapolationLimit) || extrapolationLimit < 0.0)
            throw std::invalid_argument("invalid truth extrapolation limit");
    }

    /// 写入真值 (乱序丢弃; 满覆盖最旧)。
    bool push(const StateSample& s) {
        if (!std::isfinite(s.t) || s.t < 0.0 || !finite(s.pos) || !finite(s.vel)) return false;
        if (count_ > 0 && s.t < newestT()) return false;
        if (count_ > 0 && s.t == newestT()) {
            buf_[(head_ + count_ - 1) % buf_.size()] = s;
            return true;
        }
        buf_[(head_ + count_) % buf_.size()] = s;
        if (count_ < buf_.size()) ++count_;
        else head_ = (head_ + 1) % buf_.size();
        return true;
    }

    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }
    double oldestT() const { return count_ ? buf_[head_].t : std::numeric_limits<double>::quiet_NaN(); }
    double newestT() const { return count_ ? buf_[(head_ + count_ - 1) % buf_.size()].t : std::numeric_limits<double>::quiet_NaN(); }

    /// Failed queries leave out unchanged. A zero extrapolation limit requires
    /// actual bracketing physics samples (recommended for measurement production).
    bool sample(double t, StateSample& out) const {
        if (count_ == 0 || !std::isfinite(t) || t < 0.0) return false;
        const StateSample& oldest = buf_[head_];
        const StateSample& newest = buf_[(head_ + count_ - 1) % buf_.size()];
        if (t <= oldest.t) return extrapolate(oldest, t, out);
        if (t >= newest.t) return extrapolate(newest, t, out);
        // 二分: 找 lo 使 at(lo).t <= t < at(hi).t
        size_t lo = 0, hi = count_ - 1;
        while (hi - lo > 1) {
            const size_t mid = lo + (hi - lo) / 2;
            if (at(mid).t <= t) lo = mid; else hi = mid;
        }
        const StateSample& a = at(lo);
        const StateSample& b = at(hi);
        const double h = b.t - a.t;
        const double u = (t - a.t) / h, u2 = u * u, u3 = u2 * u;
        out.t = t;
        out.pos = a.pos * (2*u3 - 3*u2 + 1) + a.vel * (h * (u3 - 2*u2 + u))
                + b.pos * (-2*u3 + 3*u2) + b.vel * (h * (u3 - u2));
        out.vel = a.pos * ((6*u2 - 6*u) / h) + a.vel * (3*u2 - 4*u + 1)
                + b.pos * ((-6*u2 + 6*u) / h) + b.vel * (3*u2 - 2*u);
        return true;
    }

private:
    const StateSample& at(size_t logical) const {
        return buf_[(head_ + logical) % buf_.size()];
    }

    static bool finite(const vsg::dvec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    bool extrapolate(const StateSample& anchor, double t, StateSample& out) const {
        const double dt = t - anchor.t;
        if (std::abs(dt) > extrapolation_limit_) return false;
        out = anchor;
        out.t = t;
        out.pos = anchor.pos + anchor.vel * dt;
        return true;
    }

    std::vector<StateSample> buf_;
    size_t head_ = 0;    ///< 最旧样本物理下标
    size_t count_ = 0;
    double extrapolation_limit_;
};

} // namespace sensor
} // namespace rockyenv

#endif // ROCKY_ENV_STATE_RING_BUFFER_H
