#pragma once
/// @file IRadarProcessor.h
/// @brief 雷达信号处理器抽象接口族 (MTI/MTD/CFAR/跟踪)。

#include "../IAlgoBase.h"
#include <cstdint>
#include <vector>

namespace vsgraysim::algo::radar {

/// 雷达信号处理帧 (RD 幅度图)
struct RadarFrame {
    std::vector<float> data;  ///< 幅度数据 (dB, 行主序)
    uint32_t width = 0;       ///< 距离 bin 数
    uint32_t height = 0;      ///< 方位 bin 数
    double   timestamp = 0.0; ///< 时间戳
};

/// 信号处理结果
struct ProcessedFrame {
    std::vector<float> data;  ///< 处理后幅度 (dB)
    uint32_t width = 0;
    uint32_t height = 0;
};

/// MTI/MTD 动目标处理器接口
class IMtiMtdProcessor : public IAlgoBase {
public:
    ~IMtiMtdProcessor() override = default;

    /// 输入最新一帧 RD 幅度图, 返回处理后的幅度图
    /// 内部维护帧历史 (对消需要 2~3 帧)
    virtual ProcessedFrame process(const RadarFrame& frame) = 0;

    /// 当前模式是否需要缓存帧
    virtual bool active() const = 0;
};

/// CFAR 检测器接口
class ICfarDetector : public IAlgoBase {
public:
    ~ICfarDetector() override = default;

    /// 检测结果
    struct Detection {
        int   row = 0, col = 0;
        float snr_db = 0.0f;
    };

    /// 检测 + 聚类, 输出质心检测
    virtual std::vector<Detection> detect(const RadarFrame& frame) = 0;

    /// 最近一次检测的虚警单元统计
    virtual int lastCellCount() const = 0;
};

/// 航迹跟踪目标
struct TrackTarget {
    double bearing_deg = 0.0;  ///< 方位 (度)
    double range_km = 0.0;     ///< 距离 (km)
    double snr_db = 0.0;       ///< 信噪比 (dB)
    bool   tracked = false;     ///< 是否已被跟踪
};

/// 航迹状态
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
};

/// 航迹管理器接口
class ITracker : public IAlgoBase {
public:
    ~ITracker() override = default;

    /// 处理一帧检测, 更新全部航迹
    virtual void processDetections(const std::vector<TrackTarget>& detections,
                                   double sim_time) = 0;

    /// 全部航迹
    virtual const std::vector<TrackState>& allTracks() const = 0;

    /// 仅已确认航迹
    virtual std::vector<TrackState> confirmedTracks() const = 0;

    /// 转为 TrackTarget
    virtual std::vector<TrackTarget> toTrackTargets() const = 0;

    /// 删除航迹
    virtual void deleteTrack(uint32_t id) = 0;
    virtual void deleteAllLost() = 0;
    virtual void clear() = 0;

    /// 统计
    virtual int tentativeCount() const = 0;
    virtual int confirmedCount() const = 0;
    virtual int coastCount() const = 0;
};

} // namespace vsgraysim::algo::radar