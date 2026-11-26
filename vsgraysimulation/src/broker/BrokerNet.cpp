/// @file BrokerNet.cpp
/// @brief 装备数据链路网关实现 (发现面/链接面/数据面/控制面)。

#include "vsgraysim/broker/BrokerNet.h"
#include "vsgraysim/broker/RadarFrameCodec.h"
#include <vsgraysim/broker/SensorProductCodec.h>
#include <type_traits>

#include "ShmTransport.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include <sstream>

namespace vsgraysim {

using nlohmann::json;

namespace {

std::string earthContextText(const json& value) {
    if(!value.is_object())throw std::invalid_argument("Earth context must be an object");
    for(const auto* field:{"geo_anchor","earth","simulation","atmosphere","ocean"})
        if(value.contains(field)&&!value[field].is_object())throw std::invalid_argument("invalid Earth context object field");
    for(const auto* field:{"terrain_ref","source_scenario"})
        if(value.contains(field)&&!value[field].is_string())throw std::invalid_argument("invalid Earth context path field");
    auto text=value.dump();
    if(text.size()>1024*1024)throw std::invalid_argument("Earth context exceeds 1 MiB");
    return text;
}

std::string flightTokenId(const char* prefix)
{
    static std::atomic<uint64_t> counter{0};
    return std::string(prefix)+"_"+std::to_string(getpid())+"_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count())+"_"+std::to_string(++counter);
}

int64_t flightMonotonicMs() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}

int64_t epochMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ---------------------------------------------------------------------------
// 二进制读写辅助 (帧/拦截弹状态序列化; 同一代码库编译, 布局一致)
// ---------------------------------------------------------------------------
struct Writer {
    std::vector<uint8_t> buf;
    template <class T>
    void put(const T& v)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }
    void putRaw(const void* p, size_t n)
    {
        if(!n) return;
        const auto* b = reinterpret_cast<const uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    void putStr(const std::string& s)
    {
        uint32_t n = uint32_t(s.size());
        put(n);
        putRaw(s.data(), n);
    }
    template <class T>
    void putVec(const std::vector<T>& v)
    {
        uint32_t n = uint32_t(v.size());
        put(n);
        putRaw(v.data(), size_t(n) * sizeof(T));
    }
};

struct Reader {
    const uint8_t* p = nullptr;
    size_t n = 0, off = 0;
    bool ok = true;

    template <class T>
    T get()
    {
        if (off + sizeof(T) > n) {
            ok = false;
            return T{};
        }
        T v;
        std::memcpy(&v, p + off, sizeof(T));
        off += sizeof(T);
        return v;
    }
    std::string getStr()
    {
        uint32_t len = get<uint32_t>();
        if (!ok || off + len > n) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p + off), len);
        off += len;
        return s;
    }
    template <class T>
    void getVec(std::vector<T>& v, uint64_t expected_elems,bool optional=false)
    {
        uint32_t len = get<uint32_t>();
        if (!ok || (len != expected_elems && !(optional && len==0)) || off>n || uint64_t(len)*sizeof(T)>n-off) {
            ok = false;
            return;
        }
        v.resize(len);
        if(len) std::memcpy(v.data(), p + off, size_t(len) * sizeof(T));
        off += size_t(len) * sizeof(T);
    }
};

// ---- 帧序列化 (按 SensorTypes.h 枚举 int32 顺序) ----
void serializeRadar(Writer& w, const RadarFrame& f)
{
    const auto body = wire::encodeRadarFrame(f);
    w.putRaw(body.data(), body.size());
}

RadarFrame deserializeRadar(Reader& r)
{
    RadarFrame frame;
    r.ok = r.ok && wire::decodeRadarFrame(r.p + r.off, r.n - r.off, frame);
    r.off = r.n;
    return frame;
}

void serializeSar(Writer& w, const SarFrame& f)
{
    w.put(f.frame_id); w.put(f.sim_time); w.put(f.timestamp_ns);
    w.put(f.sensor_id); w.put(f.platform_id);
    w.put(f.carrier_hz); w.put(f.prf); w.put(f.velocity); w.put(f.altitude);
    w.put(f.incidence_angle); w.put(f.swath_width);
    w.put(uint32_t(f.mode)); w.put(f.looks);
    w.put(f.range_resolution); w.put(f.azimuth_resolution);
    w.put(f.latitude); w.put(f.longitude); w.put(f.altitude_m);
    w.put(f.roll); w.put(f.pitch); w.put(f.yaw);
    w.put(f.velocity_n); w.put(f.velocity_e); w.put(f.velocity_d);
    w.put(f.range_samples); w.put(f.azimuth_samples);
    w.put(f.snr_db); w.put(f.quality_flags); w.put(f.stage);
    w.putVec(f.echo_data);
    w.put(uint32_t(0x31524153)); // SAR1 optional sampling extension; legacy prefix unchanged
    w.put(f.sample_rate_hz); w.put(f.range_min);
    if(f.hasFocusedIsar()) {
        const auto& axes=*f.isar_axes;
        w.put(uint32_t(0x32524153)); // SAR2 optional focused-image axes
        w.put(axes.range_origin_m); w.put(axes.range_spacing_m);
        w.put(axes.cross_range_origin_m); w.put(axes.cross_range_spacing_m);
        w.put(uint8_t(axes.illustrative));
    }
}

SarFrame deserializeSar(Reader& r)
{
    SarFrame f;
    f.frame_id = r.get<uint64_t>(); f.sim_time = r.get<double>();
    f.timestamp_ns = r.get<uint64_t>();
    f.sensor_id = r.get<uint32_t>(); f.platform_id = r.get<uint32_t>();
    f.carrier_hz = r.get<float>(); f.prf = r.get<float>();
    f.velocity = r.get<float>(); f.altitude = r.get<float>();
    f.incidence_angle = r.get<float>(); f.swath_width = r.get<float>();
    f.mode = SarMode(r.get<uint32_t>()); f.looks = r.get<uint32_t>();
    f.range_resolution = r.get<float>(); f.azimuth_resolution = r.get<float>();
    f.latitude = r.get<double>(); f.longitude = r.get<double>();
    f.altitude_m = r.get<double>();
    f.roll = r.get<float>(); f.pitch = r.get<float>(); f.yaw = r.get<float>();
    f.velocity_n = r.get<float>(); f.velocity_e = r.get<float>();
    f.velocity_d = r.get<float>();
    f.range_samples = r.get<uint32_t>(); f.azimuth_samples = r.get<uint32_t>();
    f.snr_db = r.get<float>(); f.quality_flags = r.get<uint32_t>();
    f.stage = r.get<uint8_t>();
    f.echo_data.clear();
    r.getVec(f.echo_data, uint64_t(f.range_samples) * f.azimuth_samples);
    if(r.ok && r.off<r.n) {
        if(r.n-r.off<12 || r.get<uint32_t>()!=0x31524153) { r.ok=false; return f; }
        f.sample_rate_hz=r.get<float>(); f.range_min=r.get<float>();
        if(!std::isfinite(f.sample_rate_hz) || f.sample_rate_hz<0 || !std::isfinite(f.range_min)) r.ok=false;
    }
    if(r.ok&&r.off<r.n) {
        if(r.n-r.off!=4+4*sizeof(double)+1||r.get<uint32_t>()!=0x32524153){r.ok=false;return f;}
        IsarImageAxes axes;
        axes.range_origin_m=r.get<double>();axes.range_spacing_m=r.get<double>();
        axes.cross_range_origin_m=r.get<double>();axes.cross_range_spacing_m=r.get<double>();
        const auto illustrative=r.get<uint8_t>();axes.illustrative=illustrative==1;
        f.isar_axes=axes;
        if(illustrative>1||!f.hasFocusedIsar())r.ok=false;
    }
    return f;
}

void serializeIr(Writer& w, const IrFrame& f)
{
    w.put(f.frame_id); w.put(f.sim_time); w.put(f.timestamp_ns);
    w.put(f.sensor_id); w.put(f.platform_id);
    w.put(f.wavelength_min); w.put(f.wavelength_max);
    w.put(f.fov_horizontal); w.put(f.fov_vertical);
    w.put(f.integration_time); w.put(f.netd);
    w.put(f.emissivity); w.put(f.atmospheric_trans);
    w.put(f.latitude); w.put(f.longitude); w.put(f.altitude);
    w.put(f.roll); w.put(f.pitch); w.put(f.yaw);
    w.put(f.width); w.put(f.height);
    w.put(f.snr_db); w.put(f.quality_flags); w.put(f.stage);
    w.putVec(f.radiance);
    w.putVec(f.temperature);
}

