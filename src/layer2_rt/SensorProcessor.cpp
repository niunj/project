#include "vsgraysim/layer2_rt/SensorProcessor.h"
#include <cmath>
#include <algorithm>

namespace vsgraysim {

namespace {

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
inline float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}
inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    if (len < 1e-9f) return {0,0,0};
    return {v.x/len, v.y/len, v.z/len};
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x+b.x, a.y+b.y, a.z+b.z};
}
inline Vec3 scale(const Vec3& v, float s) {
    return {v.x*s, v.y*s, v.z*s};
}

void buildFrame(const Vec3& dir, Vec3& right, Vec3& up) {
    Vec3 ref = {0,1,0};
    if (std::abs(dot(dir, ref)) > 0.99f) ref = {0,0,1};
    right = normalize(cross(dir, ref));
    up = normalize(cross(right, dir));
}

// ============================================================
// OpticalSensorProcessor  —  VIS / LASER
// ============================================================
class OpticalSensorProcessor : public SensorProcessor {
public:
    std::vector<Ray> emitRays(const Vec3& origin, const Vec3& direction,
                               float beam_width, uint32_t num_rays,
                               float, float, std::mt19937& rng) const override
    {
        std::vector<Ray> rays;
        rays.reserve(num_rays);
        Vec3 dir = normalize(direction);
        Vec3 right, up;
        buildFrame(dir, right, up);

        float sigma = beam_width * 0.5f;
        std::normal_distribution<float> gauss(0.0f, sigma>0 ? sigma : 1.0f);
        for (uint32_t i = 0; i < num_rays; ++i) {
            float tx = sigma>0 ? gauss(rng) : 0, ty = sigma>0 ? gauss(rng) : 0;
            rays.push_back({origin, normalize(add(add(dir, scale(right, tx)), scale(up, ty))), 1.0f});
        }
        return rays;
    }

    float computeSurfaceScatter(const Vec3& incident, const Vec3& normal,
                                 const GeoUnitPhysParams& params, float) const override
    {
        float NdotL = std::max(0.0f, dot(normal, scale(incident, -1.0f)));
        float albedo = params.vis_albedo;
        float diffuse = albedo * NdotL / 3.14159f;
        Vec3 half_vec = normalize(add(scale(incident, -1.0f), scale(incident, -1.0f)));
        float NdotH = std::max(0.0f, dot(normal, half_vec));
        float specular = std::pow(NdotH, 32.0f) * (1.0f - albedo);
        return diffuse + specular;
    }

    float computeReturnAmplitude(float tx_power, float brdf,
                                  float L_atm, float distance) const override
    {
        float R2 = std::max(1.0f, distance * distance);
        return tx_power * brdf * L_atm / R2;
    }
};

// ============================================================
// IrSensorProcessor  —  passive IR
// ============================================================
class IrSensorProcessor : public SensorProcessor {
public:
    std::vector<Ray> emitRays(const Vec3& origin, const Vec3& direction,
                               float beam_width, uint32_t num_rays,
                               float, float, std::mt19937& rng) const override
    {
        std::vector<Ray> rays;
        rays.reserve(num_rays);
        Vec3 dir = normalize(direction);
        Vec3 right, up;
        buildFrame(dir, right, up);

        float half = beam_width * 0.5f;
        std::uniform_real_distribution<float> uni(-half, half);
        for (uint32_t i = 0; i < num_rays; ++i) {
            float tx = uni(rng), ty = uni(rng);
            rays.push_back({origin, normalize(add(add(dir, scale(right, tx)), scale(up, ty))), 1.0f});
        }
        return rays;
    }

    float computeSurfaceScatter(const Vec3&, const Vec3&,
                                 const GeoUnitPhysParams& params, float) const override
    {
        float e = params.ir_emissivity[0];
        return std::clamp(e,0.0f,1.0f);
    }

    float computeReturnAmplitude(float, float emissivity,
                                  float L_atm, float) const override
    {
        return emissivity * L_atm;
    }
};

