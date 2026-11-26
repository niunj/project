#pragma once
/// @file BrokerNet.h
/// @brief 装备数据链路网关: 打通仿真控制进程与显控进程。
///
/// 三面分离架构 (全部承载于可配置 ITransport, 默认 shm=UDS):
///   发现面: 仿真侧周期广播装备快照心跳 + 显控同步拉取 (双保险),
///           显控镜像远端注册表并做失联清理 (timeout_ms 无心跳判离线)。
///   链接面: 显控 linkAttach/linkDetach 经 request/reply 建链断链;
///           仿真侧维护链接集合并回调门控 —— 发现面没有链接时
///           仿真不产帧 (Simulation::setSensorGate), 链接建立后
///           才开始数据仿真与传输 (按需产帧, 节省 RT/CUDA 算力)。
///   数据面: 仅已链接装备的帧被序列化转发到 broker/frame/<id> 主题,
///           显控侧解码后注入本地 SimDataBroker 通道 (会话无感知取数)。
///   控制面: 电源/模式/交战命令 request/reply 到仿真侧真实执行, 带 ack。
///
/// 角色:
///   - 仿真控制进程: startSimRole()  (发布方 + 服务端)
///   - 显控进程:     startDisplayRole() (订阅方 + 客户端)
///   - transport=disabled: 单进程直连 (开发调试, 行为与历史版本一致)

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <unordered_map>

#include "vsgraysim/broker/EquipmentInfo.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include "vsgraysim/broker/FlightControl.h"

namespace vsgraysim::ipc {
class ITransport;
}

namespace vsgraysim {

class BrokerNet {
public:
    static BrokerNet& instance();

    struct Config {
        bool        enabled      = true;     // false = 单进程直连 (不启传输)
        std::string transport    = "shm";    // shm | zmq(预留)
        std::string sim_service  = "vsgraysim_sim"; // 仿真进程服务名 (UDS 套接字名)
        int         heartbeat_ms = 500;
        int         timeout_ms   = 2500;     // 显控侧失联判定
        int         request_ms   = 800;      // request/reply 超时
        int         lease_ms     = 5000;     // subscriber lease lifetime, monotonic clock

        /// 从 workspace/config/broker.json 读取, 缺省值兜底
        static Config load();
    };

    enum class Role { None, Sim, Display };

    // ==================== 仿真控制进程 (发布方) ====================
    void startSimRole(const Config& cfg = Config::load());
    /// 链接门控回调: 建链/断链时触发, 参数为当前全部已链接装备。
    /// 仿真侧据此开/关传感器产帧 (Simulation::setSensorGate)。
    void setLinkGate(std::function<void(const std::set<uint32_t>& linked)> cb);
    /// 仿真元数据 (心跳携带, 显控显示链路健康状态)
    void setSimMeta(const std::string& scene, double sim_time,
                    double fps, bool running);
    void setPlatformTelemetry(const std::string& json_array);
    /// Shared Earth scenario configuration; resource paths are resolved by the publisher.
    void setEarthContext(const std::string& json_object);
    /// 某装备当前是否有显控链接 (数据面传输门控)
    bool linkAllows(uint32_t equipment_id) const;
    /// 场景重装载/仿真复位: 清空全部链接并关门 (显控需重新装订建链)
    void clearLinks();
    /// 帧转发 (仿真侧): 仅已链接装备转出。由 SimDataBroker 帧外发 tap 驱动。
    void forwardRadar(const RadarFrame& f);
    void forwardSar(const SarFrame& f);
    void forwardIr(const IrFrame& f);
    void forwardVis(const VisFrame& f);
    void forwardSonar(const SonarFrame& f);

    // ==================== 显控进程 (订阅方) ====================
    void startDisplayRole(const Config& cfg = Config::load());
    /// 同步拉取仿真侧装备快照并合并进本地注册表镜像。
    /// @return 仿真进程是否在线
    bool pullSnapshot(int timeout_ms = 800);
    /// 建链: 请求仿真侧打开该装备的数据通道 (触发产帧门控)。成功后
    /// 本进程开始接收 broker/frame/<id> 并注入本地 SimDataBroker。
    bool linkAttach(uint32_t equipment_id,bool receiveFrames=true);
    /// 断链: 关闭数据通道 (传感器停帧)
    bool linkDetach(uint32_t equipment_id);
    bool leaseActive(uint32_t equipment_id) const;
    bool hasTaskDemand() const;
    nlohmann::json subscriptionStatus() const;
    nlohmann::json transportStatistics() const;
    /// 显控侧: 仿真进程是否在线 (心跳新鲜)
    bool simOnline() const;
    /// 最近一次心跳携带的仿真元数据
    struct SimMeta {
        bool     online = false;
        std::string scene;
        double   sim_time = 0.0, fps = 0.0;
        bool     running = false;
        std::string platforms_json="[]";
        std::string earth_json="{}";
        std::string session_id;
        std::string scene_session_id;
        bool flight_control_available=false;
    };
    SimMeta lastSimMeta() const;

    // ==================== 通用 ====================
    /// 控制命令路由: 显控进程经网络转发到仿真侧执行 (并同步本地 UI 状态);
    /// 仿真侧/单进程本地直达。
    void sendControl(const ControlCommand& cmd);
    /// 交战请求路由 (显控制导雷达 -> 仿真侧)
    void sendEngagement(const EngagementRequest& req);

