#include "Deployment.h"
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <regex>

namespace scene_deployment {
namespace {
const Json schema = Json::parse(R"({
  "add_target": {"required":["id","model","position"],"optional":["name"]},
  "place": {"required":["object","position"],"optional":[]},
  "route": {"required":["object","points","speed_mps"],"optional":[]},
  "attach_sensor": {"required":["object","profile"],"optional":["name"]},
  "material": {"required":["object"],"optional":["preset","parameters","region"]},
  "weather": {"required":["preset"],"optional":[]}
})");

void require(bool valid, const char* message) {
    if (!valid) throw std::invalid_argument(message);
}

bool numeric(const Json& value, double min, double max) {
    if (!value.is_number()) return false;
    const double number = value.get<double>();
    return std::isfinite(number) && number >= min && number <= max;
}

void identifier(const Json& value) {
    require(value.is_string(), "expected an identifier");
    const auto& id = value.get_ref<const std::string&>();
    require(!id.empty() && id.size() <= 96, "identifier length must be 1..96");
    for (const unsigned char c : id)
        require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '_' || c == '-' || c == '.', "invalid identifier character");
}

void vector(const Json& value, size_t size) {
    require(value.is_array() && value.size() == size, "invalid coordinate or color vector");
    for (const auto& component : value)
        require(numeric(component, -1e7, 1e7), "invalid coordinate or color component");
}

void position(const Json& value) {
    require(value.is_object(), "position must be an object");
    unsigned int coordinates = 0;
    for (const auto& [key, field] : value.items()) {
        if (key == "agl_m") {
            require(numeric(field, 0., 30000.), "agl_m must be 0..30000 metres");
        } else {
            require(key == "local_m" || key == "relative_m" || key == "wgs84", "unknown position coordinate");
            vector(field, 3);
            if (key == "wgs84")
                require(numeric(field[0], -90., 90.) && numeric(field[1], -180., 180.), "WGS84 latitude or longitude out of range");
            ++coordinates;
        }
    }
    require(coordinates == 1, "provide one of local_m, relative_m, or wgs84");
}

void parameters(const Json& value) {
    const std::set<std::string> fields = {"color_rgba", "temperature_k", "eps_mwir", "eps_lwir",
        "emissivity", "rho_tv", "rho_swir", "radar"};
    const std::set<std::string> radarFields = {"rcs_scale", "dielectric_real", "dielectric_imag", "sigma0_db", "surface_rough", "dielectric_model"};
    require(value.is_object() && !value.empty(), "parameters must be a nonempty object");
    for (const auto& [key, field] : value.items()) {
        require(fields.count(key), "unsupported material parameter");
        if (key == "color_rgba") {
            vector(field, 4);
            for (const auto& component : field) require(numeric(component, 0., 1.), "color component must be 0..1");
        } else if (key == "radar") {
            require(field.is_object() && !field.empty(), "radar parameters must be a nonempty object");
            for (const auto& [name, number] : field.items()) {
                require(radarFields.count(name), "unknown radar material parameter");
                if (name == "sigma0_db") require(numeric(number, -120., 120.), "sigma0_db must be -120..120 dB");
                else if (name == "surface_rough") require(numeric(number, 0., 1.), "surface_rough must be 0..1");
                else if (name == "dielectric_model") require(number.is_number_integer() && numeric(number, 0., 3.), "dielectric_model must be an integer from 0..3");
                else require(numeric(number, name == "dielectric_real" ? 1. : 0., 1e8), "invalid radar material parameter");
            }
        } else if (key == "temperature_k") {
            require(numeric(field, 1., 5000.), "temperature outside 1..5000 K");
        } else {
            require(numeric(field, 0., 1.), "spectral fraction must be 0..1");
        }
    }
}
}

Json validate(const Json& plan) {
    require(plan.is_object() && plan.size() == 2 && plan.value("format", std::string{}) == format &&
            plan.contains("actions"), "expected scene-deployment-v1 with actions");
    const auto& actions = plan.at("actions");
    require(actions.is_array() && !actions.empty() && actions.size() <= 64, "provide 1..64 actions");
    for (size_t i = 0; i < actions.size(); ++i) {
        try {
            const auto& action = actions[i];
            require(action.is_object() && action.contains("op"), "action requires op");
            const auto op = action.at("op").get<std::string>();
            require(schema.contains(op), "unknown action");
            const auto& fields = schema.at(op);
            std::set<std::string> allowed{"op"};
            for (const auto& required : fields.at("required")) {
                const auto name = required.get<std::string>();
                require(action.contains(name), ("missing " + name).c_str());
                allowed.insert(name);
            }
            for (const auto& optional : fields.at("optional")) allowed.insert(optional.get<std::string>());
            for (const auto& [key, value] : action.items()) {
                require(allowed.count(key), ("unsupported field " + key).c_str());
                if (key == "id" || key == "model" || key == "object" || key == "profile" || key == "preset") identifier(value);
                if (key == "name") require(value.is_string() && !value.get_ref<const std::string&>().empty() &&
                    value.get_ref<const std::string&>().size() <= 128, "name must be 1..128 UTF-8 bytes");
            }
            if (action.contains("position")) position(action.at("position"));
            if (op == "route") {
                require(numeric(action.at("speed_mps"), 0.1, 350.), "speed must be 0.1..350 m/s");
                const auto& points = action.at("points");
                require(points.is_array() && points.size() >= 2 && points.size() <= 64, "route requires 2..64 points");
                for (const auto& point : points) position(point);
            }
            if (op == "material") {
                require(action.contains("preset") || action.contains("parameters"), "material requires preset or parameters");
                if (action.contains("parameters")) parameters(action.at("parameters"));
                if (action.contains("region")) {
                    const auto& region = action.at("region");
                    require(region.is_object() && region.size() == 2, "region requires min_m/max_m or center/size_m");
                    if(region.contains("center")) {
                        position(region.at("center"));
                        const auto& size=region.at("size_m");
                        vector(size,3);
                        for(const auto& extent:size) require(extent.get<double>()>0.,"region size must be positive");
                    } else {
                        vector(region.at("min_m"),3);
                        vector(region.at("max_m"),3);
                        for(size_t axis=0;axis<3;++axis)
                            require(region.at("min_m")[axis].get<double>()<region.at("max_m")[axis].get<double>(),"region bounds must be increasing");
                    }
                }
            }
        } catch (const std::exception& error) {
            throw std::invalid_argument("action " + std::to_string(i+1) + " — " + error.what());
        }
    }
    return plan;
}

