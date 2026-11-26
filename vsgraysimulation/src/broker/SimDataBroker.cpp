/// @file SimDataBroker.cpp
/// @brief 仿真数据总线实现。

#include "vsgraysim/broker/SimDataBroker.h"
#include "vsgraysim/layer3b_signal/SignalFrame.h"

#include <chrono>
#include <cmath>
#include <functional>

#include <spdlog/spdlog.h>

namespace vsgraysim {

namespace {
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // anonymous namespace

SimDataBroker& SimDataBroker::instance()
{
    static SimDataBroker broker;
    return broker;
}

// =============================================================================
// 装备发现
// =============================================================================

std::vector<EquipmentInfo> SimDataBroker::scanOnlineEquipment() const
{
    return EquipmentRegistry::instance().listOnline();
}

std::vector<EquipmentInfo> SimDataBroker::scanByType(SensorType t) const
{
    return EquipmentRegistry::instance().listByType(t);
}

uint32_t SimDataBroker::firstEquipmentOfType(SensorType t) const
{
    const auto list = EquipmentRegistry::instance().listByType(t);
    return list.empty() ? 0u : list.front().equipment_id;
}

// =============================================================================
// 订阅管理
// =============================================================================

bool SimDataBroker::subscribe(uint32_t equipment_id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto eq = EquipmentRegistry::instance().findById(equipment_id);
    if (!eq) {
        spdlog::warn("SimDataBroker: subscribe failed, equipment {} not found",
                     equipment_id);
        return false;
    }
    subscriptions_.insert(equipment_id);
    spdlog::info("SimDataBroker: subscribed to equipment {} '{}'",
                 equipment_id, eq->name);
    return true;
}

void SimDataBroker::unsubscribe(uint32_t equipment_id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    subscriptions_.erase(equipment_id);
    spdlog::info("SimDataBroker: unsubscribed from equipment {}", equipment_id);
}

bool SimDataBroker::isSubscribed(uint32_t equipment_id) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return subscriptions_.count(equipment_id) > 0;
}

std::vector<uint32_t> SimDataBroker::getSubscribedList() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return std::vector<uint32_t>(subscriptions_.begin(), subscriptions_.end());
}

// =============================================================================
// 服务端
// =============================================================================

void SimDataBroker::serverOnline(bool online)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (server_online_ == online) return;
    server_online_ = online;
    spdlog::info("SimDataBroker: simulation data service {}",
                 online ? "ONLINE" : "OFFLINE");
    if (online) {
        // 服务重启后装备默认开机
        for (auto& [id, ch] : channels_) {
            ch.stats.powered = true;
        }
    }
}

void SimDataBroker::resetEquipmentChannel(uint32_t id){std::lock_guard<std::mutex> lock(mtx_);channels_.erase(id);subscriptions_.erase(id);}

bool SimDataBroker::isServerOnline() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return server_online_;
}

SimDataBroker::EquipmentChannel& SimDataBroker::getChannel(uint32_t equipment_id)
{
    auto it = channels_.find(equipment_id);
    if (it == channels_.end()) {
        // 自动创建通道 (原地构造: GpuBuffer 不可移动/拷贝)
        auto eq = EquipmentRegistry::instance().findById(equipment_id);
        auto [new_it, inserted] = channels_.try_emplace(equipment_id);
        if (eq) new_it->second.type = eq->type;
        return new_it->second;
    }
    return it->second;
}