    /// Nonblocking submission; queued is a transport acknowledgement only.
    /// Retry deduplication retains the most recent 512 request identities.
    std::string submitFlightControl(FlightControlCommand command);
    bool pollFlightControl(FlightControlCommand* command);
    void completeFlightControl(const FlightControlResult& result);
    bool flightControlResult(const std::string& request_id,FlightControlResult* out) const;
    std::string flightClientId() const;
    std::string flightSessionId() const;
    void resetFlightControlSession();
    void setFlightControlAvailable(bool available);

    void stop();
    Role role() const { return role_; }
    bool transportEnabled() const { return tp_ != nullptr; }

private:
    BrokerNet() = default;
    ~BrokerNet();
    BrokerNet(const BrokerNet&) = delete;
    BrokerNet& operator=(const BrokerNet&) = delete;

    // ---- 快照序列化 (发现面) ----
    std::string buildSnapshot() const;
    void handleHeartbeat(const void* data, size_t size);   // display: 镜像合并

    // ---- 帧序列化 (数据面) ----
    void publishFrame(uint32_t sid, SensorType t, const void* body,
                      size_t body_size, uint64_t surfaceRevision, std::vector<uint8_t>& buf);
    void handleFrameTopic(const std::string& topic,
                          const void* data, size_t size);  // display: 注入本地

    // ---- 服务端处理 (仿真侧, 监听线程上下文) ----
    bool onLinkAttach(const std::string& req_json, std::string* resp);
    bool onLinkDetach(const std::string& req_json, std::string* resp);
    bool onLeaseRenew(const std::string&,std::string*);
    void expireLeases();
    void refreshLeaseGateLocked(uint32_t);
    bool onControl(const std::string& req_json, std::string* resp);
    bool onEngagement(const std::string& req_json, std::string* resp);
    size_t linksSize() const;
    /// 快照当前链接集合并触发门控回调 (建链/断链后调用)
    void notifyGate();

    bool requestJson(const std::string& service, const std::string& req_json,
                     std::string* resp_json, int timeout_ms);
    void simLoop();    // 心跳线程 (sim)
    void mirrorLoop(); // 镜像维护线程 (display)

    // ---- 数据面异步转发: 帧序列化后入队, 专用发送线程写传输层,
    //      仿真线程不再被 IPC publish 阻塞 (队列溢出时丢弃最旧帧) ----
    void enqueueForward(std::string topic, std::vector<uint8_t>&& buf);
    void forwardLoop();
    FlightControlResult acceptFlightControl(const FlightControlCommand& command);
    void flightSendLoop();
    void storeFlightResultLocked(const FlightControlResult& result);

    Role role_ = Role::None;
    Config cfg_;
    std::unique_ptr<ipc::ITransport> tp_;

    // ---- sim side ----
    mutable std::mutex sim_mtx_;
    mutable std::mutex snapshot_mtx_;
    std::set<uint32_t> links_;                       // 已链接装备 (数据面门控)
    struct Lease {std::string client,token,scene;int64_t expires=0;bool data=true;};
    std::map<uint32_t,std::map<std::string,Lease>> leases_;
    std::function<void(const std::set<uint32_t>&)> gate_cb_;
    std::string scene_name_;
    double sim_time_ = 0.0, sim_fps_ = 0.0;
    bool sim_running_ = false;
    std::string platforms_json_="[]";
    std::string earth_json_="{}";

    // ---- display side ----
    mutable std::mutex disp_mtx_;
    std::mutex heartbeat_mtx_; // serialize passive heartbeats and query replies
    uint64_t last_snapshot_order_ns_ = 0;
    std::set<std::string> frame_topics_;             // 已订阅帧主题
    int64_t  last_heartbeat_ms_ = 0;                 // epoch ms
    SimMeta  meta_;
    mutable std::mutex lease_mtx_;
    std::condition_variable lease_cv_;
    std::map<uint32_t,Lease> owned_leases_;
    mutable std::mutex transport_stats_mtx_;
    std::map<uint32_t,std::deque<double>> receive_ms_;

    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};

    // ---- 异步转发队列 (sim 侧数据面) ----
    std::mutex fwd_mtx_;
    std::condition_variable fwd_cv_;
    std::deque<std::pair<std::string, std::vector<uint8_t>>> fwd_queue_;
    std::unique_ptr<std::thread> fwd_thread_;
    std::atomic<bool> fwd_running_{false};
    uint64_t fwd_dropped_ = 0;
    static constexpr size_t kFwdQueueCap = 256;
    mutable std::mutex flight_mtx_;
    std::condition_variable flight_cv_;
    std::string flight_client_id_,flight_session_id_,scene_session_id_;
    bool flight_available_=false;
    std::deque<FlightControlCommand> flight_pending_,flight_outgoing_;
    std::unordered_map<std::string,FlightControlResult> flight_results_;
    std::unordered_map<std::string,std::string> flight_payloads_;
    std::unordered_map<std::string,int64_t> flight_enqueued_ms_;
    std::set<std::string> flight_inflight_;
    std::deque<std::string> flight_result_order_;
    std::unique_ptr<std::thread> flight_thread_;
    std::atomic<bool> flight_send_running_{false};
};

} // namespace vsgraysim
