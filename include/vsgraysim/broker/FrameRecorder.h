#pragma once
/// @file FrameRecorder.h
/// @brief 装备数据帧录制/回放 (第二阶段专业增强)。
///
/// 录制: 将每帧 SignalFrame 序列化写入二进制文件 (header + payload),
///       供离线回放/事后分析/算法验证。
/// 回放: 顺序读取录制文件, 重建 SignalFrame, 可替代实时数据源。
///
/// 文件格式 (小端):
///   [FileHeader][FrameBlock]*N
///   FileHeader: magic(4B "VSRD") + version(4B) + frame_count(8B)
///   FrameBlock: FrameHead + samples(复数对) + range_axis + doppler_axis
///   FrameHead:  frame_id/sim_time/sensor_type/sensor_id/w/h/fs/prf/
///               carrier_hz/coherent_pulses/range_min/range_max/
///               n_samples/n_range/n_doppler
///   v2: one product-stage byte follows FrameHead; player also reads v1.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "vsgraysim/layer3b_signal/SignalFrame.h"

namespace vsgraysim {

/// 帧录制器 — 一个装备通道一个实例
class FrameRecorder {
public:
    ~FrameRecorder();

    /// 打开录制文件 (覆盖); 目录不存在时自动创建
    bool start(const std::string& file_path);
    /// 写入一帧 (录制中有效)
    bool recordFrame(const SignalFrame& frame);
    /// 结束录制并落盘 (回填文件头帧计数)
    void stop();

    bool isRecording() const { return out_.is_open(); }
    uint64_t recordedFrames() const { return frame_count_; }
    const std::string& filePath() const { return path_; }

private:
    std::ofstream out_;
    std::string   path_;
    uint64_t      frame_count_ = 0;
    uint64_t      header_pos_ = 0;
};

/// 帧回放器 — 顺序读取录制文件
class FramePlayer {
public:
    /// 打开录制文件; 返回总帧数 (0 = 打开失败或空)
    uint64_t open(const std::string& file_path);
    /// 读取下一帧; 到结尾返回 false
    bool nextFrame(SignalFrame* out);
    /// 跳到指定帧号 (顺序扫描)
    bool seek(uint64_t frame_index);
    void close();

    bool isOpen() const { return in_.is_open(); }
    uint64_t totalFrames() const { return total_frames_; }
    uint64_t currentFrameIndex() const { return cur_index_; }
    /// 录制时长 (s, 依据最大 sim_time — 由 open 时扫描或读取时更新)
    double durationSeconds() const { return duration_s_; }

private:
    std::ifstream in_;
    std::string   path_;
    uint64_t total_frames_ = 0;
    uint64_t cur_index_ = 0;
    double   duration_s_ = 0.0;
    uint32_t version_ = 1;
};

} // namespace vsgraysim
