#pragma once
/// @file SimDataBroker.h
/// @brief 仿真数据总线: 仿真控制软件(数据源) <-> 各显控软件(数据处理) 之间的
///        装备数据发布/订阅 + 控制反馈通道。
///
/// 数据流 (正向, GPU 句柄传递):
///   仿真控制软件 (场景+时序+RT 数据生成)
///       -- publishRadar/Sar/Ir/Vis(): 装备数据上传 GPU 缓冲区
///       -- GPU 句柄 (cudaIpcMemHandle_t / 设备指针)
///       --> 显控软件 latestRadar/Sar/Ir/Vis(): 从 GPU 取数 -> 本地处理 -> 显示
///
/// 控制流 (反向, 闭环反馈):
///   显控软件 sendControl() (开关机等装备控制)
///       --> 仿真控制软件 pollControl() -> 改变数据生成行为 (关机即停发)
///
/// 装备发现:
///   显控软件启动时调用 scanOnlineEquipment() 扫描在线装备列表,
///   用户选择要链接的装备后调用 subscribe(equipment_id) 订阅数据。

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/broker/SensorFrames.h"
#include "vsgraysim/broker/EquipmentInfo.h"
#include "vsgraysim/broker/GpuBuffer.h"

namespace vsgraysim {

/// 显控 -> 仿真控制的装备控制命令 (闭环反馈)
struct ControlCommand {
    enum class Type : uint8_t {
        PowerOn,
        PowerOff,
        SetGain,
        SetThreshold,
        SetMode,
    };
    Type        type = Type::PowerOn;
    SensorType  equipment_type = SensorType::RADAR;
    uint32_t    equipment_id = 0;       // 目标装备 ID
    uint64_t    seq = 0;                // 命令序号
    float       param_f = 0.0f;         // 浮点参数 (增益/门限等)
    uint32_t    param_u = 0;            // 整数参数 (模式等)
};

/// 单个装备通道的发布状态 (供仿真控制软件 UI 显示)
struct ChannelStats {
    uint64_t published_frames = 0;      // 累计发布帧数
    uint64_t last_frame_id = 0;         // 最新帧号
    int64_t  last_client_poll_ms = -1;  // 显控最近取数时刻 (epoch ms, -1=从未)
    bool     powered = true;            // 装备电源状态
    bool     data_on_gpu = false;       // 数据是否驻留 GPU
};

/// 制导雷达/搜索雷达 发起的交战请求 (显控 -> 仿真控制软件 -> 红方地空导弹)
///
/// 由雷达显控在"制导模式"下对锁定目标触发发射时发送。
/// 仿真软件收到后: 在目标世界位置附近 (gate_m 范围内) 关联蓝方目标,
/// 指派一枚红方地空导弹解锁发射并连续喂给目标实时位置制导拦截。
struct EngagementRequest {
    bool        fire = false;          // true=请求发射拦截弹
    float       target_x = 0, target_y = 0, target_z = 0;  // 目标近似世界位置 (由航迹方位/距离换算)
    float       gate_m = 5000.0f;      // 目标关联门限 (在门限内匹配蓝方飞行目标)
    uint32_t    source_equipment_id = 0;  // 发起装备 (制导雷达) ID, 0=自动
    uint32_t    request_seq = 0;       // 请求序号
};

/// 拦截弹状态回传 (仿真控制软件 -> 显控, 制导雷达界面展示"为导弹提供信息")
struct InterceptorStatus {
    uint32_t   interceptor_id = 0;
    std::string name;
    uint32_t   target_id = 0;
    bool       launched = false;       // 已发射
    bool       in_flight = false;      // 飞行中 (制导信息持续下传中)
    bool       impacted = false;       // 已命中/结束
    bool       target_destroyed = false; // 目标被摧毁
    float      range_km = 0.0f;        // 到目标当前距离
    double     launch_time = 0.0;
    double     sim_time = 0.0;
};

class SimDataBroker {
public:
    static SimDataBroker& instance();

    // ==================== 装备发现 ====================

    /// 扫描在线装备列表 (显控软件启动时调用)
    std::vector<EquipmentInfo> scanOnlineEquipment() const;

    /// 按类型筛选装备
    std::vector<EquipmentInfo> scanByType(SensorType t) const;