IrFrame deserializeIr(Reader& r)
{
    IrFrame f;
    f.frame_id = r.get<uint64_t>(); f.sim_time = r.get<double>();
    f.timestamp_ns = r.get<uint64_t>();
    f.sensor_id = r.get<uint32_t>(); f.platform_id = r.get<uint32_t>();
    f.wavelength_min = r.get<float>(); f.wavelength_max = r.get<float>();
    f.fov_horizontal = r.get<float>(); f.fov_vertical = r.get<float>();
    f.integration_time = r.get<float>(); f.netd = r.get<float>();
    f.emissivity = r.get<float>(); f.atmospheric_trans = r.get<float>();
    f.latitude = r.get<double>(); f.longitude = r.get<double>();
    f.altitude = r.get<double>();
    f.roll = r.get<float>(); f.pitch = r.get<float>(); f.yaw = r.get<float>();
    f.width = r.get<uint32_t>(); f.height = r.get<uint32_t>();
    f.snr_db = r.get<float>(); f.quality_flags = r.get<uint32_t>();
    f.stage = r.get<uint8_t>();
    f.radiance.clear();
    r.getVec(f.radiance, uint64_t(f.width) * f.height);
    f.temperature.clear();
    r.getVec(f.temperature, uint64_t(f.width) * f.height,true);
    return f;
}

void serializeVis(Writer& w, const VisFrame& f)
{
    w.put(f.frame_id); w.put(f.sim_time); w.put(f.timestamp_ns);
    w.put(f.sensor_id); w.put(f.platform_id);
    w.put(f.wavelength); w.put(f.fov); w.put(f.exposure_time); w.put(f.gain);
    w.put(f.latitude); w.put(f.longitude); w.put(f.altitude);
    w.put(f.roll); w.put(f.pitch); w.put(f.yaw);
    w.put(f.width); w.put(f.height);
    w.put(f.quality_flags); w.put(f.stage);
    w.putVec(f.intensity);
}

VisFrame deserializeVis(Reader& r)
{
    VisFrame f;
    f.frame_id = r.get<uint64_t>(); f.sim_time = r.get<double>();
    f.timestamp_ns = r.get<uint64_t>();
    f.sensor_id = r.get<uint32_t>(); f.platform_id = r.get<uint32_t>();
    f.wavelength = r.get<float>(); f.fov = r.get<float>();
    f.exposure_time = r.get<float>(); f.gain = r.get<float>();
    f.latitude = r.get<double>(); f.longitude = r.get<double>();
    f.altitude = r.get<double>();
    f.roll = r.get<float>(); f.pitch = r.get<float>(); f.yaw = r.get<float>();
    f.width = r.get<uint32_t>(); f.height = r.get<uint32_t>();
    f.quality_flags = r.get<uint32_t>();
    f.stage = r.get<uint8_t>();
    f.intensity.clear();
    r.getVec(f.intensity, uint64_t(f.width) * f.height);
    return f;
}

// ---- EquipmentInfo <-> json ----
json infoToJson(const EquipmentInfo& e)
{
    json j;
    j["id"] = e.equipment_id;
    j["name"] = e.name;
    j["type"] = int(e.type);
    j["platform_id"] = e.platform_id;
    j["platform_name"] = e.platform_name;
    j["faction"] = e.faction;
    j["platform_kind"] = e.platform_kind;
    j["role"] = e.role;
    j["powered"] = e.powered;
    j["active"] = e.active;
    j["subscribed"] = e.subscribed;
    j["frame_rate"] = e.frame_rate;
    j["range_max"] = e.range_max;
    j["description"] = e.description;
    if(!e.model_id.empty())j["model_id"] = e.model_id;
    return j;
}

EquipmentInfo infoFromJson(const json& j)
{
    EquipmentInfo e;
    e.equipment_id = j.value("id", 0u);
    e.name = j.value("name", std::string());
    e.type = SensorType(j.value("type", 3));
    e.platform_id = j.value("platform_id", 0u);
    e.platform_name = j.value("platform_name", std::string());
    e.faction = j.value("faction", 0);
    e.platform_kind = j.value("platform_kind", std::string());
    e.role = j.value("role", 0);
    e.powered = j.value("powered", true);
    e.active = j.value("active", false);
    e.subscribed = j.value("subscribed", false);
    e.frame_rate = j.value("frame_rate", 0.0f);
    e.range_max = j.value("range_max", 0.0f);
    e.description = j.value("description", std::string());
    e.model_id = j.value("model_id", std::string());
    return e;
}

} // anonymous namespace

namespace wire {
std::vector<uint8_t> encodeSensorProduct(const SensorProduct& product){Writer w;std::visit([&](const auto& frame){using T=std::decay_t<decltype(frame)>;if constexpr(std::is_same_v<T,RadarFrame>){w.put(uint32_t(SensorType::RADAR));serializeRadar(w,frame);}else if constexpr(std::is_same_v<T,SarFrame>){w.put(uint32_t(SensorType::SAR));serializeSar(w,frame);}else if constexpr(std::is_same_v<T,IrFrame>){w.put(uint32_t(SensorType::IR));serializeIr(w,frame);}else {w.put(uint32_t(SensorType::VIS));serializeVis(w,frame);}w.put(frame.surface_revision);},product);return std::move(w.buf);}
bool decodeSensorProduct(const void* bytes,size_t size,SensorProduct& out){if(size<12||size>16*1024*1024)return false;Reader r{static_cast<const uint8_t*>(bytes),size};const auto type=SensorType(r.get<uint32_t>());uint64_t revision;std::memcpy(&revision,r.p+size-sizeof(revision),sizeof(revision));r.n-=sizeof(revision);switch(type){case SensorType::RADAR:out=deserializeRadar(r);break;case SensorType::SAR:out=deserializeSar(r);break;case SensorType::IR:out=deserializeIr(r);break;case SensorType::VIS:out=deserializeVis(r);break;default:return false;}if(!r.ok||r.off!=r.n)return false;std::visit([&](auto& f){f.surface_revision=revision;},out);return true;}
}

// =============================================================================
// 单例 / 生命周期
// =============================================================================
BrokerNet& BrokerNet::instance()
{
    static BrokerNet net;
    return net;
}

BrokerNet::~BrokerNet()
{
    stop();
}

BrokerNet::Config BrokerNet::Config::load()
{
    Config c;
    try {
        std::ifstream f("workspace/config/broker.json");
        if (f) {
            json j;
            f >> j;
            c.enabled = j.value("enabled", c.enabled);
            c.transport = j.value("transport", c.transport);
            c.sim_service = j.value("sim_service", c.sim_service);
            c.heartbeat_ms = j.value("heartbeat_ms", c.heartbeat_ms);
            c.timeout_ms = j.value("timeout_ms", c.timeout_ms);
            c.request_ms = j.value("request_ms", c.request_ms);
            c.lease_ms = std::clamp(j.value("lease_ms", c.lease_ms),500,60000);
        }
    } catch (const std::exception& e) {
        spdlog::warn("BrokerNet: config load failed ({}), using defaults", e.what());
    }
    return c;
}

