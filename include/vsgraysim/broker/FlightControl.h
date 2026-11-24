#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace vsgraysim {
/// Complete actuator/setpoint snapshot. Inputs are normalized demand, not a
/// claim that a particular airframe supports calibrated surface deflections.
struct FlightControlCommand {
    std::string request_id, client_id, session_id;
    uint32_t platform_id=0;
    std::string mode="manual";
    double throttle=.5, elevator=0, aileron=0, rudder=0;
    double target_speed=0, target_altitude=0, target_heading=0;
    bool release=false;
};
struct FlightControlResult {
    std::string request_id,client_id,session_id;
    uint32_t platform_id=0;
    std::string stage="queued",code="queued",message;
    double sim_time=0;
    uint64_t revision=0;
};
inline bool flightToken(const std::string& value) {
    if(value.empty() || value.size()>128)return false;
    for(unsigned char c:value)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    return true;
}
inline std::string validateFlightControl(const FlightControlCommand& c) {
    if(!flightToken(c.request_id)||!flightToken(c.client_id)||!flightToken(c.session_id)||!c.platform_id)return "invalid_identity";
    if(c.mode!="manual"&&c.mode!="altitude_hold"&&c.mode!="route")return "unsupported_mode";
    auto range=[](double v,double low,double high){return std::isfinite(v)&&v>=low&&v<=high;};
    if(!range(c.throttle,0,1)||!range(c.elevator,-1,1)||!range(c.aileron,-1,1)||!range(c.rudder,-1,1)||
       !range(c.target_speed,0,2000)||!range(c.target_altitude,-1000,100000)||!range(c.target_heading,0,360))return "invalid_input";
    return {};
}
inline void to_json(nlohmann::json& j,const FlightControlCommand& c) {
    j={{"version",1},{"request_id",c.request_id},{"client_id",c.client_id},{"session_id",c.session_id},{"platform_id",c.platform_id},
       {"mode",c.mode},{"throttle",c.throttle},{"elevator",c.elevator},{"aileron",c.aileron},{"rudder",c.rudder},
       {"target_speed",c.target_speed},{"target_altitude",c.target_altitude},{"target_heading",c.target_heading},{"release",c.release}};
}
inline void from_json(const nlohmann::json& j,FlightControlCommand& c) {
    if(!j.is_object()||!j.contains("version")||!j["version"].is_number_integer()||j["version"]!=1)throw std::invalid_argument("unsupported flight control version");
    FlightControlCommand value;
    for(auto key:{"request_id","client_id","session_id","mode"})if(!j.contains(key)||!j[key].is_string())throw std::invalid_argument("flight control identity type");
    value.request_id=j["request_id"];value.client_id=j["client_id"];value.session_id=j["session_id"];value.mode=j["mode"];
    if(!j.contains("platform_id")||!j["platform_id"].is_number_integer()||j["platform_id"].get<double>()<1||j["platform_id"].get<double>()>UINT32_MAX)throw std::invalid_argument("flight platform ID");
    value.platform_id=j["platform_id"].get<uint32_t>();
    for(auto key:{"throttle","elevator","aileron","rudder","target_speed","target_altitude","target_heading"})
        if(!j.contains(key)||!j[key].is_number())throw std::invalid_argument("flight input type");
    value.throttle=j["throttle"];value.elevator=j["elevator"];value.aileron=j["aileron"];value.rudder=j["rudder"];
    value.target_speed=j["target_speed"];value.target_altitude=j["target_altitude"];value.target_heading=j["target_heading"];
    if(!j.contains("release")||!j["release"].is_boolean())throw std::invalid_argument("flight release type");
    value.release=j["release"];
    const auto error=validateFlightControl(value);if(!error.empty())throw std::invalid_argument(error);
    c=std::move(value);
}
inline void to_json(nlohmann::json& j,const FlightControlResult& r) {
    j={{"request_id",r.request_id},{"client_id",r.client_id},{"session_id",r.session_id},{"platform_id",r.platform_id},
       {"stage",r.stage},{"code",r.code},{"message",r.message},{"sim_time",r.sim_time},{"revision",r.revision}};
}
inline void from_json(const nlohmann::json& j,FlightControlResult& r) {
    FlightControlResult v;
    for(auto key:{"request_id","client_id","session_id","stage","code","message"})if(!j.contains(key)||!j[key].is_string())throw std::invalid_argument("invalid flight result string");
    v.request_id=j["request_id"];v.client_id=j["client_id"];v.session_id=j["session_id"];v.stage=j["stage"];v.code=j["code"];v.message=j["message"];
    if(!flightToken(v.request_id)||!flightToken(v.client_id)||!flightToken(v.session_id)||v.code.size()>128||v.message.size()>1024||
       (v.stage!="queued"&&v.stage!="applied"&&v.stage!="rejected"&&v.stage!="cancelled"&&v.stage!="unknown"&&v.stage!="expired"))throw std::invalid_argument("invalid flight result identity");
    if(!j.contains("platform_id")||!j["platform_id"].is_number_integer()||j["platform_id"].get<double>()<1||j["platform_id"].get<double>()>UINT32_MAX ||
       !j.contains("revision")||!j["revision"].is_number_unsigned()||!j.contains("sim_time")||!j["sim_time"].is_number()||!std::isfinite(j["sim_time"].get<double>()))throw std::invalid_argument("invalid flight result number");
    v.platform_id=j["platform_id"];v.revision=j["revision"];v.sim_time=j["sim_time"];r=std::move(v);
}
inline FlightControlResult flightResult(const FlightControlCommand& c,const std::string& stage,const std::string& code,const std::string& message={}) {
    FlightControlResult r;r.request_id=c.request_id;r.client_id=c.client_id;r.session_id=c.session_id;r.platform_id=c.platform_id;r.stage=stage;r.code=code;r.message=message;return r;
}
} // namespace vsgraysim
