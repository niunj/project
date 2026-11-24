#pragma once
/// @file MtiMtdProcessor.h
/// @brief MTI/MTD 动目标处理 — 雷达显控第二阶段增强。
///
/// 继承自 algo::radar::IMtiMtdProcessor 以支持算法替换。
#ifndef VSGRAYSIM_GUI_MTI_MTD_PROCESSOR_H
#define VSGRAYSIM_GUI_MTI_MTD_PROCESSOR_H

#include <algo/radar/IRadarProcessor.h>

#include <cstdint>
#include <deque>
#include <vector>

namespace vsgraysim { namespace gui {

class MtiMtdProcessor : public algo::radar::IMtiMtdProcessor {
public:
    enum class Mode { Bypass = 0, TwoPulse, ThreePulse, Mtd5Tap };

    struct Params {
        Mode mode = Mode::Bypass;
        float gain_comp_db = 6.0f;
    };

    MtiMtdProcessor() = default;
    explicit MtiMtdProcessor(const Params& p) : params_(p) {}

    // ---- IMtiMtdProcessor 接口实现 ----
    bool init() override { return true; }
    void reset() override { history_.clear(); }
    std::string name() const override { return "mti_mtd"; }

    algo::radar::ProcessedFrame process(const algo::radar::RadarFrame& frame) override
    {
        auto old = process(frame.data, frame.width, frame.height);
        return {std::move(old), frame.width, frame.height};
    }

    bool active() const override { return params_.mode != Mode::Bypass; }

    /// 输入最新一帧 RD 幅度图 (dB), 返回处理后的幅度图 (dB)。(原始接口)
    std::vector<float> process(const std::vector<float>& rd_db,
                               uint32_t w, uint32_t h);

    void setParams(const Params& p) { params_ = p; }
    const Params& params() const { return params_; }

private:
    Params params_;
    std::deque<std::vector<float>> history_;
    uint32_t w_ = 0, h_ = 0;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_MTI_MTD_PROCESSOR_H