void SimDataBroker::enableHostFrameCache(bool enabled){std::lock_guard lock(mtx_);host_cache_=enabled;if(!enabled)for(auto& [id,c]:channels_){c.radar_host.reset();c.sar_host.reset();c.ir_host.reset();c.vis_host.reset();}}
std::shared_ptr<const RadarFrame> SimDataBroker::radarSnapshot(uint32_t id) const {std::lock_guard lock(mtx_);auto it=channels_.find(id);return it==channels_.end()?nullptr:it->second.radar_host;}
std::shared_ptr<const SarFrame> SimDataBroker::sarSnapshot(uint32_t id) const {std::lock_guard lock(mtx_);auto it=channels_.find(id);return it==channels_.end()?nullptr:it->second.sar_host;}
std::shared_ptr<const IrFrame> SimDataBroker::irSnapshot(uint32_t id) const {std::lock_guard lock(mtx_);auto it=channels_.find(id);return it==channels_.end()?nullptr:it->second.ir_host;}
std::shared_ptr<const VisFrame> SimDataBroker::visSnapshot(uint32_t id) const {std::lock_guard lock(mtx_);auto it=channels_.find(id);return it==channels_.end()?nullptr:it->second.vis_host;}

std::shared_ptr<const SonarFrame> SimDataBroker::sonarSnapshot(uint32_t id) const {
    std::lock_guard lock(mtx_);
    const auto it=channels_.find(id);
    return it==channels_.end()?nullptr:it->second.sonar_host;
}

void SimDataBroker::setSonarTap(std::function<void(const SonarFrame&)> tap) {
    std::lock_guard lock(mtx_);
    sonar_tap_=std::move(tap);
}

bool SimDataBroker::publishSonar(const SonarFrame& frame) {
    if(!frame.valid())return false;
    std::function<void(const SonarFrame&)> tap;
    {
        std::lock_guard lock(mtx_);
        if(!server_online_)return false;
        const auto info=EquipmentRegistry::instance().findById(frame.sensor_id);
        if(!info||info->type!=SensorType::SONAR||info->platform_id!=frame.platform_id)return false;
        auto& channel=getChannel(frame.sensor_id);
        if(!channel.stats.powered||!info->powered||
           (channel.sonar_host&&channel.sonar_host->frame_id>=frame.frame_id))return false;
        channel.sonar_host=std::make_shared<const SonarFrame>(frame);
        channel.stats.last_frame_id=frame.frame_id;
        ++channel.stats.published_frames;
        tap=sonar_tap_;
    }
    if(tap)tap(frame);
    return true;
}

bool SimDataBroker::publishRadar(const RadarFrame& frame)
{
    if (frame.empty() || frame.rd_map.size() != size_t(frame.range_bins) * frame.doppler_bins) return false;
    if(frame.receiver&&(!frame.receiver->valid()||frame.receiver->first_transmit_s>frame.sim_time))return false;
    if(frame.ground&&(frame.ground->size()>RadarFrame::maxGroundSamples||
       std::any_of(frame.ground->begin(),frame.ground->end(),[](const auto& point){return !point.valid();})))return false;

    std::function<void(const RadarFrame&)> tap;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!server_online_) return false;

        EquipmentChannel& ch = getChannel(frame.sensor_id);
        if (!ch.stats.powered) return false;  // 装备关机: 停发数据

        // 上传 GPU
        const size_t bytes = frame.rd_map.size() * sizeof(frame.rd_map[0]);
        if (!host_cache_ && !ch.gpu.upload(frame.rd_map.data(), bytes)) {
            spdlog::warn("SimDataBroker: radar upload failed for equipment {}",
                         frame.sensor_id);
            return false;
        }

        // 保存元数据 (不含数据本体)
        if(host_cache_)ch.radar_host=std::make_shared<const RadarFrame>(frame);
        ch.radar_meta = frame;
        ch.radar_meta.rd_map.clear();
        ch.frame_id = frame.frame_id;
        ch.stats.published_frames++;
        ch.stats.last_frame_id = frame.frame_id;
        ch.stats.data_on_gpu = ch.gpu.onGpu();
        tap = radar_tap_;
    }
    if (tap) tap(frame);   // invoke outside the channel lock
    return true;
}