// =============================================================================
// 仿真控制进程 (发布方)
// =============================================================================
void BrokerNet::startSimRole(const Config& cfg)
{
    if (role_ != Role::None) return;
    cfg_ = cfg;SimDataBroker::instance().enableHostFrameCache(false);
    role_ = Role::Sim;
    setEarthContext("{}");
    resetFlightControlSession();
    {std::lock_guard<std::mutex> lock(flight_mtx_);scene_session_id_=flightTokenId("scene");}

    if (!cfg_.enabled || cfg_.transport != "shm") {
        if (cfg_.enabled)
            spdlog::warn("BrokerNet: transport '{}' not implemented yet, "
                         "falling back to in-process mode", cfg_.transport);
        else
            spdlog::info("BrokerNet: sim role in in-process mode (no transport)");
        return;
    }

    auto* tp = new ipc::ShmTransport();
    tp->init(cfg_.sim_service);
    if (!tp->start()) {
        spdlog::error("BrokerNet: sim transport start failed");
        delete tp;
        return;
    }
    tp_.reset(tp);

    // ---- 发现面: 注册表快照查询服务 ----
    tp_->serve("broker/registry/query", [this](const void*, size_t,
                                               std::vector<uint8_t>& resp) {
        std::string s = buildSnapshot();
        resp.assign(s.begin(), s.end());
        return true;
    });

    // ---- 链接面: 建链/断链 ----
    tp_->serve("broker/link/attach", [this](const void* data, size_t size,
                                            std::vector<uint8_t>& resp) {
        std::string req(reinterpret_cast<const char*>(data), size);
        std::string r;
        onLinkAttach(req, &r);
        resp.assign(r.begin(), r.end());
        return true;
    });
    tp_->serve("broker/link/detach", [this](const void* data, size_t size,
                                            std::vector<uint8_t>& resp) {
        std::string req(reinterpret_cast<const char*>(data), size);
        std::string r;
        onLinkDetach(req, &r);
        resp.assign(r.begin(), r.end());
        return true;
    });

    tp_->serve("broker/link/renew",[this](const void* data,size_t size,std::vector<uint8_t>& response){
        std::string answer;onLeaseRenew(std::string(static_cast<const char*>(data),size),&answer);response.assign(answer.begin(),answer.end());return true;
    });

    // ---- 控制面: 电源/模式/交战 ----
    tp_->serve("broker/control", [this](const void* data, size_t size,
                                        std::vector<uint8_t>& resp) {
        std::string req(reinterpret_cast<const char*>(data), size);
        std::string r;
        onControl(req, &r);
        resp.assign(r.begin(), r.end());
        return true;
    });
    tp_->serve("broker/flight/control", [this](const void* data,size_t size,std::vector<uint8_t>& response) {
        json answer;
        try {
            if(size>8192)throw std::invalid_argument("flight request exceeds 8 KiB");
            const auto command=json::parse(static_cast<const char*>(data),static_cast<const char*>(data)+size).get<FlightControlCommand>();
            answer={{"result",acceptFlightControl(command)}};
        } catch(const std::exception& error) {answer={{"error",std::string(error.what())}};}
        const auto bytes=answer.dump();response.assign(bytes.begin(),bytes.end());return true;
    });
    tp_->serve("broker/engagement", [this](const void* data, size_t size,
                                           std::vector<uint8_t>& resp) {
        std::string req(reinterpret_cast<const char*>(data), size);
        std::string r;
        onEngagement(req, &r);
        resp.assign(r.begin(), r.end());
        return true;
    });

    // 拦截弹状态外发 tap (仿真 -> 显控 制导雷达界面)
    SimDataBroker::instance().setInterceptorTap(
        [this](const std::vector<InterceptorStatus>& statuses) {
            if (!tp_ || statuses.empty()) return;
            Writer w;
            w.put(uint32_t(statuses.size()));
            for (const auto& s : statuses) {
                w.put(s.interceptor_id);
                w.putStr(s.name);
                w.put(s.target_id);
                w.put(uint8_t(s.launched)); w.put(uint8_t(s.in_flight));
                w.put(uint8_t(s.impacted)); w.put(uint8_t(s.target_destroyed));
                w.put(s.range_km); w.put(s.launch_time); w.put(s.sim_time);
            }
            tp_->publish("broker/interceptors", w.buf.data(), w.buf.size());
        });

    // 数据面帧外发 tap: publish 成功后锁外触发, 仅已链接装备被转出
    SimDataBroker::instance().setFrameTap(
        [this](const RadarFrame& f) { forwardRadar(f); },
        [this](const SarFrame&  f) { forwardSar(f);  },
        [this](const IrFrame&   f) { forwardIr(f);   },
        [this](const VisFrame&  f) { forwardVis(f);  });
    SimDataBroker::instance().setSonarTap([this](const SonarFrame& f){forwardSonar(f);});

    running_ = true;
    // 数据面异步转发线程: 帧外发不再阻塞仿真帧预算 (序列化仍即时, IPC 写入后台)
    fwd_running_.store(true);
    fwd_thread_ = std::make_unique<std::thread>([this]() { forwardLoop(); });
    thread_ = std::make_unique<std::thread>([this]() { simLoop(); });
    spdlog::info("BrokerNet: sim role started (service='{}', heartbeat={}ms)",
                 cfg_.sim_service, cfg_.heartbeat_ms);
}

void BrokerNet::setLinkGate(std::function<void(const std::set<uint32_t>&)> cb)
{
    std::lock_guard<std::mutex> lock(sim_mtx_);
    gate_cb_ = std::move(cb);
}

void BrokerNet::setSimMeta(const std::string& scene, double sim_time,
                           double fps, bool running)
{
    std::lock_guard<std::mutex> lock(sim_mtx_);
    if(scene_name_!=scene || sim_time<sim_time_) {platforms_json_="[]";earth_json_="{}";}
    scene_name_ = scene;
    sim_time_ = sim_time;
    sim_fps_ = fps;
    sim_running_ = running;
}

bool BrokerNet::linkAllows(uint32_t equipment_id) const
{
    std::lock_guard<std::mutex> lock(sim_mtx_);
    return links_.count(equipment_id) > 0;
}

void BrokerNet::setPlatformTelemetry(const std::string& text) {
    if(text.size()>1024*1024) throw std::invalid_argument("platform telemetry exceeds 1 MiB");
    const auto value=json::parse(text);
    if(!value.is_array() || value.size()>4096) throw std::invalid_argument("invalid platform telemetry array");
    std::lock_guard<std::mutex> lock(sim_mtx_); platforms_json_=value.dump();
}
void BrokerNet::setEarthContext(const std::string& text) {
    if(text.size()>1024*1024)throw std::invalid_argument("Earth context exceeds 1 MiB");
    const auto validated=earthContextText(json::parse(text));
    std::lock_guard<std::mutex> lock(sim_mtx_);earth_json_=validated;
}

void BrokerNet::simLoop()
{
    while (running_) {
        expireLeases();
        std::string snap = buildSnapshot();
        if (tp_)
            tp_->publish("broker/registry/heartbeat", snap.data(), snap.size());
        std::this_thread::sleep_for(
            std::chrono::milliseconds(cfg_.heartbeat_ms));
    }
}

std::string BrokerNet::buildSnapshot() const
{
    std::lock_guard snapshot(snapshot_mtx_);
    std::lock_guard state(sim_mtx_);
    json j;
    j["proto"] = 1;
    j["subscription_leases"]=true;j["lease_ms"]=cfg_.lease_ms;
    j["ts_ms"] = uint64_t(epochMs());
    // All transports are local. CLOCK_MONOTONIC orders snapshots across worker
    // restarts too; query replies may arrive after a newer passive heartbeat.
    j["snapshot_order_ns"] = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    {
        j["host"] = cfg_.sim_service;
        j["pid"] = uint32_t(getpid());
        j["scene"] = scene_name_;
        j["sim_time"] = sim_time_;
        j["fps"] = sim_fps_;
        j["running"] = sim_running_;
        j["platforms"] = json::parse(platforms_json_);
        j["earth"] = json::parse(earth_json_);
        json arr = json::array();
        for (const auto& e : EquipmentRegistry::instance().listOnline()) {
            json je = infoToJson(e);
            je["subscribed"] = links_.count(e.equipment_id) > 0;
            size_t data=0,binding=0;auto found=leases_.find(e.equipment_id);if(found!=leases_.end())for(const auto& [client,lease]:found->second)if(lease.data)++data;else ++binding;
            je["data_subscribers"]=data;je["binding_subscribers"]=binding;
            arr.push_back(je);
        }
        j["equipment"] = arr;
    }
    {
        std::lock_guard<std::mutex> lock(flight_mtx_);
        j["session_id"]=flight_session_id_;
        j["scene_session_id"]=scene_session_id_;
        j["flight_control_available"]=flight_available_ && SimDataBroker::instance().isServerOnline();
        j["control_results"]=json::array();
        for(const auto& id:flight_result_order_) {
            const auto found=flight_results_.find(id);
            if(found!=flight_results_.end())j["control_results"].push_back(found->second);
        }
    }
    return j.dump();
}

