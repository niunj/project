#include "EquipmentSession.h"
#include "vsgraysim/broker/BrokerNet.h"
#include "vsgraysim/broker/EquipmentInfo.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <atomic>

namespace vsgraysim { namespace gui { namespace display {
EquipmentSession::EquipmentSession(EquipmentChannelSpec spec):spec_(std::move(spec)) {}
EquipmentSession::~EquipmentSession() { unbind(); }
bool EquipmentSession::bind(uint32_t id,const std::string& name) {
    unbind();
    if(!id || !SimDataBroker::instance().subscribe(id)) return false;
    static std::atomic<uint64_t> nextBinding{1};
    binding_generation_=nextBinding.fetch_add(1,std::memory_order_relaxed);
    const auto info=EquipmentRegistry::instance().findById(id);
    bound_remote_=info && info->remote;
    equipment_id_=id; name_=name; return true;
}
void EquipmentSession::unbind() {
    if(equipment_id_) SimDataBroker::instance().unsubscribe(equipment_id_);
    radar_view_.reset();sar_view_.reset();ir_view_.reset();vis_view_.reset();
    equipment_id_=0; binding_generation_=0; name_.clear(); frames_=0; last_frame_id_=0; last_sim_time_=0;
    has_frame_=false; connected_=false; active_=false; paused_=false; powered_=false;
    bound_remote_=false; expected_surface_.reset(); waiting_surface_=false;
    radar_on_gpu_=false; radar_gpu_handle_={}; radar_={}; ir_={}; sar_={}; vis_={};
    frame_timer_.invalidate(); control_timer_.invalidate(); power_request_.reset();
    control_status_.clear(); last_stage_=255;
}
double EquipmentSession::frameAgeSeconds() const { return frame_timer_.isValid() ? frame_timer_.elapsed()*.001 : -1.; }
bool EquipmentSession::stale() const {
    return has_frame_ && (!connected_ || (!paused_ && powered_ && frameAgeSeconds()>stale_after_s_));
}
QString EquipmentSession::statusText() const {
    if(!bound()) return QStringLiteral("未绑定");
    if(!connected_) return QStringLiteral("链路离线 · 保留最后帧");
    if(power_request_) return QStringLiteral("等待仿真端电源回报");
    if(!powered_) return QStringLiteral("设备已关机");
    if(waiting_surface_) return QStringLiteral("等待同版本地表与测量");
    if(paused_) return QStringLiteral("仿真已暂停 · 保留最后帧");
    if(!has_frame_) return QStringLiteral("已连接 · 等待测量数据");
    if(stale()) return QStringLiteral("数据超时 · %1 s 未更新").arg(frameAgeSeconds(),0,'f',1);
    return QStringLiteral("实时数据 · 帧龄 %1 s").arg(frameAgeSeconds(),0,'f',2);
}
void EquipmentSession::noteFrame(uint64_t id,double time,uint8_t stage) {
    if(!has_frame_ || id!=last_frame_id_ || time!=last_sim_time_ || stage!=last_stage_) {
        last_frame_id_=id; last_sim_time_=time; last_stage_=stage; ++frames_; frame_timer_.restart();
    }
    has_frame_=true;
}
void EquipmentSession::tick(std::optional<uint64_t> surface,bool surfaceReady,bool localPaused) {
    if(!bound()) return;
    auto& broker=SimDataBroker::instance();
    const auto info=EquipmentRegistry::instance().findById(equipment_id_);
    const bool remote=bound_remote_;
    connected_=info && (remote ? BrokerNet::instance().simOnline()&&BrokerNet::instance().leaseActive(equipment_id_) : broker.isServerOnline());
    paused_=connected_ && (remote ? !BrokerNet::instance().lastSimMeta().running : localPaused);
    if(info && std::isfinite(info->frame_rate) && info->frame_rate>0)
        stale_after_s_=std::max(3.,4./info->frame_rate);
    if(power_request_) {
        // Remote broker optimistically mirrors submitted controls. Only a newer
        // simulation heartbeat can confirm its actual state.
        if(info && ((!remote) || info->last_seen_ms>request_seen_ms_)) {
            powered_=info->powered;
            if(powered_==*power_request_) { power_request_.reset(); control_status_=QStringLiteral("仿真端已回报电源状态"); }
        }
        if(power_request_ && control_timer_.elapsed()>5000) {
            power_request_.reset(); control_status_=QStringLiteral("控制请求未确认，请检查链路后重试");
        }
    } else if(info) powered_=info->powered;
    else powered_=broker.isEquipmentPowered(equipment_id_);
    active_=false;
    if(expected_surface_!=surface || (!surfaceReady && has_frame_)) {
        ++binding_generation_; // clear the panel's cached image on the same refresh
        has_frame_=false;
        expected_surface_=surface;
    }
    waiting_surface_=!surfaceReady;
    if(!connected_ || !powered_) return;
    if(!surfaceReady) return;
    auto accepts=[&](uint64_t actual) {
        if(!surface || actual==*surface) return true;
        waiting_surface_=true;
        if(has_frame_) ++binding_generation_;
        has_frame_=false;
        radar_on_gpu_=false;
        radar_gpu_handle_={};
        return false;
    };
    if(remote){
        auto accept=[&](const auto& frame){if(frame&&!frame->empty()&&accepts(frame->surface_revision)){noteFrame(frame->frame_id,frame->sim_time,frame->stage);active_=true;return true;}return false;};
        if(spec_.sensor_type==SensorType::RADAR){auto frame=broker.radarSnapshot(equipment_id_);if(accept(frame)){radar_view_=std::move(frame);radar_on_gpu_=false;return;}}
        if(spec_.sensor_type==SensorType::IR){auto frame=broker.irSnapshot(equipment_id_);if(accept(frame)){ir_view_=std::move(frame);return;}}
        if(spec_.sensor_type==SensorType::VIS){auto frame=broker.visSnapshot(equipment_id_);if(accept(frame)){vis_view_=std::move(frame);return;}}
        if(spec_.sensor_type==SensorType::SAR){auto frame=broker.sarSnapshot(equipment_id_);if(accept(frame)){sar_view_=std::move(frame);return;}}
    }
    switch(spec_.sensor_type) {
    case SensorType::RADAR: {
        RadarFrame frame; bool got=broker.latestRadarMeta(equipment_id_,&frame);
        radar_on_gpu_=false; radar_gpu_handle_={};
        if(got && !frame.empty()) {
            auto handle=broker.channelHandle(equipment_id_);
            const auto bytes=size_t(frame.range_bins)*frame.doppler_bins*sizeof(std::complex<float>);
            if(handle.device_ptr && handle.size_bytes>=bytes) { radar_gpu_handle_=handle; radar_on_gpu_=true; }
            else got=broker.latestRadar(equipment_id_,&frame);
        }
        if(got && !frame.empty() && accepts(frame.surface_revision)) { noteFrame(frame.frame_id,frame.sim_time,frame.stage); radar_=std::move(frame); }
        break;
    }
    case SensorType::IR: {
        IrFrame frame; if(broker.latestIr(equipment_id_,&frame) && !frame.empty() && accepts(frame.surface_revision)) { noteFrame(frame.frame_id,frame.sim_time,frame.stage); ir_=std::move(frame); } break;
    }
    case SensorType::SAR: {
        SarFrame frame; if(broker.latestSar(equipment_id_,&frame) && !frame.empty() && accepts(frame.surface_revision)) { noteFrame(frame.frame_id,frame.sim_time,frame.stage); sar_=std::move(frame); } break;
    }
    case SensorType::VIS: {
        VisFrame frame; if(broker.latestVis(equipment_id_,&frame) && !frame.empty() && accepts(frame.surface_revision)) { noteFrame(frame.frame_id,frame.sim_time,frame.stage); vis_=std::move(frame); } break;
    }
    case SensorType::LASER: break;
    }
    active_=has_frame_ && !paused_ && !stale();
}
bool EquipmentSession::setPowered(bool on) {
    if(!bound() || !connected_ || power_request_) { control_status_=QStringLiteral("设备未连接或请求仍待确认"); return false; }
    ControlCommand command; command.type=on ? ControlCommand::Type::PowerOn : ControlCommand::Type::PowerOff;
    command.equipment_type=spec_.sensor_type; command.equipment_id=equipment_id_;
    const auto info=EquipmentRegistry::instance().findById(equipment_id_);
    request_seen_ms_=info ? info->last_seen_ms : 0;
    power_request_=on; control_timer_.restart(); control_status_=QStringLiteral("已发送电源请求，等待仿真端回报");
    BrokerNet::instance().sendControl(command);
    return true;
}
bool EquipmentSession::setRadarMode(int) {
    control_status_=QStringLiteral("仿真后端尚未提供雷达模式执行接口"); return false;
}
}}}
