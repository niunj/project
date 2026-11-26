#pragma once
/// @file EquipmentSession.h
/// @brief 显控"装备会话" — 一个链接装备对应一条数据链 + 一套处理/控制。
///
/// 每个 EquipmentSession 绑订一个在线装备:
///   - 通过 SimDataBroker 订阅该装备 (GPU 句柄数据 + 元数据)
///   - tick() 从 broker 拉取最新帧并缓存最近一帧 / 帧号 / sim_time / 帧计数
///   - 提供对装备的闭环控制 (开关机 / 工作模式) 与状态查询
///
/// 同一个工作台的多个 Session 即"多装备协同": 由外部 Workbench 统一 tick,
/// 并可通过光照目标互联 (航迹互引、共享目标表)。
#ifndef VSGRAYSIM_GUI_DISPLAY_EQUIPMENT_SESSION_H
#define VSGRAYSIM_GUI_DISPLAY_EQUIPMENT_SESSION_H

#include <QString>
#include <QElapsedTimer>
#include <optional>
#include <cstdint>
#include <string>

#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/broker/SensorFrames.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include "WorkMode.h"

namespace vsgraysim { namespace gui { namespace display {

class EquipmentSession {
public:
    explicit EquipmentSession(EquipmentChannelSpec spec);
    ~EquipmentSession();
    EquipmentSession(const EquipmentSession&) = delete;
    EquipmentSession& operator=(const EquipmentSession&) = delete;

    /// 从未绑定到绑定一个在线装备; 返回是否成功订阅。
    bool bind(uint32_t equipment_id, const std::string& name);

    /// 取消绑定 (取消订阅)
    void unbind();

    /// 拉取最新帧并更新统计 (由 Workbench 定时驱动)
    void tick(std::optional<uint64_t> surface={},bool surfaceReady=true,bool localPaused=false);

    // ---- 状态 ----
    bool      bound() const { return equipment_id_ != 0; }
    uint32_t  equipmentId() const { return equipment_id_; }
    uint64_t  bindingGeneration() const { return binding_generation_; }
    std::string equipmentName() const { return name_; }
    SensorType sensorType() const { return spec_.sensor_type; }
    EquipmentWorkRole role() const { return spec_.role; }
    const EquipmentChannelSpec& spec() const { return spec_; }

    uint64_t  frames() const { return frames_; }
    uint64_t  lastFrameId() const { return last_frame_id_; }
    double    lastSimTime() const { return last_sim_time_; }
    bool      powered() const { return powered_; }
    bool      active() const { return active_; }
    bool      hasFrame() const { return has_frame_; }
    bool      connected() const { return connected_; }
    bool      paused() const { return paused_; }
    bool      stale() const;
    double    frameAgeSeconds() const;
    QString   statusText() const;
    bool      powerRequestPending() const { return power_request_.has_value(); }
    QString   lastControlStatus() const { return control_status_; }
    bool      radarModeSupported() const { return false; }

    // 最近一帧访问 (按传感器类型; 不适用返回 nullptr)
    const RadarFrame* radar() const { return spec_.sensor_type == SensorType::RADAR ? (radar_view_?radar_view_.get():&radar_) : nullptr; }
    const IrFrame*    ir()    const { return spec_.sensor_type == SensorType::IR ? (ir_view_?ir_view_.get():&ir_) : nullptr; }
    const SarFrame*   sar()   const { return spec_.sensor_type == SensorType::SAR ? (sar_view_?sar_view_.get():&sar_) : nullptr; }
    const VisFrame*   vis()   const { return spec_.sensor_type == SensorType::VIS ? (vis_view_?vis_view_.get():&vis_) : nullptr; }

    std::shared_ptr<const RadarFrame> radarSnapshot()const{return radar_view_;}
    std::shared_ptr<const SarFrame> sarSnapshot()const{return sar_view_;}
    std::shared_ptr<const IrFrame> irSnapshot()const{return ir_view_;}
    std::shared_ptr<const VisFrame> visSnapshot()const{return vis_view_;}
    std::shared_ptr<const SonarFrame> sonarSnapshot()const{return sonar_view_;}

    // ---- 雷达 GPU 驻留路径 (零拷贝) ----
    /// 最近雷达帧数据是否驻留 GPU (rd_map 为空, 本体经 radarGpuHandle() 消费)
    bool radarOnGpu() const { return radar_on_gpu_; }
    /// 雷达帧数据通道 GPU 句柄 (device_ptr 指向交织 I/Q 复数 RD 图)
    const GpuBufferHandle& radarGpuHandle() const { return radar_gpu_handle_; }

    // ---- 对装备的闭环控制 ----
    bool setPowered(bool on);        ///< true = request submitted, not an execution acknowledgement
    void setPowerOff() { setPowered(false); }

    /// 雷达工作模式 (SEARCH/TRACK/TWS) 下发给装备; 非雷达通道忽略。
    bool setRadarMode(int mode_index); ///< Unsupported until a backend mode executor exists.

private:
    EquipmentChannelSpec spec_;
    uint32_t    equipment_id_ = 0;
    uint64_t    binding_generation_ = 0;
    std::string name_;
    uint64_t    frames_ = 0;
    uint64_t    last_frame_id_ = 0;
    double      last_sim_time_ = 0.0;
    bool        powered_ = true;
    bool        active_ = false;

    std::shared_ptr<const RadarFrame> radar_view_;
    std::shared_ptr<const SarFrame> sar_view_;
    std::shared_ptr<const IrFrame> ir_view_;
    std::shared_ptr<const VisFrame> vis_view_;
    std::shared_ptr<const SonarFrame> sonar_view_;
    RadarFrame  radar_;       ///< 最近雷达帧 (如适用; GPU 路径下仅元数据)
    IrFrame     ir_;          ///< 最近红外帧
    SarFrame    sar_;         ///< 最近 SAR 帧
    VisFrame    vis_;
    std::optional<uint64_t> expected_surface_;
    bool waiting_surface_=false;
    bool        has_frame_ = false;
    bool        connected_ = false, paused_ = false;
    bool        bound_remote_ = false;
    double      stale_after_s_ = 3.0;
    QElapsedTimer frame_timer_, control_timer_;
    uint8_t     last_stage_ = 255;
    std::optional<bool> power_request_;
    uint64_t    request_seen_ms_ = 0;
    QString     control_status_;
    void noteFrame(uint64_t id,double time,uint8_t stage);

    // 雷达 GPU 驻留路径: 数据本体不下载, 面板经句柄在 GPU 上处理
    bool            radar_on_gpu_ = false;
    GpuBufferHandle radar_gpu_handle_;
};

}}} // namespace vsgraysim::gui::display

#endif // VSGRAYSIM_GUI_DISPLAY_EQUIPMENT_SESSION_H