bool SimDataBroker::publishSar(const SarFrame& frame)
{
    if (frame.empty() || frame.echo_data.size() != size_t(frame.range_samples) * frame.azimuth_samples ||
        (frame.isar_axes&&!frame.hasFocusedIsar())) return false;

    std::function<void(const SarFrame&)> tap;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!server_online_) return false;

        EquipmentChannel& ch = getChannel(frame.sensor_id);
        if (!ch.stats.powered) return false;

        const size_t bytes = frame.echo_data.size() * sizeof(frame.echo_data[0]);
        if (!host_cache_ && !ch.gpu.upload(frame.echo_data.data(), bytes)) {
            spdlog::warn("SimDataBroker: sar upload failed for equipment {}",
                         frame.sensor_id);
            return false;
        }

        if(host_cache_)ch.sar_host=std::make_shared<const SarFrame>(frame);
        ch.sar_meta = frame;
        ch.sar_meta.echo_data.clear();
        ch.frame_id = frame.frame_id;
        ch.stats.published_frames++;
        ch.stats.last_frame_id = frame.frame_id;
        ch.stats.data_on_gpu = ch.gpu.onGpu();
        tap = sar_tap_;
    }
    if (tap) tap(frame);
    return true;
}

bool SimDataBroker::publishIr(const IrFrame& frame)
{
    if (frame.empty() || frame.radiance.size() != size_t(frame.width) * frame.height) return false;

    std::function<void(const IrFrame&)> tap;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!server_online_) return false;

        EquipmentChannel& ch = getChannel(frame.sensor_id);
        if (!ch.stats.powered) return false;

        const size_t bytes = frame.radiance.size() * sizeof(frame.radiance[0]);
        if (!host_cache_ && !ch.gpu.upload(frame.radiance.data(), bytes)) {
            spdlog::warn("SimDataBroker: ir upload failed for equipment {}",
                         frame.sensor_id);
            return false;
        }

        if(host_cache_)ch.ir_host=std::make_shared<const IrFrame>(frame);
        ch.ir_meta = frame;
        ch.ir_meta.radiance.clear();
        ch.ir_meta.temperature.clear();
        ch.frame_id = frame.frame_id;
        ch.stats.published_frames++;
        ch.stats.last_frame_id = frame.frame_id;
        ch.stats.data_on_gpu = ch.gpu.onGpu();
        tap = ir_tap_;
    }
    if (tap) tap(frame);
    return true;
}

bool SimDataBroker::publishVis(const VisFrame& frame)
{
    if (frame.empty() || frame.intensity.size() != size_t(frame.width) * frame.height) return false;

    std::function<void(const VisFrame&)> tap;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!server_online_) return false;

        EquipmentChannel& ch = getChannel(frame.sensor_id);
        if (!ch.stats.powered) return false;

        const size_t bytes = frame.intensity.size() * sizeof(frame.intensity[0]);
        if (!host_cache_ && !ch.gpu.upload(frame.intensity.data(), bytes)) {
            spdlog::warn("SimDataBroker: vis upload failed for equipment {}",
                         frame.sensor_id);
            return false;
        }

        if(host_cache_)ch.vis_host=std::make_shared<const VisFrame>(frame);
        ch.vis_meta = frame;
        ch.vis_meta.intensity.clear();
        ch.frame_id = frame.frame_id;
        ch.stats.published_frames++;
        ch.stats.last_frame_id = frame.frame_id;
        ch.stats.data_on_gpu = ch.gpu.onGpu();
        tap = vis_tap_;
    }
    if (tap) tap(frame);
    return true;
}

// =============================================================================
// GPU 驻留帧发布 (D2D 拷贝: 帧本体不落主机, 不触发跨进程 tap)
// =============================================================================