    /// 返回某类型的第一个在线装备 ID (无则返回 0, 供监控面板兼容旧索引)
    uint32_t firstEquipmentOfType(SensorType t) const;

    // ==================== 订阅管理 ====================

    /// 显控软件订阅某装备数据
    bool subscribe(uint32_t equipment_id);

    /// 取消订阅
    void unsubscribe(uint32_t equipment_id);

    /// 查询订阅状态
    bool isSubscribed(uint32_t equipment_id) const;
    std::vector<uint32_t> getSubscribedList() const;

    // ==================== 服务端 (仿真控制软件) ====================

    /// 仿真数据服务上线/下线
    void serverOnline(bool online);
    /// Source lifecycle only: discard previous acquisition payload and subscription.
    void resetEquipmentChannel(uint32_t equipment_id);
    bool isServerOnline() const;

    /// 发布各类型帧数据 (上传 GPU + 更新元数据)
    /// Return true only after the complete payload has been accepted by the channel.
    bool publishRadar(const RadarFrame& frame);
    bool publishSar(const SarFrame& frame);
    bool publishIr(const IrFrame& frame);
    bool publishVis(const VisFrame& frame);

    /// 发布 GPU 驻留帧: 帧本体经 D2D 拷贝进通道 GPU 缓冲 (不落主机),
    /// 仅更新元数据。meta 的载荷 vector 忽略, 只取类型化元数据字段。
    /// 注意: 已设置数据面 tap (跨进程转发需主机数据) 时, 调用方应改走
    /// 主机数据 publish* 路径 (见 hasFrameTaps())。
    void publishRadarDevice(const RadarFrame& meta, const void* dev_ptr, std::size_t bytes);
    void publishSarDevice(const SarFrame& meta, const void* dev_ptr, std::size_t bytes);
    void publishIrDevice(const IrFrame& meta, const void* dev_ptr, std::size_t bytes);
    void publishVisDevice(const VisFrame& meta, const void* dev_ptr, std::size_t bytes);

    /// 数据面帧外发 tap (BrokerNet 跨进程转发) 是否已设置 (仿真侧)
    bool hasFrameTaps() const;

    /// 取走一条待处理的控制命令; 无命令返回 false
    bool pollControl(ControlCommand* out);

    /// 通道状态
    ChannelStats channelStats(uint32_t equipment_id) const;

    /// GPU 句柄信息
    GpuBufferHandle channelHandle(uint32_t equipment_id) const;

    // ==================== 客户端 (各显控软件) ====================

    /// 获取指定装备的最新帧
    bool latestRadar(uint32_t equipment_id, RadarFrame* out);
    bool latestSar(uint32_t equipment_id, SarFrame* out);
    bool latestIr(uint32_t equipment_id, IrFrame* out);
    bool latestVis(uint32_t equipment_id, VisFrame* out);
    // Display consumers retain immutable host payloads received over IPC. This
    // avoids GUI-thread GPU downloads and gives background jobs safe lifetimes.
    void enableHostFrameCache(bool enabled);
    std::shared_ptr<const RadarFrame> radarSnapshot(uint32_t) const;
    std::shared_ptr<const SarFrame> sarSnapshot(uint32_t) const;
    std::shared_ptr<const IrFrame> irSnapshot(uint32_t) const;
    std::shared_ptr<const VisFrame> visSnapshot(uint32_t) const;

    /// 仅获取最新雷达帧元数据 (不下载数据本体); 显控端配和
    /// channelHandle() 在 GPU 上直接处理帧数据 (零拷贝路径)。
    bool latestRadarMeta(uint32_t equipment_id, RadarFrame* out);

    /// 发送装备控制命令
    void sendControl(const ControlCommand& cmd);

    /// 查询装备电源状态
    bool isEquipmentPowered(uint32_t equipment_id) const;

    // ==================== 交战闭环 (制导雷达 -> 仿真控制软件) ====================

    /// 显控发送交战请求 (制导雷达对锁定目标触发发射)
    void sendEngagement(const EngagementRequest& req);

    /// 仿真控制软件取走一个交战请求
    bool pollEngagement(EngagementRequest* out);

