#pragma once
/// @file TrackManager.h
/// @brief 航迹管理器 — 雷达显控第二阶段增强。
///
/// 继承自 algo::radar::ITracker 以支持算法替换。
#ifndef VSGRAYSIM_GUI_TRACK_MANAGER_H
#define VSGRAYSIM_GUI_TRACK_MANAGER_H

#include <algo/radar/IRadarProcessor.h>

#include <cstdint>
#include <vector>

#include "PpiDisplay.h"

namespace vsgraysim { namespace gui {

/// 航迹管理 (M/N 起始 + 4 态卡尔曼 + 质量评估)
class TrackManager : public algo::radar::ITracker {
public:
    // 保持原有 TrackState 定义 (与接口的 TrackState 字段一致)
    struct TrackState {
        uint32_t track_id = 0;
        double bearing_deg = 0.0;
        double range_km = 0.0;
        double radial_velocity_ms = 0.0;
        double speed_ms = 0.0;
        double heading_deg = 0.0;
        double snr_db = 0.0;
        int hit_count = 0;
        int miss_count = 0;
        float quality = 0.0f;
        enum class Status : uint8_t { Tentative, Confirmed, Coast, Lost };
        Status status = Status::Tentative;
        double last_update_time = 0.0;
        double first_seen_time = 0.0;
        double prev_pos[2] = {0.0, 0.0};
        double prev_time = 0.0;
        bool   prev_pos_valid = false;
    };

    struct InitParams {
        int   M = 3;
        int   N = 5;
        int   max_coast = 10;
        float gate_km = 2.0f;
        float kalman_q = 0.5f;
        float kalman_r = 2.0f;
        int   delete_lost_frames = 3;
    };

    TrackManager() = default;
    explicit TrackManager(const InitParams& p) : params_(p) {}

    // ---- ITracker 接口实现 ----
    bool init() override { return true; }
    void reset() override { clear(); }
    std::string name() const override { return "track_manager"; }

    void processDetections(const std::vector<algo::radar::TrackTarget>& detections,
                           double sim_time) override;

    const std::vector<algo::radar::TrackState>& allTracks() const override;
    std::vector<algo::radar::TrackState> confirmedTracks() const override;
    std::vector<algo::radar::TrackTarget> toTrackTargets() const override;

    void deleteTrack(uint32_t id) override;
    void deleteAllLost() override;
    void clear() override;

    int tentativeCount() const override;
    int confirmedCount() const override;
    int coastCount() const override;

    // ---- 原始接口 (使用 PpiTarget) ----
    void processDetections(const std::vector<PpiTarget>& detections,
                           double sim_time);

    const std::vector<TrackState>& allLegacyTracks() const { return legacy_tracks_; }
    std::vector<PpiTarget> toPpiTargets() const;

    void setParams(const InitParams& p) { params_ = p; }
    const InitParams& params() const { return params_; }

private:
    struct Kalman4 {
        double x[4] = {0, 0, 0, 0};
        double P[4][4] = {{0}};
    };

    void associateUpdate(const std::vector<PpiTarget>& meas, double t);
    void updateQuality(TrackState& tr, const Kalman4& k);

    // 内部转换
    static PpiTarget trackTargetToPpi(const algo::radar::TrackTarget& t);
    static algo::radar::TrackTarget ppiToTrackTarget(const PpiTarget& t);
    static algo::radar::TrackState legacyToAlgo(const TrackState& t);
    static TrackState algoToLegacy(const algo::radar::TrackState& t);

    InitParams params_;
    std::vector<TrackState> legacy_tracks_;
    std::vector<Kalman4>    kalmans_;
    uint32_t next_track_id_ = 1;
    double last_time_ = 0.0;
    bool   has_last_time_ = false;

    // 接口所需的内部缓存
    mutable std::vector<algo::radar::TrackState> algo_tracks_cache_;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_TRACK_MANAGER_H