void SimDataBroker::publishRadarDevice(const RadarFrame& meta,
                                         const void* dev_ptr, std::size_t bytes)
{
    if (meta.empty() || !dev_ptr || bytes == 0) return;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return;

    EquipmentChannel& ch = getChannel(meta.sensor_id);ch.radar_host.reset();
    if (!ch.stats.powered) return;  // 装备关机: 停发数据

    if (!ch.gpu.uploadDevice(dev_ptr, bytes)) {
        spdlog::warn("SimDataBroker: radar device publish failed for equipment {}",
                     meta.sensor_id);
        return;
    }

    ch.radar_meta = meta;
    ch.radar_meta.rd_map.clear();
    ch.frame_id = meta.frame_id;
    ch.stats.published_frames++;
    ch.stats.last_frame_id = meta.frame_id;
    ch.stats.data_on_gpu = ch.gpu.onGpu();
}

void SimDataBroker::publishSarDevice(const SarFrame& meta,
                                     const void* dev_ptr, std::size_t bytes)
{
    if (meta.empty() || !dev_ptr || bytes == 0) return;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return;

    EquipmentChannel& ch = getChannel(meta.sensor_id);ch.sar_host.reset();
    if (!ch.stats.powered) return;

    if (!ch.gpu.uploadDevice(dev_ptr, bytes)) {
        spdlog::warn("SimDataBroker: sar device publish failed for equipment {}",
                     meta.sensor_id);
        return;
    }

    ch.sar_meta = meta;
    ch.sar_meta.echo_data.clear();
    ch.frame_id = meta.frame_id;
    ch.stats.published_frames++;
    ch.stats.last_frame_id = meta.frame_id;
    ch.stats.data_on_gpu = ch.gpu.onGpu();
}

void SimDataBroker::publishIrDevice(const IrFrame& meta,
                                    const void* dev_ptr, std::size_t bytes)
{
    if (meta.empty() || !dev_ptr || bytes == 0) return;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return;

    EquipmentChannel& ch = getChannel(meta.sensor_id);ch.ir_host.reset();
    if (!ch.stats.powered) return;

    if (!ch.gpu.uploadDevice(dev_ptr, bytes)) {
        spdlog::warn("SimDataBroker: ir device publish failed for equipment {}",
                     meta.sensor_id);
        return;
    }

    ch.ir_meta = meta;
    ch.ir_meta.radiance.clear();
    ch.ir_meta.temperature.clear();
    ch.frame_id = meta.frame_id;
    ch.stats.published_frames++;
    ch.stats.last_frame_id = meta.frame_id;
    ch.stats.data_on_gpu = ch.gpu.onGpu();
}

void SimDataBroker::publishVisDevice(const VisFrame& meta,
                                     const void* dev_ptr, std::size_t bytes)
{
    if (meta.empty() || !dev_ptr || bytes == 0) return;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return;

    EquipmentChannel& ch = getChannel(meta.sensor_id);ch.vis_host.reset();
    if (!ch.stats.powered) return;

    if (!ch.gpu.uploadDevice(dev_ptr, bytes)) {
        spdlog::warn("SimDataBroker: vis device publish failed for equipment {}",
                     meta.sensor_id);
        return;
    }

    ch.vis_meta = meta;
    ch.vis_meta.intensity.clear();
    ch.frame_id = meta.frame_id;
    ch.stats.published_frames++;
    ch.stats.last_frame_id = meta.frame_id;
    ch.stats.data_on_gpu = ch.gpu.onGpu();
}

bool SimDataBroker::hasFrameTaps() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return bool(radar_tap_ || sar_tap_ || ir_tap_ || vis_tap_);
}

bool SimDataBroker::pollControl(ControlCommand* out)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (control_queue_.empty() || !out) return false;
    *out = control_queue_.front();
    control_queue_.pop_front();
    return true;
}

ChannelStats SimDataBroker::channelStats(uint32_t equipment_id) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = channels_.find(equipment_id);
    if (it != channels_.end()) {
        return it->second.stats;
    }
    return ChannelStats{};
}