Json parse(std::string_view response) {
    require(!response.empty() && response.size() <= maxResponseBytes, "empty or oversized deployment plan");
    return validate(Json::parse(response));
}

std::optional<Json> simplePlan(std::string_view instruction, const std::string& selected) {
    std::string text(instruction);
    for (const auto& [wide, ascii] : {std::pair{"（","("},std::pair{"）",")"},std::pair{"，",","},std::pair{"。","."}}) {
        size_t index = 0;
        while ((index = text.find(wide,index)) != text.npos) {
            text.replace(index,std::char_traits<char>::length(wide),ascii);
            ++index;
        }
    }
    static const std::string coordinates = R"(\s*\(\s*([-+]?[0-9]+(?:\.[0-9]+)?)\s*,\s*([-+]?[0-9]+(?:\.[0-9]+)?)\s*,\s*([-+]?[0-9]+(?:\.[0-9]+)?)\s*\)\s*(?:米|m)?\.?\s*$)";
    static const std::regex add(R"(^\s*添加\s*([a-z0-9-]+)(?:\s*编号\s*([a-z0-9-]+))?\s*到)" + coordinates);
    static const std::regex move(R"(^\s*(?:把选中目标)?(?:移动到|移到|放到|设为))" + coordinates);
    std::smatch match;
    Json action;
    int coordinateIndex = 1;
    if (std::regex_match(text,match,add)) {
        action = {{"op","add_target"},{"model",match[1].str()},
                  {"id",match[2].matched ? match[2].str() : match[1].str()}};
        coordinateIndex = 3;
    } else if (std::regex_match(text,match,move)) action = {{"op","place"},{"object",selected}};
    else return std::nullopt;
    action["position"] = {{"local_m",{std::stod(match[coordinateIndex].str()),
        std::stod(match[coordinateIndex+1].str()),std::stod(match[coordinateIndex+2].str())}}};
    return validate({{"format",format},{"actions",Json::array({std::move(action)})}});
}

Json modelRequest(std::string_view model,std::string_view instruction,const Json& context) {
    require(!model.empty() && model.size() <= 256, "model identifier must be 1..256 UTF-8 bytes");
    require(!instruction.empty() && instruction.size() <= 8192, "instruction must be 1..8192 UTF-8 bytes");
    const std::string system = R"(You are an offline scene editor. Emit one JSON object with "format":"scene-deployment-v1" and an "actions" array. Use only identifiers supplied by context for assets, profiles and presets. Existing objects must use their exact id. If the user asks about "here" or "this object", use the selected point or object. With missing details reply {"format":"scene-deployment-v1","clarification":"问题（使用用户的语言）"}. Scene coordinates are east/up/south in metres. A position has exactly one of local_m:[east,up,south], relative_m:[east,up,south] from the selected point, or wgs84:[latitude,longitude,ellipsoid altitude]. Optional agl_m sets height above terrain at that position. Route points include both endpoints; use speed_mps. Material region is an object-local box {min_m,max_m}, or {center:position,size_m:[width,height,length]} for a box centred on a scene position. Material parameters are color_rgba, temperature_k, emissivity, eps_mwir, eps_lwir, rho_tv, rho_swir, and radar (rcs_scale, dielectric_real, dielectric_imag, sigma0_db, surface_rough, dielectric_model). Plain example: {"format":"scene-deployment-v1","actions":[{"op":"weather","preset":"light_rain"}]}. Each action needs op and the following required fields, plus optional fields when relevant. )";
    const auto messages=Json::array({{{"role","system"},{"content",system + schema.dump()}},
        {{"role","user"},{"content",Json{{"instruction",instruction},{"context",context}}.dump()}}});
    return {{"model",model},{"messages",messages},{"stream",false},{"temperature",0.1},
        {"max_tokens",4096},{"response_format",{{"type","json_object"}}}};
}

Json modelPlan(std::string_view completion) {
    require(!completion.empty() && completion.size() <= 2 * maxResponseBytes, "empty or oversized model response");
    const auto envelope=Json::parse(completion);
    const auto& choice=envelope.at("choices").at(0);
    require(choice.value("finish_reason",std::string{})!="length","local model exhausted its output budget");
    const auto content=choice.at("message").at("content").get<std::string>();
    require(!content.empty() && content.size() <= maxResponseBytes, "empty or oversized deployment plan");
    const auto plan=Json::parse(content);
    if(plan.is_object()&&plan.size()==2&&plan.value("format",std::string{})==format&&plan.contains("clarification")) {
        const auto clarification=plan.at("clarification").get<std::string>();
        require(!clarification.empty()&&clarification.size()<=1024,"invalid clarification response");
        throw std::invalid_argument(clarification);
    }
    return validate(plan);
}
}
