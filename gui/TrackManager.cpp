#include "TrackManager.h"

#include <algo/factory/AlgoFactory.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace gui {

// 自动注册到算法工厂
namespace {
const auto _reg_track = algo::AutoRegister<TrackManager>("radar", "track_manager");
}

using Status = TrackManager::TrackState::Status;

void TrackManager::processDetections(const std::vector<PpiTarget>& detections,
                                     double sim_time) {
    associateUpdate(detections, sim_time);
}

// ---- ITracker 接口实现 ----

void TrackManager::processDetections(
    const std::vector<algo::radar::TrackTarget>& detections,
    double sim_time)
{
    std::vector<PpiTarget> ppi;
    ppi.reserve(detections.size());
    for (const auto& d : detections)
        ppi.push_back(trackTargetToPpi(d));
    processDetections(ppi, sim_time);
}

const std::vector<algo::radar::TrackState>& TrackManager::allTracks() const
{
    algo_tracks_cache_.clear();
    algo_tracks_cache_.reserve(legacy_tracks_.size());
    for (const auto& t : legacy_tracks_)
        algo_tracks_cache_.push_back(legacyToAlgo(t));
    return algo_tracks_cache_;
}

std::vector<algo::radar::TrackTarget> TrackManager::toTrackTargets() const
{
    std::vector<algo::radar::TrackTarget> out;
    out.reserve(legacy_tracks_.size());
    for (const auto& t : legacy_tracks_) {
        if (t.status == Status::Lost) continue;
        out.push_back(ppiToTrackTarget(
            {t.bearing_deg, t.range_km, t.snr_db,
             t.status == Status::Confirmed || t.status == Status::Coast}));
    }
    return out;
}

// ---- 内部转换 ----

PpiTarget TrackManager::trackTargetToPpi(const algo::radar::TrackTarget& t)
{
    return {t.bearing_deg, t.range_km, t.snr_db, t.tracked};
}

algo::radar::TrackTarget TrackManager::ppiToTrackTarget(const PpiTarget& t)
{
    return {t.bearing_deg, t.range_km, t.snr_db, t.tracked};
}

algo::radar::TrackState TrackManager::legacyToAlgo(const TrackState& t)
{
    algo::radar::TrackState a;
    a.track_id = t.track_id;
    a.bearing_deg = t.bearing_deg;
    a.range_km = t.range_km;
    a.radial_velocity_ms = t.radial_velocity_ms;
    a.speed_ms = t.speed_ms;
    a.heading_deg = t.heading_deg;
    a.snr_db = t.snr_db;
    a.hit_count = t.hit_count;
    a.miss_count = t.miss_count;
    a.quality = t.quality;
    a.status = static_cast<algo::radar::TrackState::Status>(
        static_cast<uint8_t>(t.status));
    a.last_update_time = t.last_update_time;
    a.first_seen_time = t.first_seen_time;
    return a;
}

TrackManager::TrackState TrackManager::algoToLegacy(const algo::radar::TrackState& a)
{
    TrackState t;
    t.track_id = a.track_id;
    t.bearing_deg = a.bearing_deg;
    t.range_km = a.range_km;
    t.radial_velocity_ms = a.radial_velocity_ms;
    t.speed_ms = a.speed_ms;
    t.heading_deg = a.heading_deg;
    t.snr_db = a.snr_db;
    t.hit_count = a.hit_count;
    t.miss_count = a.miss_count;
    t.quality = a.quality;
    t.status = static_cast<Status>(static_cast<uint8_t>(a.status));
    t.last_update_time = a.last_update_time;
    t.first_seen_time = a.first_seen_time;
    return t;
}