GpuBufferHandle SimDataBroker::channelHandle(uint32_t equipment_id) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = channels_.find(equipment_id);
    if (it != channels_.end()) {
        return it->second.gpu.handle();
    }
    return GpuBufferHandle{};
}

// =============================================================================
// 客户端
// =============================================================================

bool SimDataBroker::latestRadar(uint32_t equipment_id, RadarFrame* out)
{
    if (!out) return false;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return false;

    auto it = channels_.find(equipment_id);
    if (it == channels_.end() || it->second.stats.published_frames == 0) return false;

    EquipmentChannel& ch = it->second;
    if(ch.radar_host){*out=*ch.radar_host;ch.stats.last_client_poll_ms=nowMs();return true;}
    const auto& meta = ch.radar_meta;
    if (meta.empty()) return false;

    RadarFrame res = meta;
    res.resize(meta.range_bins, meta.doppler_bins);
    const size_t bytes = res.rd_map.size() * sizeof(res.rd_map[0]);
    if (!ch.gpu.download(res.rd_map.data(), bytes)) return false;

    *out = std::move(res);
    ch.stats.last_client_poll_ms = nowMs();
    return true;
}

bool SimDataBroker::latestRadarMeta(uint32_t equipment_id, RadarFrame* out)
{
    if (!out) return false;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return false;

    auto it = channels_.find(equipment_id);
    if (it == channels_.end() || it->second.stats.published_frames == 0) return false;

    EquipmentChannel& ch = it->second;
    const auto& meta = ch.radar_meta;
    if (meta.empty()) return false;

    *out = meta;   // 数据本体驻留通道 GPU 缓冲, 经 channelHandle() 消费
    ch.stats.last_client_poll_ms = nowMs();
    return true;
}

bool SimDataBroker::latestSar(uint32_t equipment_id, SarFrame* out)
{
    if (!out) return false;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return false;

    auto it = channels_.find(equipment_id);
    if (it == channels_.end() || it->second.stats.published_frames == 0) return false;

    EquipmentChannel& ch = it->second;
    if(ch.sar_host){*out=*ch.sar_host;ch.stats.last_client_poll_ms=nowMs();return true;}
    const auto& meta = ch.sar_meta;
    if (meta.empty()) return false;

    SarFrame res = meta;
    res.resize(meta.range_samples, meta.azimuth_samples);
    const size_t bytes = res.echo_data.size() * sizeof(res.echo_data[0]);
    if (!ch.gpu.download(res.echo_data.data(), bytes)) return false;

    *out = std::move(res);
    ch.stats.last_client_poll_ms = nowMs();
    return true;
}

bool SimDataBroker::latestIr(uint32_t equipment_id, IrFrame* out)
{
    if (!out) return false;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return false;

    auto it = channels_.find(equipment_id);
    if (it == channels_.end() || it->second.stats.published_frames == 0) return false;

    EquipmentChannel& ch = it->second;
    if(ch.ir_host){*out=*ch.ir_host;ch.stats.last_client_poll_ms=nowMs();return true;}
    const auto& meta = ch.ir_meta;
    if (meta.empty()) return false;

    IrFrame res = meta;
    res.resize(meta.width, meta.height);
    const size_t bytes = res.radiance.size() * sizeof(res.radiance[0]);
    if (!ch.gpu.download(res.radiance.data(), bytes)) return false;

    *out = std::move(res);
    ch.stats.last_client_poll_ms = nowMs();
    return true;
}

bool SimDataBroker::latestVis(uint32_t equipment_id, VisFrame* out)
{
    if (!out) return false;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!server_online_) return false;

    auto it = channels_.find(equipment_id);
    if (it == channels_.end() || it->second.stats.published_frames == 0) return false;

    EquipmentChannel& ch = it->second;
    if(ch.vis_host){*out=*ch.vis_host;ch.stats.last_client_poll_ms=nowMs();return true;}
    const auto& meta = ch.vis_meta;
    if (meta.empty()) return false;

    VisFrame res = meta;
    res.resize(meta.width, meta.height);
    const size_t bytes = res.intensity.size() * sizeof(res.intensity[0]);
    if (!ch.gpu.download(res.intensity.data(), bytes)) return false;

    *out = std::move(res);
    ch.stats.last_client_poll_ms = nowMs();
    return true;
}