// ============================================================
// RadarSensorProcessor  —  active radar (fan beam)
// ============================================================
class RadarSensorProcessor : public SensorProcessor {
public:
    std::vector<Ray> emitRays(const Vec3& origin, const Vec3& direction,
                               float beam_width, uint32_t num_rays,
                               float scan_az, float scan_el,
                               std::mt19937& rng) const override
    {
        std::vector<Ray> rays;
        rays.reserve(num_rays);
        Vec3 dir = normalize(direction);
        Vec3 right, up;
        buildFrame(dir, right, up);

        float ca = std::cos(scan_az), sa = std::sin(scan_az);
        float ce = std::cos(scan_el), se = std::sin(scan_el);
        Vec3 scan_dir = normalize({dir.x*ca - dir.z*sa, dir.y + se, dir.x*sa + dir.z*ca});

        float az_spread = beam_width;
        float el_spread = beam_width * 0.1f;
        std::uniform_real_distribution<float> az_dist(-az_spread*0.5f, az_spread*0.5f);
        std::uniform_real_distribution<float> el_dist(-el_spread*0.5f, el_spread*0.5f);
        for (uint32_t i = 0; i < num_rays; ++i) {
            float da = az_dist(rng), de = el_dist(rng);
            rays.push_back({origin, normalize(add(add(scan_dir, scale(right, da)), scale(up, de))), 1.0f});
        }
        return rays;
    }

    float computeSurfaceScatter(const Vec3& incident, const Vec3& normal,
                                 const GeoUnitPhysParams& params, float) const override
    {
        float NdotI = std::abs(dot(normal, incident));
        if (NdotI < 0.01f) return 0.0f;
        float eps_r = params.radar_epsilon_real;
        float eps_i = params.radar_epsilon_imag;
        float reflection = std::sqrt(eps_r*eps_r + eps_i*eps_i);
        float rough = params.radar_surface_rough;
        return NdotI * reflection * std::exp(-4.0f * rough * rough) * params.rcs_modify_factor;
    }

    float computeReturnAmplitude(float tx_power, float rcs,
                                  float L_atm, float distance) const override
    {
        float R2 = std::max(1.0f, distance * distance);
        float R4 = R2 * R2;
        return tx_power * rcs * L_atm / (4.0f * 3.14159f * R4);
    }
};

// ============================================================
// SarSensorProcessor  —  synthetic aperture radar
// ============================================================
class SarSensorProcessor : public SensorProcessor {
public:
    std::vector<Ray> emitRays(const Vec3& origin, const Vec3& direction,
                               float, uint32_t num_rays,
                               float, float, std::mt19937&) const override
    {
        std::vector<Ray> rays;
        rays.reserve(num_rays);
        Vec3 dir = normalize(direction);
        Vec3 right, up;
        buildFrame(dir, right, up);

        float aperture_length = 10.0f;
        float spacing = aperture_length / num_rays;
        float mid = float(num_rays - 1) * 0.5f;
        for (uint32_t i = 0; i < num_rays; ++i) {
            float offset = (float(i) - mid) * spacing;
            rays.push_back({{origin.x + right.x*offset,
                             origin.y + right.y*offset,
                             origin.z + right.z*offset}, dir, 1.0f});
        }
        return rays;
    }

    float computeSurfaceScatter(const Vec3& incident, const Vec3& normal,
                                 const GeoUnitPhysParams& params, float) const override
    {
        float NdotI = std::abs(dot(normal, incident));
        if (NdotI < 0.01f) return 0.0f;
        float eps_r = params.radar_epsilon_real;
        float eps_i = params.radar_epsilon_imag;
        float reflection = std::sqrt(eps_r*eps_r + eps_i*eps_i);
        float rough = params.radar_surface_rough;
        return NdotI * reflection * std::exp(-4.0f * rough * rough) * params.rcs_modify_factor;
    }

    float computeReturnAmplitude(float tx_power, float rcs,
                                  float L_atm, float distance) const override
    {
        float R2 = std::max(1.0f, distance * distance);
        float R4 = R2 * R2;
        return tx_power * rcs * L_atm / (4.0f * 3.14159f * R4);
    }
};

} // anonymous namespace

std::unique_ptr<SensorProcessor> SensorProcessor::create(SensorType type) {
    switch (type) {
        case SensorType::VIS:   return std::make_unique<OpticalSensorProcessor>();
        case SensorType::LASER: return std::make_unique<OpticalSensorProcessor>();
        case SensorType::IR:    return std::make_unique<IrSensorProcessor>();
        case SensorType::RADAR: return std::make_unique<RadarSensorProcessor>();
        case SensorType::SAR:   return std::make_unique<SarSensorProcessor>();
    }
    return std::make_unique<OpticalSensorProcessor>();
}

} // namespace vsgraysim
