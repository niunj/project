#include "MtiMtdProcessor.h"

#include <algo/factory/AlgoFactory.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace gui {

// 自动注册到算法工厂
namespace {
const auto _reg_mti = algo::AutoRegister<MtiMtdProcessor>("radar", "mti_mtd_processor");
}

std::vector<float> MtiMtdProcessor::process(const std::vector<float>& rd_db,
                                            uint32_t w, uint32_t h) {
    if (params_.mode == Mode::Bypass || rd_db.size() != size_t(w) * h) {
        return rd_db;
    }
    if (w != w_ || h != h_) { history_.clear(); w_ = w; h_ = h; }

    history_.push_back(rd_db);
    while (history_.size() > 3) history_.pop_front();

    const size_t n = size_t(w) * h;
    std::vector<float> out(n, -120.0f);
    const float comp = params_.gain_comp_db;

    const auto& h0 = history_[history_.size() - 1];            // 最新
    const bool has1 = history_.size() >= 2;
    const bool has2 = history_.size() >= 3;
    const auto& h1 = has1 ? history_[history_.size() - 2] : h0; // 次 new
    const auto& h2 = has2 ? history_[history_.size() - 3] : h1; // 最旧

    switch (params_.mode) {
    case Mode::Bypass:
        return rd_db;

    case Mode::TwoPulse: {
        if (!has1) return rd_db;   // 不足两帧直接透传
        // dB 域近似对消: 幅度差 (线性域 |a-b|, 用 dB 近似 20log10|10^(a/20)-10^(b/20)|)
        for (size_t i = 0; i < n; ++i) {
            const float a = std::pow(10.0f, h0[i] / 20.0f);
            const float b = std::pow(10.0f, h1[i] / 20.0f);
            const float d = std::abs(a - b);
            out[i] = d > 1e-9f ? 20.0f * std::log10(d) + comp : -120.0f;
        }
        break;
    }

    case Mode::ThreePulse: {
        if (!has2) return rd_db;
        for (size_t i = 0; i < n; ++i) {
            const float a = std::pow(10.0f, h0[i] / 20.0f);
            const float b = std::pow(10.0f, h1[i] / 20.0f);
            const float c = std::pow(10.0f, h2[i] / 20.0f);
            const float d = std::abs(a - 2.0f * b + c);
            out[i] = d > 1e-9f ? 20.0f * std::log10(d) + comp : -120.0f;
        }
        break;
    }

    case Mode::Mtd5Tap: {
        // 5 抽头多普勒滤波器组近似: 跨 3 帧加权组合 (峰值滤波响应)
        if (!has2) return rd_db;
        static const float taps[5] = {-0.103f, -0.207f, 0.621f, -0.207f, -0.103f};
        // 简化为 3 帧版本: 中心帧强加权 + 两侧弱抑制 (静止杂波相消)
        for (size_t i = 0; i < n; ++i) {
            const float a = std::pow(10.0f, h2[i] / 20.0f);
            const float b = std::pow(10.0f, h1[i] / 20.0f);
            const float c = std::pow(10.0f, h0[i] / 20.0f);
            const float d = std::abs(-0.207f * a + 0.621f * b + 0.621f * c - 0.207f * a);
            out[i] = d > 1e-9f ? 20.0f * std::log10(d) + comp : -120.0f;
        }
        break;
    }
    }

    return out;
}

}} // namespace vsgraysim::gui