void SimDataBroker::sendControl(const ControlCommand& cmd)
{
    std::lock_guard<std::mutex> lock(mtx_);
    ControlCommand c = cmd;
    c.seq = ++control_seq_;
    control_queue_.push_back(c);

    // 电源状态立即生效
    auto it = channels_.find(cmd.equipment_id);
    if (it != channels_.end() && (c.type==ControlCommand::Type::PowerOn || c.type==ControlCommand::Type::PowerOff)) {
        it->second.stats.powered = (c.type == ControlCommand::Type::PowerOn);
    }
    if(c.type==ControlCommand::Type::PowerOn || c.type==ControlCommand::Type::PowerOff)
        EquipmentRegistry::instance().setPowered(cmd.equipment_id,c.type==ControlCommand::Type::PowerOn);

    spdlog::info("SimDataBroker: control #{} equipment={} -> {}",
                 c.seq, cmd.equipment_id,
                 c.type == ControlCommand::Type::PowerOn ? "POWER ON" : c.type==ControlCommand::Type::PowerOff ? "POWER OFF" : "UNSUPPORTED PARAMETER COMMAND");
}

bool SimDataBroker::isEquipmentPowered(uint32_t equipment_id) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = channels_.find(equipment_id);
    if (it != channels_.end()) {
        return it->second.stats.powered;
    }
    return false;
}

// =============================================================================
// 交战闭环 (制导雷达 -> 仿真控制软件 -> 红方地空导弹)
// =============================================================================

void SimDataBroker::sendEngagement(const EngagementRequest& req)
{
    std::lock_guard<std::mutex> lock(mtx_);
    EngagementRequest r = req;
    r.request_seq = ++engagement_seq_;
    engagement_queue_.push_back(r);
    spdlog::info("SimDataBroker: engagement #{} fire={} gate={}m pos=({:.0f},{:.0f},{:.0f})",
                 r.request_seq, r.fire, r.gate_m, r.target_x, r.target_y, r.target_z);
}

bool SimDataBroker::pollEngagement(EngagementRequest* out)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (engagement_queue_.empty() || !out) return false;
    *out = engagement_queue_.front();
    engagement_queue_.pop_front();
    return true;
}

void SimDataBroker::publishInterceptorStatus(const std::vector<InterceptorStatus>& statuses)
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        // 只保留最近一帧快照, 避免无限累积
        interceptor_queue_.clear();
        interceptor_queue_.push_back(statuses);
    }
    // 锁外触发外发 tap (BrokerNet 仿真侧跨进程转发)
    std::function<void(const std::vector<InterceptorStatus>&)> tap;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        tap = interceptor_tap_;
    }
    if (tap) tap(statuses);
}

bool SimDataBroker::pollInterceptorStatuses(std::vector<InterceptorStatus>* out)
{
    if (!out) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    if (interceptor_queue_.empty()) return false;
    *out = interceptor_queue_.front();
    interceptor_queue_.clear();
    return true;
}

// =============================================================================
// 向后兼容 (旧 SignalFrame 接口)
// =============================================================================