// ---- 链接面服务端 ----
void BrokerNet::refreshLeaseGateLocked(uint32_t id) {
    if(id==0)return; // world/time observers are not measurement equipment
    bool data=false;const auto found=leases_.find(id);if(found!=leases_.end())for(const auto& [client,lease]:found->second)data|=lease.data;
    if(data){links_.insert(id);EquipmentRegistry::instance().setSubscribed(id,true);SimDataBroker::instance().subscribe(id);}
    else {links_.erase(id);EquipmentRegistry::instance().setSubscribed(id,false);SimDataBroker::instance().unsubscribe(id);}
}
void BrokerNet::expireLeases(){
    bool changed=false;const auto now=flightMonotonicMs();
    {std::lock_guard lock(sim_mtx_);for(auto it=leases_.begin();it!=leases_.end();){bool erased=false;for(auto lease=it->second.begin();lease!=it->second.end();)if(lease->second.expires<=now){lease=it->second.erase(lease);erased=true;}else ++lease;
        if(erased){refreshLeaseGateLocked(it->first);changed=true;}if(it->second.empty())it=leases_.erase(it);else ++it;}}
    if(changed)notifyGate();
}
bool BrokerNet::onLinkAttach(const std::string& text,std::string* response){
    try {
        if(text.size()>8192)throw std::invalid_argument("lease request too large");const auto j=json::parse(text);const auto id=j.at("equipment_id").get<uint32_t>();
        const auto client=j.at("client_id").get<std::string>(),scene=j.at("scene_session_id").get<std::string>();
        const bool world=id==0&&!j.value("receive_frames",true)&&j.value("task_type",std::string())=="world";
        if((!id&&!world)||client.empty()||client.size()>192||scene.empty()||scene.size()>192)throw std::invalid_argument("invalid subscriber identity");
        Lease lease;
        {std::lock_guard lock(sim_mtx_);std::lock_guard epoch(flight_mtx_);
            if(scene!=scene_session_id_||!SimDataBroker::instance().isServerOnline()||(!world&&!EquipmentRegistry::instance().findById(id)))throw std::invalid_argument("stale scene or offline equipment");
            if(leases_.size()>=4096&&!leases_.count(id))throw std::runtime_error("lease equipment capacity exceeded");
            auto& clients=leases_[id];if(clients.size()>=128&&!clients.count(client))throw std::runtime_error("subscriber capacity exceeded");
            auto& current=clients[client];if(current.token.empty()||current.expires<=flightMonotonicMs())current.token=flightTokenId("subscription");current.client=client;current.scene=scene;current.data=j.value("receive_frames",true);current.expires=flightMonotonicMs()+cfg_.lease_ms;lease=current;refreshLeaseGateLocked(id);
        }
        notifyGate();*response=json{{"ok",true},{"token",lease.token},{"scene_session_id",scene},{"lease_ms",cfg_.lease_ms}}.dump();return true;
    }catch(const std::exception& e){*response=json{{"ok",false},{"error",e.what()}}.dump();return false;}
}
bool BrokerNet::onLinkDetach(const std::string& text,std::string* response){
    try {
        if(text.size()>8192)throw std::invalid_argument("lease request too large");const auto j=json::parse(text);const auto id=j.at("equipment_id").get<uint32_t>();
        const auto client=j.at("client_id").get<std::string>(),token=j.at("token").get<std::string>(),scene=j.at("scene_session_id").get<std::string>();
        {std::lock_guard lock(sim_mtx_);std::lock_guard epoch(flight_mtx_);if(scene!=scene_session_id_)throw std::invalid_argument("stale scene");
            auto equipment=leases_.find(id);if(equipment!=leases_.end()){auto owned=equipment->second.find(client);if(owned!=equipment->second.end()){
                if(owned->second.token!=token||token.empty())throw std::invalid_argument("subscriber token mismatch");equipment->second.erase(owned);refreshLeaseGateLocked(id);if(equipment->second.empty())leases_.erase(equipment);}}
        }
        notifyGate();*response=R"({"ok":true})";return true;
    }catch(const std::exception& e){*response=json{{"ok",false},{"error",e.what()}}.dump();return false;}
}
bool BrokerNet::onLeaseRenew(const std::string& text,std::string* response){
    try {
        if(text.size()>128*1024)throw std::invalid_argument("renewal too large");const auto j=json::parse(text);const auto client=j.at("client_id").get<std::string>(),scene=j.at("scene_session_id").get<std::string>();const auto& items=j.at("leases");if(!items.is_array()||items.size()>256)throw std::invalid_argument("invalid renewal batch");
        json valid=json::array();{std::lock_guard lock(sim_mtx_);std::lock_guard epoch(flight_mtx_);if(scene!=scene_session_id_)throw std::invalid_argument("stale scene");const auto now=flightMonotonicMs();
            for(const auto& item:items){const auto id=item.at("equipment_id").get<uint32_t>();auto e=leases_.find(id);if(e==leases_.end())continue;auto l=e->second.find(client);if(l!=e->second.end()&&l->second.token==item.at("token")&&l->second.expires>now){l->second.expires=now+cfg_.lease_ms;valid.push_back(id);}}
        }*response=json{{"ok",true},{"valid",valid},{"lease_ms",cfg_.lease_ms}}.dump();return true;
    }catch(const std::exception& e){*response=json{{"ok",false},{"error",e.what()}}.dump();return false;}
}

size_t BrokerNet::linksSize() const
{
    std::lock_guard<std::mutex> lock(sim_mtx_);
    return links_.size();
}

void BrokerNet::notifyGate()
{
    std::set<uint32_t> snap;
    std::function<void(const std::set<uint32_t>&)> cb;
    {
        std::lock_guard<std::mutex> lk(sim_mtx_);
        snap = links_;
        cb = gate_cb_;
    }
    if (cb) cb(snap);
}

void BrokerNet::clearLinks()
{
    resetFlightControlSession();
    {std::lock_guard<std::mutex> lock(flight_mtx_);scene_session_id_=flightTokenId("scene");}
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(sim_mtx_);
        changed = !links_.empty();
        links_.clear();leases_.clear();
        earth_json_="{}";
    }
    for (const auto& e : EquipmentRegistry::instance().listOnline())
        {EquipmentRegistry::instance().setSubscribed(e.equipment_id, false);SimDataBroker::instance().unsubscribe(e.equipment_id);}
    if (changed)
        notifyGate();   // -> 全部装备停帧
}

// ---- 控制面服务端 ----
bool BrokerNet::onControl(const std::string& req_json, std::string* resp)
{
    try {
        json j = json::parse(req_json);
        ControlCommand cmd;
        cmd.type = ControlCommand::Type(j.value("type", 0));
        cmd.equipment_type = SensorType(j.value("equipment_type", 3));
        cmd.equipment_id = j.value("equipment_id", 0u);
        cmd.param_f = j.value("param_f", 0.0f);
        cmd.param_u = j.value("param_u", 0u);
        if(cmd.type!=ControlCommand::Type::PowerOn && cmd.type!=ControlCommand::Type::PowerOff) {
            *resp=R"({"ok":false,"error":"unsupported equipment control"})"; return true;
        }
        if(!SimDataBroker::instance().isServerOnline() || !EquipmentRegistry::instance().findById(cmd.equipment_id)) {
            *resp=R"({"ok":false,"error":"equipment offline or unknown"})"; return true;
        }
        SimDataBroker::instance().sendControl(cmd);  // 入队 + 电源立即生效
        *resp = R"({"ok":true})";
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("BrokerNet: control parse failed: {}", e.what());
        return false;
    }
}

bool BrokerNet::onEngagement(const std::string& req_json, std::string* resp)
{
    try {
        json j = json::parse(req_json);
        EngagementRequest req;
        req.fire = j.value("fire", false);
        req.target_x = j.value("target_x", 0.0f);
        req.target_y = j.value("target_y", 0.0f);
        req.target_z = j.value("target_z", 0.0f);
        req.gate_m = j.value("gate_m", 5000.0f);
        req.source_equipment_id = j.value("source_equipment_id", 0u);
        SimDataBroker::instance().sendEngagement(req);
        *resp = R"({"ok":true})";
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("BrokerNet: engagement parse failed: {}", e.what());
        return false;
    }
}