    /// 仿真控制软件发布拦截弹状态 (制导信息回传, 供制导雷达界面展示)
    void publishInterceptorStatus(const std::vector<InterceptorStatus>& statuses);

    /// 显控取走拦截弹状态
    bool pollInterceptorStatuses(std::vector<InterceptorStatus>* out);

    /// 拦截弹状态外发 tap (BrokerNet 仿真侧设置, 用于跨进程转发);
    /// publishInterceptorStatus 时触发 (锁外调用)
    void setInterceptorTap(
        std::function<void(const std::vector<InterceptorStatus>&)> tap)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        interceptor_tap_ = std::move(tap);
    }

    /// 本地注入拦截弹状态快照 (BrokerNet 显控侧收到远端转发后调用;
    /// 语义同 publishInterceptorStatus; 仅仿真侧设置 tap, 无回环)
    void publishInterceptorStatusesLocal(const std::vector<InterceptorStatus>& s)
    {
        publishInterceptorStatus(s);
    }

    // ==================== 数据面帧外发 (BrokerNet 跨进程转发挂点) ====================

    /// 各类型帧外发 tap (BrokerNet 仿真侧设置): publish 成功后锁外调用,
    /// 由 BrokerNet 序列化转发给已链接的显控进程。显控侧注入路径
    /// (handleFrameTopic -> publishRadar...) 不设置 tap, 无回环。
    void setFrameTap(std::function<void(const RadarFrame&)> radar_tap,
                     std::function<void(const SarFrame&)>   sar_tap,
                     std::function<void(const IrFrame&)>    ir_tap,
                     std::function<void(const VisFrame&)>   vis_tap)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        radar_tap_ = std::move(radar_tap);
        sar_tap_   = std::move(sar_tap);
        ir_tap_    = std::move(ir_tap);
        vis_tap_   = std::move(vis_tap);
    }

    // ==================== 向后兼容 (旧 SignalFrame 接口) ====================

    /// @deprecated 使用 publishRadar/Sar/Ir/Vis 代替
    bool publish(const struct SignalFrame& frame);

    /// @deprecated 使用 latestRadar/Sar/Ir/Vis 代替
    bool latest(SensorType t, struct SignalFrame* out);

private:
    SimDataBroker() = default;

    mutable std::mutex mtx_;
    bool server_online_ = false;
    bool host_cache_=false;

    /// 装备数据通道 (按 equipment_id 索引)
    struct EquipmentChannel {
        GpuBuffer         gpu;
        uint64_t          frame_id = 0;
        ChannelStats      stats;
        // 各类型帧的元数据 (不含数据本体)
        RadarFrame        radar_meta;
        SarFrame          sar_meta;
        IrFrame           ir_meta;
        VisFrame          vis_meta;
        SensorType        type = SensorType::RADAR;
        std::shared_ptr<const RadarFrame> radar_host;
        std::shared_ptr<const SarFrame> sar_host;
        std::shared_ptr<const IrFrame> ir_host;
        std::shared_ptr<const VisFrame> vis_host;
    };
    std::unordered_map<uint32_t, EquipmentChannel> channels_;

    /// 当前订阅的装备 ID 集合
    std::set<uint32_t> subscriptions_;

    /// 控制命令队列
    std::deque<ControlCommand> control_queue_;
    uint64_t control_seq_ = 0;

    /// 交战请求队列 (显控 -> 仿真) 与 拦截弹状态队列 (仿真 -> 显控)
    std::deque<EngagementRequest> engagement_queue_;
    uint64_t engagement_seq_ = 0;
    std::deque< std::vector<InterceptorStatus> > interceptor_queue_;

    /// 拦截弹状态外发 tap (跨进程转发; 锁外调用)
    std::function<void(const std::vector<InterceptorStatus>&)> interceptor_tap_;

    /// 各类型帧外发 tap (跨进程转发; 锁外调用, 仅仿真侧设置)
    std::function<void(const RadarFrame&)> radar_tap_;
    std::function<void(const SarFrame&)>   sar_tap_;
    std::function<void(const IrFrame&)>    ir_tap_;
    std::function<void(const VisFrame&)>   vis_tap_;

    /// 获取或创建通道
    EquipmentChannel& getChannel(uint32_t equipment_id);
};

} // namespace vsgraysim
