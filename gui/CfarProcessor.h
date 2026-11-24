#pragma once
/// @file CfarProcessor.h
/// @brief 多算法恒虚警检测 (CA/SO/GO/OS-CFAR) — 雷达显控第二阶段增强。
///
/// 输入: RD 幅度图 (dB)。
/// 输出: 检测单元坐标 + SNR。
/// 算法:
///   CA — 单元平均 (均匀杂波)
///   SO — 最小选择 (杂波边缘, 干扰目标在参考窗)
///   GO — 最大选择 (杂波边缘)
///   OS — 有序统计 (多目标环境)
///
/// 继承自 algo::radar::ICfarDetector 以支持算法替换。
#ifndef VSGRAYSIM_GUI_CFAR_PROCESSOR_H
#define VSGRAYSIM_GUI_CFAR_PROCESSOR_H

#include <algo/radar/IRadarProcessor.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace vsgraysim { namespace gui {

class CfarProcessor : public algo::radar::ICfarDetector {
public:
    enum class Algorithm { CA = 0, SO, GO, OS };

    struct Params {
        Algorithm algo = Algorithm::CA;
        float  threshold_db = 18.0f;  ///< 检测门限 (SNR dB, 相对噪声底)
        int    guard_bins  = 2;       ///< 保护单元 (两侧)
        int    ref_bins    = 6;       ///< 参考单元 (两侧)
        int    os_rank     = 12;      ///< OS-CFAR 排序取值秩 (0-based, 相对参考窗起点)
        int    cluster_radius = 3;    ///< 聚类半径 (单元)
        float max_dynamic_range_db=100.0f; ///< Relative numeric floor for float32 FFT tails; not receiver calibration
        float  min_snr_db  = 6.0f;    ///< 最小可输出 SNR
    };

    /// 一个检测点 (像素坐标 + SNR)
    struct Detection {
        int   row = 0, col = 0;
        float snr_db = 0.0f;
    };

    // ---- ICfarDetector 接口实现 ----
    bool init() override { return true; }
    void reset() override { last_cell_count = 0; }
    std::string name() const override { return "ca_cfar"; }

    std::vector<algo::radar::ICfarDetector::Detection>
    detect(const algo::radar::RadarFrame& frame) override
    {
        auto old = detect(frame.data, frame.width, frame.height);
        std::vector<algo::radar::ICfarDetector::Detection> r;
        r.reserve(old.size());
        for (auto& d : old)
            r.push_back({d.row, d.col, d.snr_db});
        return r;
    }

    /// 检测 + 聚类, 输出质心检测 (原始接口)
    std::vector<Detection> detect(const std::vector<float>& rd_db,
                                  uint32_t w, uint32_t h) const;

    void setParams(const Params& p) { params_ = p; }
    const Params& params() const { return params_; }

    /// 最近一次检测的虚警单元统计 (供状态栏)
    int lastCellCount() const override { return last_cell_count; }
    mutable int last_cell_count = 0;

private:
    float noiseLevel(const std::vector<float>& rd, uint32_t w, uint32_t h,
                     int row, int col) const;
    void cluster(const std::vector<Detection>& cells, uint32_t w, uint32_t h,
                 std::vector<Detection>& out) const;

    Params params_;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_CFAR_PROCESSOR_H
