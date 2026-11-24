#include "vsgraysim/broker/EquipmentDataSource.h"

#include <cmath>

#include <spdlog/spdlog.h>

#include "vsgraysim/Simulation.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include "vsgraysim/broker/FrameRecorder.h"

namespace vsgraysim {

namespace {
/// RADAR/SAR/IR -> 录制器槽位 (其他传感器类型不录制)
FrameRecorder* recorderForChannel(SensorType t, FrameRecorder* const (&recorders)[3])
{
    switch (t) {
    case SensorType::RADAR: return recorders[0];
    case SensorType::SAR:   return recorders[1];
    case SensorType::IR:    return recorders[2];
    default:                return nullptr;
    }
}

/// GPU 驻留帧 (GpuFormedFrame + 元数据帧) -> 类型化元数据帧 (不含载荷)
bool buildDeviceMeta(const Simulation::GpuFormedFrame& gf, const SignalFrame& meta,
                     RadarFrame* rf, SarFrame* sf, IrFrame* irf, VisFrame* vf)
{
    const uint64_t ts_ns = uint64_t(std::llround(meta.sim_time * 1.0e9));
    switch (gf.type) {
    case SensorType::RADAR:
        rf->frame_id = meta.frame_id;
        rf->surface_revision=meta.surface_revision;
        rf->sim_time = meta.sim_time;
        rf->timestamp_ns = ts_ns;
        rf->sensor_id = meta.sensor_id;
        rf->carrier_hz = float(meta.carrier_hz);
        rf->prf = meta.prf;
        rf->sample_rate_hz = meta.fs;
        rf->coherent_pulses = meta.coherent_pulses;
        rf->range_min = meta.range_min;
        rf->range_max = meta.range_max;
        rf->range_bins = gf.width;
        rf->doppler_bins = gf.height;
        rf->stage=meta.product_stage;
        return true;
    case SensorType::SAR:
        sf->frame_id = meta.frame_id;
        sf->surface_revision=meta.surface_revision;
        sf->sim_time = meta.sim_time;
        sf->timestamp_ns = ts_ns;
        sf->sensor_id = meta.sensor_id;
        sf->carrier_hz = float(meta.carrier_hz);
        sf->prf = meta.prf;
        sf->range_samples = gf.width;
        sf->azimuth_samples = gf.height;
        sf->stage=meta.product_stage; sf->sample_rate_hz=meta.fs; sf->range_min=meta.range_min;
        return true;
    case SensorType::IR:
        irf->frame_id = meta.frame_id;
        irf->surface_revision=meta.surface_revision;
        irf->sim_time = meta.sim_time;
        irf->timestamp_ns = ts_ns;
        irf->sensor_id = meta.sensor_id;
        irf->width = gf.width;
        irf->height = gf.height;
        irf->stage=meta.product_stage;
        return true;
    case SensorType::VIS:
    case SensorType::LASER:
        vf->frame_id = meta.frame_id;
        vf->surface_revision=meta.surface_revision;
        vf->sim_time = meta.sim_time;
        vf->timestamp_ns = ts_ns;
        vf->sensor_id = meta.sensor_id;
        vf->width = gf.width;
        vf->height = gf.height;
        vf->stage=meta.product_stage;
        return true;
    }
    return false;
}
} // anonymous namespace

void EquipmentDataSource::attachFrameRecorders(FrameRecorder* radar,
                                               FrameRecorder* sar,
                                               FrameRecorder* ir)
{
    recorders_[0] = radar;
    recorders_[1] = sar;
    recorders_[2] = ir;
}

void EquipmentDataSource::detachFrameRecorders()
{
    recorders_[0] = recorders_[1] = recorders_[2] = nullptr;
}

bool EquipmentDataSource::initialize(const nlohmann::json&)
{
    if (!sim_) {
        spdlog::error("EquipmentDataSource: null Simulation backend");
        return false;
    }
    return true;
}

void EquipmentDataSource::onPreTick(const TimeTick&)
{
    // nothing to prepare
}

void EquipmentDataSource::onTick(const TimeTick& tick)
{
    if (!sim_) return;

    // 1) 按 TimingBus 时序推进一帧仿真 (场景演化 + RT 散射 + 信号形成)
    last_tick_ok_ = sim_->step(tick.dt, tick.sim_time);
    if (!last_tick_ok_) return;

    // 2) 装备数据经 broker 分发:
    //    GPU 驻留帧 -> D2D 拷贝进通道 GpuBuffer (帧本体不落主机),
    //    显控经 GpuBufferHandle 在 GPU 上直接处理;
    //    需要主机数据的消费方 (跨进程转发/录制) 存在时按需下载帧本体。
    SimDataBroker& broker = SimDataBroker::instance();
    const auto& fresh = sim_->freshFrameSensors();
    const auto& gpu_frames = sim_->gpuFrames();
    const bool need_host_frames = broker.hasFrameTaps();   // BrokerNet 转发需主机数据
    for(const auto& frame:sim_->completedFrames()) {
        const bool accepted=broker.publish(frame);
        auto* recorder=recorderForChannel(frame.sensor_type,recorders_);
        if(accepted && recorder) recorder->recordFrame(frame);
    }
    for (const auto& [sid, frame] : sim_->lastFrames()) {
        // 本帧未产新帧 (CPI 窗口预热/帧率节流空窗/门控外) 则跳过:
        // broker 通道保留上一帧元数据, 显控维持上一帧, 不重发旧帧。
        if (fresh.count(sid) == 0) continue;

        auto git = gpu_frames.find(sid);
        if(git==gpu_frames.end()) continue; // CPU measurement queue was delivered above
        FrameRecorder* rec = recorderForChannel(frame.sensor_type, recorders_);
        const bool need_host = need_host_frames ||
            (rec && broker.isEquipmentPowered(sid));

        if (git != gpu_frames.end() && !need_host) {
            // 零拷贝发布: 帧本体驻留显存, 仅元数据过主机。
            RadarFrame rf; SarFrame sf; IrFrame irf; VisFrame vf;
            if (buildDeviceMeta(git->second, frame, &rf, &sf, &irf, &vf)) {
                switch (git->second.type) {
                case SensorType::RADAR:
                    broker.publishRadarDevice(rf, git->second.dev_ptr, git->second.bytes);
                    break;
                case SensorType::SAR:
                    broker.publishSarDevice(sf, git->second.dev_ptr, git->second.bytes);
                    break;
                case SensorType::IR:
                    broker.publishIrDevice(irf, git->second.dev_ptr, git->second.bytes);
                    break;
                default:
                    broker.publishVisDevice(vf, git->second.dev_ptr, git->second.bytes);
                    break;
                }
            }
            continue;
        }

        // 主机数据路径: CPU 成帧直接可用; GPU 帧且需主机数据时下载帧本体 (小图/小谱)
        SignalFrame host = frame;
        if (git != gpu_frames.end() && host.samples.empty())
            sim_->downloadFrameSamples(sid, host.samples);
        if (!host.samples.empty()) {
            const bool accepted=broker.publish(host);
            // 数据分发处录制: 装备关机时 publish 已丢弃, 录制同步跳过
            if (accepted && rec)
                rec->recordFrame(host);
        }
    }
}

void EquipmentDataSource::onPostTick(const TimeTick&)
{
    // 3) 闭环反馈: 取走显控软件的控制命令 (电源状态已在 broker 立即生效,
    //    这里消费队列用于服务端事件记录)
    SimDataBroker& broker = SimDataBroker::instance();
    ControlCommand cmd;
    while (broker.pollControl(&cmd)) {
        spdlog::info("EquipmentDataSource: equipment [{}] {} by display-console",
                     sensorTypeName(cmd.equipment_type),
                     cmd.type == ControlCommand::Type::PowerOn
                         ? "POWER ON" : "POWER OFF");
    }
}

void EquipmentDataSource::onStop()
{
    SimDataBroker::instance().serverOnline(false);
}

} // namespace vsgraysim