void TrackManager::associateUpdate(const std::vector<PpiTarget>& meas,
                                    double t) {
    const double dt = has_last_time_ ? std::max(1e-3, t - last_time_) : 0.05;
    last_time_ = t;
    has_last_time_ = true;

    const size_t nt = legacy_tracks_.size();
    const size_t nm = meas.size();
    std::vector<int> meas_of_track(nt, -1);
    std::vector<bool> meas_used(nm, false);

    // ---- 粗关联 (最近邻 + 门限) ----
    for (size_t i = 0; i < nt; ++i) {
        TrackState& tr = legacy_tracks_[i];
        double best = 1e9; int bj = -1;
        for (size_t j = 0; j < nm; ++j) {
            if (meas_used[j]) continue;
            const double dr = std::abs(meas[j].range_km - tr.range_km);
            const double db = std::abs(meas[j].bearing_deg - tr.bearing_deg);
            if (dr < params_.gate_km && db < 10.0) {
                const double d = dr + db * 0.05;
                if (d < best) { best = d; bj = int(j); }
            }
        }
        if (bj >= 0) { meas_of_track[i] = bj; meas_used[size_t(bj)] = true; }
    }

    // ---- 航迹更新 / 外推 ----
    for (size_t i = 0; i < nt; ++i) {
        TrackState& tr = legacy_tracks_[i];
        Kalman4& k = kalmans_[i];

        // 预测
        const double dt2 = dt * dt;
        k.x[0] += k.x[1] * dt;
        k.x[2] += k.x[3] * dt;
        for (int d = 0; d < 4; ++d) k.P[d][d] += params_.kalman_q * dt2;

        const int j = meas_of_track[i];
        if (j >= 0) {
            // 量测更新 (bearing, range)
            const double zb = meas[size_t(j)].bearing_deg;
            const double zr = meas[size_t(j)].range_km;
            for (int axis = 0; axis < 2; ++axis) {
                const int ipos = axis * 2, ivel = axis * 2 + 1;
                const double z = axis == 0 ? zb : zr;
                const double S = k.P[ipos][ipos] + params_.kalman_r;
                const double K0 = k.P[ipos][ipos] / S;
                const double K1 = k.P[ivel][ipos] / S;
                const double innov = z - k.x[ipos];
                k.x[ipos] += K0 * innov;
                k.x[ivel] += K1 * innov;
                const double p00 = k.P[ipos][ipos], p01 = k.P[ipos][ivel];
                const double p10 = k.P[ivel][ipos], p11 = k.P[ivel][ivel];
                k.P[ipos][ipos] = p00 - K0 * p00;
                k.P[ipos][ivel] = p01 - K0 * p01;
                k.P[ivel][ipos] = p10 - K1 * p00;
                k.P[ivel][ivel] = p11 - K1 * p01;
            }
            tr.bearing_deg = k.x[0];
            tr.range_km = k.x[2];
            tr.radial_velocity_ms = k.x[3] * 1000.0;  // km/s -> m/s
            tr.snr_db = meas[size_t(j)].snr_db;
            tr.hit_count++;
            tr.miss_count = 0;
            tr.last_update_time = t;

            // 位置差分 -> 全速度/航向
            if (tr.prev_pos_valid) {
                const double dtm = std::max(1e-3, t - tr.prev_time);
                const double dx = (tr.range_km * 1000.0 *
                    std::sin(tr.bearing_deg * M_PI / 180.0)) - tr.prev_pos[0];
                const double dy = (tr.range_km * 1000.0 *
                    std::cos(tr.bearing_deg * M_PI / 180.0)) - tr.prev_pos[1];
                tr.speed_ms = std::hypot(dx, dy) / dtm;
                tr.heading_deg = std::atan2(dx, dy) * 180.0 / M_PI;
            }
            tr.prev_pos[0] = tr.range_km * 1000.0 *
                std::sin(tr.bearing_deg * M_PI / 180.0);
            tr.prev_pos[1] = tr.range_km * 1000.0 *
                std::cos(tr.bearing_deg * M_PI / 180.0);
            tr.prev_pos_valid = true;
            tr.prev_time = t;

            // M/N 起始确认
            if (tr.status == Status::Tentative && tr.hit_count >= params_.M)
                tr.status = Status::Confirmed;
            else if (tr.status == Status::Coast)
                tr.status = Status::Confirmed;
        } else {
            // 未关联 -> 外推
            tr.miss_count++;
            tr.bearing_deg = k.x[0];
            tr.range_km = k.x[2];
            tr.radial_velocity_ms = k.x[3] * 1000.0;
            if (tr.status == Status::Confirmed && tr.miss_count <= params_.max_coast)
                tr.status = Status::Coast;
            if (tr.miss_count > params_.max_coast)
                tr.status = Status::Lost;
        }
        updateQuality(tr, k);
    }

    // ---- 新量测 -> 新 tentative 航迹 ----
    for (size_t j = 0; j < nm; ++j) {
        if (meas_used[j]) continue;
        TrackState tr;
        tr.track_id = next_track_id_++;
        tr.bearing_deg = meas[j].bearing_deg;
        tr.range_km = meas[j].range_km;
        tr.snr_db = meas[j].snr_db;
        tr.hit_count = 1;
        tr.status = Status::Tentative;
        tr.last_update_time = t;
        tr.first_seen_time = t;

        Kalman4 k;
        k.x[0] = tr.bearing_deg;
        k.x[2] = tr.range_km;
        for (int d = 0; d < 4; ++d) k.P[d][d] = 10.0;

        legacy_tracks_.push_back(tr);
        kalmans_.push_back(k);
    }

    // ---- 删除: Lost 保留 delete_lost_frames 帧后移除 ----
    std::vector<TrackState> keep_tr;
    std::vector<Kalman4> keep_k;
    keep_tr.reserve(legacy_tracks_.size());
    for (size_t i = 0; i < legacy_tracks_.size(); ++i) {
        const bool remove = legacy_tracks_[i].status == Status::Lost &&
                            legacy_tracks_[i].miss_count >
                                params_.max_coast + params_.delete_lost_frames;
        if (!remove) { keep_tr.push_back(legacy_tracks_[i]); keep_k.push_back(kalmans_[i]); }
    }
    legacy_tracks_.swap(keep_tr);
    kalmans_.swap(keep_k);
}