// ---- 数据面: 帧转发 (仿真侧) ----
void BrokerNet::publishFrame(uint32_t sid, SensorType t, const void* body,
                             size_t body_size, uint64_t surfaceRevision, std::vector<uint8_t>& buf)
{
    Writer w;
    w.put(uint32_t(t));
    w.put(sid);
    w.put(uint32_t(body_size));
    w.putRaw(body, body_size);
    w.put(surfaceRevision);
    {std::lock_guard<std::mutex> lock(flight_mtx_);w.putStr(scene_session_id_);}
    w.put(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    buf = std::move(w.buf);
}

void BrokerNet::forwardSonar(const SonarFrame& f) {
    if(!tp_||!linkAllows(f.sensor_id))return;
    Writer body;
    body.put(uint8_t(f.water_column_enabled?2:1));
    body.put(f.platform_id);body.put(f.frame_id);body.put(f.tick);body.put(f.sim_time);
    body.put(f.range_limit_m);body.put(f.transducer_draft_m);body.put(f.clearance_m);
    body.put(f.water_depth_m);body.put(f.two_way_ms);body.put(uint8_t(f.bottom));
    if(f.water_column_enabled)body.put(f.echo_range_m);
    std::vector<uint8_t> buffer;
    publishFrame(f.sensor_id,SensorType::SONAR,body.buf.data(),body.buf.size(),f.surface_revision,buffer);
    enqueueForward("broker/frame/"+std::to_string(f.sensor_id),std::move(buffer));
}

void BrokerNet::forwardRadar(const RadarFrame& f)
{
    if (!tp_ || !linkAllows(f.sensor_id)) return;
    Writer w;
    serializeRadar(w, f);
    std::vector<uint8_t> buf;
    publishFrame(f.sensor_id, SensorType::RADAR, w.buf.data(), w.buf.size(), f.surface_revision, buf);
    enqueueForward("broker/frame/" + std::to_string(f.sensor_id), std::move(buf));
}

void BrokerNet::forwardSar(const SarFrame& f)
{
    if (!tp_ || !linkAllows(f.sensor_id)) return;
    Writer w;
    serializeSar(w, f);
    std::vector<uint8_t> buf;
    publishFrame(f.sensor_id, SensorType::SAR, w.buf.data(), w.buf.size(), f.surface_revision, buf);
    enqueueForward("broker/frame/" + std::to_string(f.sensor_id), std::move(buf));
}

void BrokerNet::forwardIr(const IrFrame& f)
{
    if (!tp_ || !linkAllows(f.sensor_id)) return;
    Writer w;
    serializeIr(w, f);
    std::vector<uint8_t> buf;
    publishFrame(f.sensor_id, SensorType::IR, w.buf.data(), w.buf.size(), f.surface_revision, buf);
    enqueueForward("broker/frame/" + std::to_string(f.sensor_id), std::move(buf));
}

void BrokerNet::forwardVis(const VisFrame& f)
{
    if (!tp_ || !linkAllows(f.sensor_id)) return;
    Writer w;
    serializeVis(w, f);
    std::vector<uint8_t> buf;
    publishFrame(f.sensor_id, SensorType::VIS, w.buf.data(), w.buf.size(), f.surface_revision, buf);
    enqueueForward("broker/frame/" + std::to_string(f.sensor_id), std::move(buf));
}

void BrokerNet::enqueueForward(std::string topic, std::vector<uint8_t>&& buf)
{
    if (!fwd_running_.load()) {          // 无转发线程 (单进程直连模式): 同步直发
        if (tp_) tp_->publish(topic, buf.data(), buf.size());
        return;
    }
    std::lock_guard<std::mutex> lk(fwd_mtx_);
    if (fwd_queue_.size() >= kFwdQueueCap) {
        fwd_queue_.pop_front();          // 溢出丢最旧帧: 显控优先拿到最新帧
        if (++fwd_dropped_ % 64 == 1)
            spdlog::warn("BrokerNet: forward queue full, dropped {} stale frames total",
                         fwd_dropped_);
    }
    fwd_queue_.emplace_back(std::move(topic), std::move(buf));
    fwd_cv_.notify_one();
}

void BrokerNet::forwardLoop()
{
    for (;;) {
        std::pair<std::string, std::vector<uint8_t>> item;
        {
            std::unique_lock<std::mutex> lk(fwd_mtx_);
            fwd_cv_.wait(lk, [this] {
                return !fwd_running_.load() || !fwd_queue_.empty();
            });
            if (fwd_queue_.empty()) {
                if (!fwd_running_.load()) break;
                continue;
            }
            item = std::move(fwd_queue_.front());
            fwd_queue_.pop_front();
        }
        if (tp_)
            tp_->publish(item.first, item.second.data(), item.second.size());
    }
}

// =============================================================================
// 显控进程 (订阅方)
// =============================================================================
void BrokerNet::startDisplayRole(const Config& cfg)
{
    if (role_ != Role::None) return;
    last_snapshot_order_ns_=0;
    cfg_ = cfg;SimDataBroker::instance().enableHostFrameCache(true);
    role_ = Role::Display;

    // 本地 SimDataBroker 作为帧缓存服务端 (显控会话无感知取数)
    SimDataBroker::instance().serverOnline(true);

    if (!cfg_.enabled || cfg_.transport != "shm") {
        if (cfg_.enabled)
            spdlog::warn("BrokerNet: transport '{}' not implemented yet, "
                         "falling back to in-process mode", cfg_.transport);
        else
            spdlog::info("BrokerNet: display role in in-process mode (no transport)");
        return;
    }

    auto* tp = new ipc::ShmTransport();
    tp->init("vsgraysim_display_" + std::to_string(getpid()));
    if (!tp->start()) {
        spdlog::error("BrokerNet: display transport start failed");
        delete tp;
        return;
    }
    tp_.reset(tp);
    flight_send_running_=true;
    flight_thread_=std::make_unique<std::thread>([this]{flightSendLoop();});

    // ---- 发现面: 心跳订阅 (被动镜像) ----
    tp_->subscribe("broker/registry/heartbeat",
                   [this](const void* data, size_t size) {
                       handleHeartbeat(data, size);
                   });

    // 拦截弹状态注入本地队列 (制导雷达界面轮询)
    tp_->subscribe("broker/interceptors",
                   [this](const void* data, size_t size) {
                       Reader r{static_cast<const uint8_t*>(data), size};
                       uint32_t n = r.get<uint32_t>();
                       std::vector<InterceptorStatus> statuses;
                       statuses.reserve(n);
                       for (uint32_t i = 0; i < n && r.ok; ++i) {
                           InterceptorStatus s;
                           s.interceptor_id = r.get<uint32_t>();
                           s.name = r.getStr();
                           s.target_id = r.get<uint32_t>();
                           s.launched = r.get<uint8_t>() != 0;
                           s.in_flight = r.get<uint8_t>() != 0;
                           s.impacted = r.get<uint8_t>() != 0;
                           s.target_destroyed = r.get<uint8_t>() != 0;
                           s.range_km = r.get<float>();
                           s.launch_time = r.get<double>();
                           s.sim_time = r.get<double>();
                           statuses.push_back(s);
                       }
                       if (r.ok && !statuses.empty())
                           SimDataBroker::instance().publishInterceptorStatusesLocal(statuses);
                   });

    running_ = true;
    thread_ = std::make_unique<std::thread>([this]() { mirrorLoop(); });
    spdlog::info("BrokerNet: display role started (heartbeat mirror active)");
}

void BrokerNet::mirrorLoop() {
    while(running_){
        EquipmentRegistry::instance().sweepStale(uint64_t(cfg_.timeout_ms));
        std::map<uint32_t,Lease> batch;{std::lock_guard lock(lease_mtx_);batch=owned_leases_;}
        if(!batch.empty()){
            json items=json::array();for(const auto& [id,l]:batch)items.push_back({{"equipment_id",id},{"token",l.token}});
            std::string response;const auto& first=batch.begin()->second;
            if(requestJson("broker/link/renew",json{{"client_id",first.client},{"scene_session_id",first.scene},{"leases",items}}.dump(),&response,cfg_.request_ms))try{
                const auto reply=json::parse(response);if(reply.value("ok",false)){const auto valid=reply.at("valid").get<std::set<uint32_t>>();std::lock_guard lock(lease_mtx_);for(const auto& [id,l]:batch){auto found=owned_leases_.find(id);if(found!=owned_leases_.end()&&found->second.token==l.token){if(valid.count(id))found->second.expires=flightMonotonicMs()+reply.at("lease_ms").get<int>();else found->second.expires=0;}}}
            }catch(const std::exception&){}
        }
        std::unique_lock wait(lease_mtx_);int delay=500;for(const auto& [id,l]:owned_leases_)delay=std::min(delay,int(std::clamp<int64_t>((l.expires-flightMonotonicMs())/4,50,500)));lease_cv_.wait_for(wait,std::chrono::milliseconds(delay));
    }
}

void BrokerNet::handleHeartbeat(const void* data, size_t size)
{
    try {
        json j = json::parse(static_cast<const char*>(data), static_cast<const char*>(data) + size);
        std::lock_guard serial(heartbeat_mtx_);
        const auto order=j.value("snapshot_order_ns",uint64_t(0));
        if(order && order<=last_snapshot_order_ns_)return;
        const std::string host = j.value("host", std::string("sim"));

        SimMeta m;
        m.online = true;
        m.scene = j.value("scene", std::string());
        m.sim_time = j.value("sim_time", 0.0);
        m.fps = j.value("fps", 0.0);
        m.running = j.value("running", false);
        m.session_id=j.value("session_id",std::string());
        m.scene_session_id=j.value("scene_session_id",m.session_id);
        m.flight_control_available=j.value("flight_control_available",false);
        if(j.contains("platforms") && j["platforms"].is_array()) m.platforms_json=j["platforms"].dump();
        if(j.contains("earth")) {
            m.earth_json=earthContextText(j["earth"]);
        }

        std::vector<EquipmentInfo> list;
        for (const auto& je : j.value("equipment", json::array()))
            list.push_back(infoFromJson(je));

        EquipmentRegistry::instance().mergeRemote(list, host);

        {
            std::lock_guard<std::mutex> lock(flight_mtx_);
            if(j.contains("control_results") && j["control_results"].is_array() && j["control_results"].size()<=512)
                for(const auto& value:j["control_results"]) {
                    try {
                        const auto result=value.get<FlightControlResult>();
                        const auto previous=flight_results_.find(result.request_id);
                        // Only reconcile requests created by this display. Another
                        // operator cannot overwrite this process's pending state.
                        if(previous!=flight_results_.end() && previous->second.client_id==result.client_id &&
                           previous->second.session_id==result.session_id)storeFlightResultLocked(result);
                    }catch(const std::exception&){}
                }
            for(auto& [id,result]:flight_results_)
                if((result.stage=="queued"||result.stage=="unknown") && result.session_id!=m.session_id) {
                    result.stage="cancelled";result.code="session_changed";result.message="Simulation session changed";
                }
        }
        std::lock_guard<std::mutex> lock(disp_mtx_);
        if(!meta_.scene_session_id.empty() && meta_.scene_session_id!=m.scene_session_id)
            {for(const auto& e:list)SimDataBroker::instance().resetEquipmentChannel(e.equipment_id);std::lock_guard leases(lease_mtx_);owned_leases_.clear();}
        meta_ = m;
        if(order)last_snapshot_order_ns_=order;
        last_heartbeat_ms_ = epochMs();
    } catch (const std::exception& e) {
        spdlog::debug("BrokerNet: heartbeat parse failed: {}", e.what());
    }
}

bool BrokerNet::pullSnapshot(int timeout_ms)
{
    if (!tp_) return false;
    std::string resp;
    if (!requestJson("broker/registry/query", "{}", &resp,
                     timeout_ms > 0 ? timeout_ms : cfg_.request_ms))
        return false;
    if (resp.find("\"proto\"") == std::string::npos)
        return false;   // 响应不是有效快照 (空/异构进程应答)
    // 走心跳解析路径合并 (同格式)
    std::vector<uint8_t> buf(resp.begin(), resp.end());
    handleHeartbeat(buf.data(), buf.size());
    return true;
}

bool BrokerNet::requestJson(const std::string& service,
                            const std::string& req_json,
                            std::string* resp_json, int timeout_ms)
{
    if (!tp_) return false;
    std::vector<uint8_t> resp;
    if (!tp_->request(service, req_json.data(), req_json.size(), resp, timeout_ms))
        return false;
    if (resp_json)
        resp_json->assign(resp.begin(), resp.end());
    return true;
}

bool BrokerNet::linkAttach(uint32_t id,bool receiveFrames) {
    if(!tp_){bool changed;{std::lock_guard lock(sim_mtx_);changed=links_.insert(id).second;}if(changed)notifyGate();EquipmentRegistry::instance().setSubscribed(id,true);return !receiveFrames||SimDataBroker::instance().subscribe(id);}
    const auto meta=lastSimMeta();if(!meta.online||meta.scene_session_id.empty())return false;
    const auto client=flightClientId();const json req={{"equipment_id",id},{"client_id",client},{"scene_session_id",meta.scene_session_id},{"receive_frames",receiveFrames},{"task_type",id==0?"world":"equipment"}};
    std::string response;if(!requestJson("broker/link/attach",req.dump(),&response,cfg_.request_ms))return false;
    try{const auto reply=json::parse(response);if(!reply.value("ok",false)||reply.at("scene_session_id")!=meta.scene_session_id)return false;
        Lease lease{client,reply.at("token").get<std::string>(),meta.scene_session_id,flightMonotonicMs()+reply.at("lease_ms").get<int>(),receiveFrames};if(lease.token.empty())return false;
        {std::lock_guard lock(lease_mtx_);owned_leases_[id]=lease;}
        lease_cv_.notify_one();
        const auto topic="broker/frame/"+std::to_string(id);
        if(receiveFrames){std::lock_guard lock(disp_mtx_);if(!frame_topics_.count(topic)){tp_->subscribe(topic,[this,topic](const void* data,size_t size){handleFrameTopic(topic,data,size);});frame_topics_.insert(topic);}}
        return !receiveFrames||SimDataBroker::instance().subscribe(id);
    }catch(const std::exception&){return false;}
}
bool BrokerNet::linkDetach(uint32_t id) {
    Lease lease;bool owned=false;{std::lock_guard lock(lease_mtx_);auto found=owned_leases_.find(id);if(found!=owned_leases_.end()){lease=found->second;owned_leases_.erase(found);owned=true;}}
    bool ok=true;if(tp_&&owned){std::string response;ok=requestJson("broker/link/detach",json{{"equipment_id",id},{"client_id",lease.client},{"token",lease.token},{"scene_session_id",lease.scene}}.dump(),&response,cfg_.request_ms);if(ok)try{ok=json::parse(response).value("ok",false);}catch(...){ok=false;}}
    if(tp_){const auto topic="broker/frame/"+std::to_string(id);std::lock_guard lock(disp_mtx_);if(frame_topics_.erase(topic))tp_->unsubscribe(topic);}
    else {std::lock_guard lock(sim_mtx_);links_.erase(id);}
    SimDataBroker::instance().unsubscribe(id);if(!tp_)notifyGate();return ok;
}
bool BrokerNet::leaseActive(uint32_t id) const {if(!tp_)return true;std::lock_guard lock(lease_mtx_);auto it=owned_leases_.find(id);return it!=owned_leases_.end()&&it->second.expires>flightMonotonicMs();}
bool BrokerNet::hasTaskDemand() const {std::lock_guard lock(sim_mtx_);const auto now=flightMonotonicMs();for(const auto& [id,clients]:leases_)for(const auto& [client,lease]:clients)if(lease.expires>now)return true;return false;}
json BrokerNet::subscriptionStatus() const {
    json out=json::array();if(role_==Role::Sim){std::lock_guard lock(sim_mtx_);for(const auto& [id,clients]:leases_){size_t data=0,binding=0;for(const auto& [client,l]:clients)if(l.data)++data;else ++binding;out.push_back({{"id",id},{"data_subscribers",data},{"binding_subscribers",binding}});}}
    else {std::lock_guard lock(lease_mtx_);const auto* transport=dynamic_cast<const ipc::ShmTransport*>(tp_.get());for(const auto& [id,l]:owned_leases_)out.push_back({{"id",id},{"data",l.data},{"active",l.expires>flightMonotonicMs()},{"scene",l.scene},{"frame_wire_bytes",transport?transport->receivedBytes("broker/frame/"+std::to_string(id)):0}});}return out;
}
json BrokerNet::transportStatistics() const {
    json out=json::array();std::lock_guard lock(transport_stats_mtx_);for(const auto& [id,values]:receive_ms_){if(values.empty())continue;auto sorted=std::vector<double>(values.begin(),values.end());std::sort(sorted.begin(),sorted.end());out.push_back({{"id",id},{"samples",sorted.size()},{"p50_ms",sorted[(sorted.size()-1)/2]},{"p95_ms",sorted[size_t((sorted.size()-1)*.95)]}});}return out;
}

void BrokerNet::handleFrameTopic(const std::string& topic,
                                 const void* data, size_t size)
{
    (void)topic;
    Reader r{static_cast<const uint8_t*>(data), size};
    const uint32_t type_i = r.get<uint32_t>();
    const uint32_t sid = r.get<uint32_t>();
    const auto bodyLength=r.get<uint32_t>();
    if (!r.ok || sid==0 || bodyLength>r.n-r.off) return;
    const auto trailer=r.n-r.off-bodyLength;
    uint64_t revision=0;
    uint64_t sentNs=0;
    if(trailer>=sizeof(revision)) {
        Reader tail{r.p+r.off+bodyLength,trailer};revision=tail.get<uint64_t>();
        if(trailer>sizeof(revision)) {
            const auto session=tail.getStr();if(tail.ok&&tail.n-tail.off==sizeof(uint64_t))sentNs=tail.get<uint64_t>();if(!tail.ok||tail.off!=tail.n||session.size()>256)return;
            std::lock_guard<std::mutex> lock(disp_mtx_);if(session!=meta_.scene_session_id)return;
        }
    }
    else if(trailer!=0) return;
    r.n=r.off+bodyLength;

    if(topic!="broker/frame/"+std::to_string(sid))return;
    {std::lock_guard lock(lease_mtx_);auto lease=owned_leases_.find(sid);if(lease==owned_leases_.end()||!lease->second.data||lease->second.expires<=flightMonotonicMs())return;}
    auto& broker = SimDataBroker::instance();
    bool accepted=false;
    switch (SensorType(type_i)) {
    case SensorType::SONAR: {
        const auto version=r.get<uint8_t>();
        if(version!=1&&version!=2)break;
        SonarFrame f;
        f.sensor_id=sid;f.surface_revision=revision;
        f.platform_id=r.get<uint32_t>();f.frame_id=r.get<uint64_t>();f.tick=r.get<uint64_t>();
        f.sim_time=r.get<double>();f.range_limit_m=r.get<double>();f.transducer_draft_m=r.get<double>();
        f.clearance_m=r.get<double>();f.water_depth_m=r.get<double>();f.two_way_ms=r.get<double>();
        f.bottom=BottomReturn(r.get<uint8_t>());
        if(version==2){
            f.water_column_enabled=true;
            f.echo_range_m=r.get<double>();
        }
        if(r.ok&&r.off==r.n)accepted=broker.publishSonar(f);
        break;
    }
    case SensorType::RADAR: {
        RadarFrame f = deserializeRadar(r);
        f.surface_revision=revision;
        if (r.ok&&r.off==r.n&&f.sensor_id==sid) accepted=broker.publishRadar(f);
        break;
    }
    case SensorType::SAR: {
        SarFrame f = deserializeSar(r);
        f.surface_revision=revision;
        if (r.ok&&r.off==r.n&&f.sensor_id==sid) accepted=broker.publishSar(f);
        break;
    }
    case SensorType::IR: {
        IrFrame f = deserializeIr(r);
        f.surface_revision=revision;
        if (r.ok&&r.off==r.n&&f.sensor_id==sid) accepted=broker.publishIr(f);
        break;
    }
    case SensorType::VIS: {
        VisFrame f = deserializeVis(r);
        f.surface_revision=revision;
        if (r.ok&&r.off==r.n&&f.sensor_id==sid) accepted=broker.publishVis(f);
        break;
    }
    default:
        break;
    }
    if(accepted&&sentNs){const uint64_t now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();if(now>=sentNs){std::lock_guard lock(transport_stats_mtx_);auto& values=receive_ms_[sid];values.push_back(double(now-sentNs)*1e-6);if(values.size()>600)values.pop_front();}}
}

BrokerNet::SimMeta BrokerNet::lastSimMeta() const
{
    if(role_==Role::Sim) {
        SimMeta result;
        {std::lock_guard<std::mutex> lock(sim_mtx_);result.scene=scene_name_;result.sim_time=sim_time_;result.fps=sim_fps_;result.running=sim_running_;result.platforms_json=platforms_json_;result.earth_json=earth_json_;}
        {std::lock_guard<std::mutex> lock(flight_mtx_);result.session_id=flight_session_id_;result.scene_session_id=scene_session_id_;result.flight_control_available=flight_available_;}
        result.online=SimDataBroker::instance().isServerOnline();return result;
    }
    std::lock_guard<std::mutex> lock(disp_mtx_);
    auto snapshot=meta_;
    snapshot.online=last_heartbeat_ms_>0 && epochMs()-last_heartbeat_ms_<cfg_.timeout_ms;
    return snapshot;
}

bool BrokerNet::simOnline() const
{
    if (role_ == Role::Sim) return true;
    if (role_ != Role::Display) return false;
    if (!tp_) return false;
    std::lock_guard<std::mutex> lock(disp_mtx_);
    return last_heartbeat_ms_ > 0 &&
           epochMs() - last_heartbeat_ms_ < cfg_.timeout_ms;
}

// =============================================================================
// 通用 (控制面客户端路由)
// =============================================================================
void BrokerNet::sendControl(const ControlCommand& cmd)
{
    // Remote commands are confirmed by simulation heartbeats. An unacknowledged
    // local power mirror could otherwise suppress valid incoming frames forever.
    if (role_ != Role::Display || !tp_) {
        SimDataBroker::instance().sendControl(cmd); return;
    }
    json j;
    j["type"] = int(cmd.type);
    j["equipment_type"] = int(cmd.equipment_type);
    j["equipment_id"] = cmd.equipment_id;
    j["param_f"] = cmd.param_f;
    j["param_u"] = cmd.param_u;
    std::string resp;
    if (!requestJson("broker/control", j.dump(), &resp, cfg_.request_ms))
        spdlog::warn("BrokerNet: control cmd to equipment {} failed (sim offline?)",
                     cmd.equipment_id);
    else {
        const auto answer=json::parse(resp,nullptr,false);
        if(answer.is_discarded() || !answer.is_object() || !answer.value("ok",false))
            spdlog::warn("BrokerNet: simulation rejected control for equipment {}",cmd.equipment_id);
    }
}

void BrokerNet::sendEngagement(const EngagementRequest& req)
{
    if (role_ == Role::Display && tp_) {
        json j;
        j["fire"] = req.fire;
        j["target_x"] = req.target_x;
        j["target_y"] = req.target_y;
        j["target_z"] = req.target_z;
        j["gate_m"] = req.gate_m;
        j["source_equipment_id"] = req.source_equipment_id;
        std::string resp;
        if (!requestJson("broker/engagement", j.dump(), &resp, cfg_.request_ms)) {
            spdlog::warn("BrokerNet: engagement request failed (sim offline?)");
            return;
        }
        return;
    }
    SimDataBroker::instance().sendEngagement(req);
}


void BrokerNet::storeFlightResultLocked(const FlightControlResult& result)
{
    auto existing=flight_results_.find(result.request_id);
    if(existing==flight_results_.end())flight_result_order_.push_back(result.request_id);
    else {
        const auto& old=existing->second;
        const bool terminal=old.stage=="applied"||old.stage=="rejected"||old.stage=="cancelled"||old.stage=="expired";
        if(terminal&&(result.stage=="queued"||result.stage=="unknown"))return;
    }
    flight_results_[result.request_id]=result;
    while(flight_result_order_.size()>512) {
        const auto id=flight_result_order_.front();flight_result_order_.pop_front();
        flight_results_.erase(id);flight_payloads_.erase(id);flight_enqueued_ms_.erase(id);flight_inflight_.erase(id);
    }
}

std::string BrokerNet::flightClientId() const
{
    std::lock_guard<std::mutex> lock(flight_mtx_);
    // Initialized lazily so disabled transport and embedded use still receive
    // the same per-process identity as full IPC deployments.
    if(flight_client_id_.empty())const_cast<BrokerNet*>(this)->flight_client_id_=flightTokenId("display");
    return flight_client_id_;
}
std::string BrokerNet::flightSessionId() const
{
    if(role_==Role::Display){std::lock_guard<std::mutex> lock(disp_mtx_);return meta_.session_id;}
    std::lock_guard<std::mutex> lock(flight_mtx_);return flight_session_id_;
}
void BrokerNet::resetFlightControlSession()
{
    std::lock_guard<std::mutex> lock(flight_mtx_);
    flight_session_id_=flightTokenId("session");
    flight_pending_.clear();flight_outgoing_.clear();flight_inflight_.clear();
    for(auto& [id,result]:flight_results_)if(result.stage=="queued"||result.stage=="unknown") {
        result.stage="cancelled";result.code="session_changed";result.message="Simulation session changed before execution";
    }
}
void BrokerNet::setFlightControlAvailable(bool available)
{
    std::lock_guard<std::mutex> lock(flight_mtx_);
    if(flight_available_&&!available)flight_session_id_=flightTokenId("session");
    flight_available_=available;
    if(available)return;
    flight_pending_.clear();flight_outgoing_.clear();flight_inflight_.clear();
    for(auto& [id,result]:flight_results_)if(result.stage=="queued"||result.stage=="unknown") {
        result.stage="cancelled";result.code="control_offline";result.message="Flight control service unavailable";
    }
}
FlightControlResult BrokerNet::acceptFlightControl(const FlightControlCommand& command)
{
    const auto invalid=validateFlightControl(command);
    if(!invalid.empty())return flightResult(command,"rejected",invalid,"Invalid flight command");
    const auto payload=json(command).dump();
    std::lock_guard<std::mutex> lock(flight_mtx_);
    const auto previous=flight_payloads_.find(command.request_id);
    if(previous!=flight_payloads_.end()) {
        if(previous->second!=payload)return flightResult(command,"rejected","request_conflict","Request ID already identifies a different command");
        return flight_results_.at(command.request_id);
    }
    auto result=flightResult(command,"queued","accepted","Awaiting simulation worker execution");
    if(command.session_id!=flight_session_id_)result=flightResult(command,"rejected","stale_session","Simulation session does not match");
    else if(!flight_available_||!SimDataBroker::instance().isServerOnline())result=flightResult(command,"rejected","control_offline","Flight control service unavailable");
    else if(flight_pending_.size()>=64)result=flightResult(command,"rejected","queue_full","Flight execution queue is full");
    else {flight_pending_.push_back(command);flight_enqueued_ms_[command.request_id]=flightMonotonicMs();}
    flight_payloads_[command.request_id]=payload;storeFlightResultLocked(result);return result;
}
std::string BrokerNet::submitFlightControl(FlightControlCommand command)
{
    if(command.request_id.empty())command.request_id=flightTokenId("request");
    if(command.client_id.empty())command.client_id=flightClientId();
    if(command.session_id.empty())command.session_id=flightSessionId();
    const auto invalid=validateFlightControl(command);
    if(!invalid.empty()) {
        std::lock_guard<std::mutex> lock(flight_mtx_);
        storeFlightResultLocked(flightResult(command,"rejected",invalid,"Invalid flight command"));return command.request_id;
    }
    if(role_!=Role::Display) {
        const auto result=acceptFlightControl(command);(void)result;return command.request_id;
    }
    const auto meta=lastSimMeta();
    std::lock_guard<std::mutex> lock(flight_mtx_);
    // Prevent a caller from replacing a tracked request with a different payload.
    const auto payload=json(command).dump();
    auto previous=flight_payloads_.find(command.request_id);
    if(previous!=flight_payloads_.end()) {
        if(previous->second!=payload)storeFlightResultLocked(flightResult(command,"rejected","request_conflict","Request ID already used"));
        return command.request_id;
    }
    flight_payloads_[command.request_id]=payload;
    auto result=flightResult(command,"queued","sending","Sending to simulation");
    if(!tp_||!meta.online||!meta.flight_control_available||!flight_send_running_)
        result=flightResult(command,"rejected","control_offline","Simulation flight control is offline");
    else if(command.session_id!=meta.session_id)result=flightResult(command,"rejected","stale_session","Simulation session changed");
    else if(flight_outgoing_.size()>=32)result=flightResult(command,"rejected","queue_full","Flight transmission queue is full");
    else {flight_outgoing_.push_back(command);flight_enqueued_ms_[command.request_id]=flightMonotonicMs();flight_cv_.notify_one();}
    storeFlightResultLocked(result);return command.request_id;
}
bool BrokerNet::pollFlightControl(FlightControlCommand* command)
{
    if(!command)return false;
    std::lock_guard<std::mutex> lock(flight_mtx_);
    while(!flight_pending_.empty()) {
        auto next=flight_pending_.front();flight_pending_.pop_front();
        auto result=flight_results_.find(next.request_id);
        if(result==flight_results_.end()||result->second.stage!="queued")continue;
        if(next.session_id!=flight_session_id_||!flight_available_||!SimDataBroker::instance().isServerOnline()) {
            storeFlightResultLocked(flightResult(next,"cancelled","control_offline","Command cancelled before execution"));continue;
        }
        if(flightMonotonicMs()-flight_enqueued_ms_[next.request_id]>1500) {
            storeFlightResultLocked(flightResult(next,"expired","execution_timeout","Command exceeded execution deadline"));continue;
        }
        flight_inflight_.insert(next.request_id);*command=std::move(next);return true;
    }
    return false;
}
void BrokerNet::completeFlightControl(const FlightControlResult& result)
{
    std::lock_guard<std::mutex> lock(flight_mtx_);
    const auto previous=flight_results_.find(result.request_id);
    if(!flight_inflight_.count(result.request_id)||previous==flight_results_.end()||previous->second.stage!="queued"||
       previous->second.client_id!=result.client_id||previous->second.session_id!=result.session_id||previous->second.platform_id!=result.platform_id)return;
    if(result.session_id!=flight_session_id_||!flight_available_)return;
    if((result.stage!="applied"&&result.stage!="rejected"&&result.stage!="expired")||!std::isfinite(result.sim_time))return;
    flight_inflight_.erase(result.request_id);storeFlightResultLocked(result);
}
bool BrokerNet::flightControlResult(const std::string& id,FlightControlResult* out) const
{
    std::lock_guard<std::mutex> lock(flight_mtx_);const auto found=flight_results_.find(id);
    if(!out||found==flight_results_.end())return false;*out=found->second;return true;
}
void BrokerNet::flightSendLoop()
{
    for(;;) {
        FlightControlCommand command;
        {
            std::unique_lock<std::mutex> lock(flight_mtx_);
            flight_cv_.wait(lock,[this]{return !flight_send_running_||!flight_outgoing_.empty();});
            if(!flight_send_running_)break;
            command=std::move(flight_outgoing_.front());flight_outgoing_.pop_front();
            const auto queued=flight_enqueued_ms_.find(command.request_id);
            if(queued==flight_enqueued_ms_.end()||flightMonotonicMs()-queued->second>1500) {
                storeFlightResultLocked(flightResult(command,"expired","send_timeout","Command expired before transmission"));continue;
            }
        }
        std::string response;auto result=flightResult(command,"unknown","transport_timeout","Delivery or execution is unknown; await heartbeat reconciliation");
        const auto payload=json(command).dump();
        bool received=requestJson("broker/flight/control",payload,&response,cfg_.request_ms);
        // A displayed pose is idempotent. Reuse its request ID so the server
        // can reconcile a lost reply without applying a second revision.
        if(!received&&command.presentation_pose)
            received=requestJson("broker/flight/control",payload,&response,cfg_.request_ms);
        if(received) {
            try {
                auto reply=json::parse(response);
                if(reply.contains("result")) {
                    auto actual=reply["result"].get<FlightControlResult>();
                    if(actual.request_id!=command.request_id||actual.client_id!=command.client_id||actual.session_id!=command.session_id||actual.platform_id!=command.platform_id)
                        throw std::invalid_argument("flight result identity mismatch");
                    result=std::move(actual);
                } else result=flightResult(command,"rejected","protocol_rejected",reply.value("error",std::string("Invalid flight response")));
            } catch(const std::exception&) {result=flightResult(command,"unknown","invalid_reply","Malformed simulation response; await heartbeat reconciliation");}
        }
        std::lock_guard<std::mutex> lock(flight_mtx_);storeFlightResultLocked(result);
    }
}

// =============================================================================
// 停止
// =============================================================================
void BrokerNet::stop()
{
    setEarthContext("{}");
    setFlightControlAvailable(false);
    if(flight_send_running_.exchange(false)) {
        flight_cv_.notify_all();
        if(flight_thread_&&flight_thread_->joinable())flight_thread_->join();
        flight_thread_.reset();
    }
    if (!running_.exchange(false)) {
        role_ = Role::None;
        return;
    }
    lease_cv_.notify_all();
    // 先停转发线程并排空队列 (此时 tp_ 仍存活)
    if (fwd_running_.exchange(false)) {
        fwd_cv_.notify_all();
        if (fwd_thread_ && fwd_thread_->joinable()) fwd_thread_->join();
        fwd_thread_.reset();
        std::deque<std::pair<std::string, std::vector<uint8_t>>> rest;
        {
            std::lock_guard<std::mutex> lk(fwd_mtx_);
            rest.swap(fwd_queue_);
        }
        if (tp_)
            for (auto& item : rest)
                tp_->publish(item.first, item.second.data(), item.second.size());
        if (fwd_dropped_ > 0)
            spdlog::info("BrokerNet: async forward dropped {} stale frames in session",
                         fwd_dropped_);
        fwd_dropped_ = 0;
    }
    if (thread_ && thread_->joinable())
        thread_->join();
    thread_.reset();

    SimDataBroker::instance().setInterceptorTap(nullptr);
    SimDataBroker::instance().setFrameTap(nullptr, nullptr, nullptr, nullptr);
    SimDataBroker::instance().setSonarTap(nullptr);

    if(role_==Role::Display){std::vector<uint32_t> ids;{std::lock_guard lock(lease_mtx_);for(const auto& [id,l]:owned_leases_)ids.push_back(id);}for(auto id:ids)linkDetach(id);}
    if (tp_) {
        tp_->stop();
        tp_.reset();
    }
    role_ = Role::None;
}

} // namespace vsgraysim