bool SimDataBroker::publish(const SignalFrame& frame)
{
    // 转换为新帧格式发布 (回放路径使用)
    // 时统字段: 仿真时刻换算成纳秒级时间戳, 供多显控跨装备对齐帧序。
    const uint64_t ts_ns = uint64_t(std::llround(double(frame.sim_time) * 1.0e9));
    if (frame.sensor_type == SensorType::RADAR) {
        RadarFrame rf;
        rf.frame_id = frame.frame_id;
        rf.surface_revision = frame.surface_revision;
        rf.sim_time = frame.sim_time;
        rf.timestamp_ns = ts_ns;
        rf.sensor_id = frame.sensor_id;
        rf.carrier_hz = float(frame.carrier_hz);
        rf.prf = frame.prf;
        rf.sample_rate_hz = frame.fs;
        rf.coherent_pulses = frame.coherent_pulses;
        rf.range_min = frame.range_min;
        rf.range_max = frame.range_max;
        rf.range_bins = frame.width;
        rf.doppler_bins = frame.height;
        rf.rd_map = frame.samples;
        rf.stage = frame.product_stage;
        return publishRadar(rf);
    } else if (frame.sensor_type == SensorType::SAR) {
        SarFrame sf;
        sf.frame_id = frame.frame_id;
        sf.surface_revision = frame.surface_revision;
        sf.sim_time = frame.sim_time;
        sf.timestamp_ns = ts_ns;
        sf.sensor_id = frame.sensor_id;
        sf.carrier_hz = float(frame.carrier_hz);
        sf.prf = frame.prf;
        sf.range_samples = frame.width;
        sf.azimuth_samples = frame.height;
        sf.echo_data = frame.samples;
        sf.stage = frame.product_stage;
        sf.sample_rate_hz = frame.fs; sf.range_min = frame.range_min;
        return publishSar(sf);
    } else if (frame.sensor_type == SensorType::IR) {
        IrFrame irf;
        irf.frame_id = frame.frame_id;
        irf.surface_revision = frame.surface_revision;
        irf.sim_time = frame.sim_time;
        irf.timestamp_ns = ts_ns;
        irf.sensor_id = frame.sensor_id;
        irf.width = frame.width;
        irf.height = frame.height;
        irf.stage = frame.product_stage;
        irf.radiance.resize(frame.samples.size());
        for (size_t i = 0; i < frame.samples.size(); ++i)
            irf.radiance[i] = frame.samples[i].real();
        return publishIr(irf);
    } else if (frame.sensor_type == SensorType::VIS) {
        VisFrame vf;
        vf.frame_id = frame.frame_id;
        vf.surface_revision = frame.surface_revision;
        vf.sim_time = frame.sim_time;
        vf.timestamp_ns = ts_ns;
        vf.sensor_id = frame.sensor_id;
        vf.width = frame.width;
        vf.height = frame.height;
        vf.stage = frame.product_stage;
        vf.intensity.resize(frame.samples.size());
        for (size_t i = 0; i < frame.samples.size(); ++i)
            vf.intensity[i] = frame.samples[i].real();
        return publishVis(vf);
    }
    return false;
}

bool SimDataBroker::latest(SensorType t, SignalFrame* out)
{
    // 简化实现: 只支持 RADAR
    if (t == SensorType::RADAR && out) {
        // 先取通道 ID 快照, 避免持锁时重入 latestRadar (std::mutex 不可重入)
        uint32_t radar_id = 0;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (auto& [id, ch] : channels_) {
                if (ch.type == SensorType::RADAR && !ch.radar_meta.empty()) {
                    radar_id = id;
                    break;
                }
            }
        }
        if (radar_id != 0) {
            RadarFrame rf;
            if (latestRadar(radar_id, &rf)) {
                out->width = rf.range_bins;
                out->height = rf.doppler_bins;
                out->samples = rf.rd_map;
                out->frame_id = rf.frame_id;
                out->surface_revision=rf.surface_revision;
                out->sim_time = rf.sim_time;
                out->sensor_id = rf.sensor_id;
                out->sensor_type = SensorType::RADAR;
                out->carrier_hz = rf.carrier_hz;
                out->prf = rf.prf;
                out->coherent_pulses = rf.coherent_pulses;
                out->product_stage = rf.stage; out->fs=rf.sample_rate_hz;
                return true;
            }
        }
    }
    return false;
}

} // namespace vsgraysim