void TrackManager::updateQuality(TrackState& tr, const Kalman4& k) {
    // 质量 = 命中率(0.5) + SNR 因子(0.3) + 协方差置信(0.2)
    const double total = double(tr.hit_count + tr.miss_count);
    const double hit_rate = total > 0 ? tr.hit_count / total : 0.0;
    const double snr_f = std::clamp((tr.snr_db - 6.0) / 24.0, 0.0, 1.0);
    const double cov = k.P[0][0] + k.P[2][2];
    const double cov_f = std::clamp(1.0 - cov / 20.0, 0.0, 1.0);
    tr.quality = float(0.5 * hit_rate + 0.3 * snr_f + 0.2 * cov_f);
}

std::vector<algo::radar::TrackState> TrackManager::confirmedTracks() const {
    std::vector<algo::radar::TrackState> out;
    for (const auto& t : legacy_tracks_)
        if (t.status == Status::Confirmed || t.status == Status::Coast)
            out.push_back(legacyToAlgo(t));
    return out;
}

std::vector<PpiTarget> TrackManager::toPpiTargets() const {
    std::vector<PpiTarget> out;
    out.reserve(legacy_tracks_.size());
    for (const auto& t : legacy_tracks_) {
        if (t.status == Status::Lost) continue;
        out.push_back({t.bearing_deg, t.range_km, t.snr_db,
                       t.status == Status::Confirmed || t.status == Status::Coast});
    }
    return out;
}

void TrackManager::deleteTrack(uint32_t id) {
    for (size_t i = 0; i < legacy_tracks_.size(); ++i) {
        if (legacy_tracks_[i].track_id == id) {
            legacy_tracks_.erase(legacy_tracks_.begin() + std::ptrdiff_t(i));
            kalmans_.erase(kalmans_.begin() + std::ptrdiff_t(i));
            return;
        }
    }
}

void TrackManager::deleteAllLost() {
    std::vector<TrackState> keep_tr;
    std::vector<Kalman4> keep_k;
    for (size_t i = 0; i < legacy_tracks_.size(); ++i) {
        if (legacy_tracks_[i].status != Status::Lost) {
            keep_tr.push_back(legacy_tracks_[i]);
            keep_k.push_back(kalmans_[i]);
        }
    }
    legacy_tracks_.swap(keep_tr);
    kalmans_.swap(keep_k);
}

void TrackManager::clear() {
    legacy_tracks_.clear();
    kalmans_.clear();
    has_last_time_ = false;
}

int TrackManager::tentativeCount() const {
    int n = 0;
    for (const auto& t : legacy_tracks_) if (t.status == Status::Tentative) ++n;
    return n;
}
int TrackManager::confirmedCount() const {
    int n = 0;
    for (const auto& t : legacy_tracks_)
        if (t.status == Status::Confirmed) ++n;
    return n;
}
int TrackManager::coastCount() const {
    int n = 0;
    for (const auto& t : legacy_tracks_) if (t.status == Status::Coast) ++n;
    return n;
}

}} // namespace vsgraysim::gui
