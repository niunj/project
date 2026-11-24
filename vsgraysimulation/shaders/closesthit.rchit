// closesthit.rchit
// Multi-spectral closest-hit shader.
// For each ray-scene intersection:
//   1. Atmospheric integration along ray-to-hit segment (attenuation, scatter)
//   2. Path phase accumulation: phi += 2*pi * distance / wavelength
//   3. Complex scattering: apply material reflection coefficient to I/Q
//   4. Cross-induction detection against receiver apertures
//
// Coherent radar physics:
//   - Each ray carries complex amplitude (I + jQ)
//   - Phase accumulates as: phi_path = 2*pi * L / lambda
//   - Scattering adds material-dependent phase shift
//   - Total phase: phi_total = phi_path + phi_scat

#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable

#include "vsgraysim_glsl_structs.glsl"

layout(location = 0) rayPayloadEXT RayPayload payload;

#include "atmosphere_common.glsl"

// Sensor receiver aperture buffer (Layer 1 attachments)
struct ApertureDesc {
    uint  sensor_id;
    Vec3  position;
    Vec3  direction;
    float half_angle;
    float aperture_area;
    uint  _pad0;
    uint  _pad1;
};

layout(set = 0, binding = 6, std430) readonly buffer ApertureBuf {
    ApertureDesc apertures[];
} aperture_buf;

// Crosstalk route buffer (Layer 0 CrossInductionRules)
struct CrossRouteDesc {
    uint tx_sensor_id;
    uint rx_sensor_id;
    int  tx_type;
    int  rx_type;
    int  cross_type;
    int  priority;
    float coupling_factor;
    float band_match;
    int  is_process;
    uint _pad;
};

layout(set = 0, binding = 7, std430) readonly buffer RouteBuf {
    CrossRouteDesc routes[];
} route_buf;

// ---- Complex reflection/scattering coefficient ----
// Returns (mag, phase_shift) given material parameters and incident angle.
// Uses simplified Fresnel equations for dielectric/metallic surfaces.
void compute_reflection_coefficient(
    float vis_albedo,
    float epsilon_real, float epsilon_imag,
    float incident_cos,
    out float reflect_mag,
    out float reflect_phase)
{
    // Default: Lambertian-like scattering with small phase shift
    if (epsilon_real < 1.01 && epsilon_imag < 0.01) {
        // Dielectric-dominated: use Fresnel
        float n = sqrt(epsilon_real);
        float r_perp = (1.0 - n) / (1.0 + n);
        reflect_mag = abs(r_perp * r_perp);
        reflect_phase = (r_perp < 0.0) ? 3.14159265 : 0.0;
    } else {
        // Metallic / lossy: complex refractive index
        float n_real = sqrt(0.5 * (sqrt(epsilon_real*epsilon_real + epsilon_imag*epsilon_imag) + epsilon_real));
        float n_imag = sqrt(0.5 * (sqrt(epsilon_real*epsilon_real + epsilon_imag*epsilon_imag) - epsilon_real));
        // Simplified metallic reflection magnitude ~0.9-1.0
        reflect_mag = clamp((n_real - 1.0) / (n_real + 1.0), 0.0, 1.0);
        // Metallic phase shift ~ pi for good conductors
        reflect_phase = 3.14159265 * (1.0 - 1.0 / max(1.0, n_real));
    }
    // Scale by incident angle (grazing -> weaker)
    reflect_mag *= clamp(incident_cos, 0.0, 1.0);
}

void main() {
    int band_channel = 0;
    if ((payload.band_type & 0x2u) != 0u) band_channel = 1;
    else if ((payload.band_type & 0x4u) != 0u) band_channel = 2;
    else if ((payload.band_type & 0x8u) != 0u) band_channel = 3;

    // ================================================================
    // 1. Atmospheric integration along the ray-to-hit segment
    // ================================================================
    atmosphere_integrate(payload,
                          vec3(payload.origin.x, payload.origin.y, payload.origin.z),
                          vec3(payload.dir.x, payload.dir.y, payload.dir.z),
                          gl_HitTEXT,
                          band_channel);

    // ================================================================
    // 2. Path phase accumulation: phi_path = 2*pi * L / lambda
    // ================================================================
    float path_phase = 2.0 * 3.14159265 * gl_HitTEXT / payload.wavelength;
    payload.phase_accum += path_phase;

    payload.distance = gl_HitTEXT;

    // ================================================================
    // 3. Look up geometry unit physical parameters
    // ================================================================
    uint prim_id = gl_PrimitiveID;
    uint n_map = band_map_buf.g_band_param_map.length();
    GeoUnitPhysParams gp;
    bool found = false;
    for (uint i = 0; i < n_map; ++i) {
        if (band_map_buf.g_band_param_map[i].geo_unit_id == prim_id) {
            gp = geo_buf.g_geo_phys_params[band_map_buf.g_band_param_map[i].param_offset];
            found = true;
            break;
        }
    }
    if (found) {
        payload.hit_geo_unit_id = gp.geo_unit_id;
        payload.hit_object_id   = gp.object_id;

        // ============================================================
        // 4. Complex scattering: apply material reflection to I/Q
        // ============================================================
        // Compute incident angle from hit normal
        vec3 N = normalize(/* would read from hit attributes, approximate */ vec3(0.0, 1.0, 0.0));
        vec3 I = -normalize(vec3(payload.dir.x, payload.dir.y, payload.dir.z));
        float NdotI = max(dot(N, I), 0.0);

        float reflect_mag, reflect_phase;
        if ((gp.band_mask & 0x1u) != 0u) {
            // VIS / laser band: use albedo-based scattering
            reflect_mag = gp.vis_albedo * NdotI;
            reflect_phase = 0.0;
        } else if ((gp.band_mask & 0x4u) != 0u) {
            // Radar band: use dielectric properties
            compute_reflection_coefficient(
                gp.vis_albedo,
                gp.radar_epsilon_real, gp.radar_epsilon_imag,
                NdotI,
                reflect_mag, reflect_phase);
        } else {
            // Default: Lambertian
            reflect_mag = NdotI * 0.3;
            reflect_phase = 0.0;
        }

        // Apply atmospheric attenuation magnitude
        reflect_mag *= payload.atm_total_atten;

        // Apply complex scattering to I/Q: multiply by |Gamma| * exp(j*phi_scat)
        float cos_p = cos(reflect_phase);
        float sin_p = sin(reflect_phase);
        float i_new = payload.i_amplitude * (reflect_mag * cos_p)
                    - payload.q_amplitude * (reflect_mag * sin_p);
        float q_new = payload.i_amplitude * (reflect_mag * sin_p)
                    + payload.q_amplitude * (reflect_mag * cos_p);
        payload.i_amplitude = i_new;
        payload.q_amplitude = q_new;

        // Add scattering phase to accumulated phase
        payload.phase_accum += reflect_phase;
    } else {
        payload.hit_geo_unit_id = 0xFFFFFFFFu;
        payload.hit_object_id   = 0xFFFFFFFFu;
    }

    // ================================================================
    // 5. Cross-induction detection
    // ================================================================
    uint tx_id = payload.tx_sensor_id;
    for (uint i = 0; i < route_buf.routes.length(); ++i) {
        CrossRouteDesc r = route_buf.routes[i];
        if (r.tx_sensor_id != tx_id) continue;
        for (uint j = 0; j < aperture_buf.apertures.length(); ++j) {
            ApertureDesc ap = aperture_buf.apertures[j];
            if (ap.sensor_id != r.rx_sensor_id) continue;
            vec3 neg_dir = -vec3(payload.dir.x, payload.dir.y, payload.dir.z);
            vec3 ap_dir  =  vec3(ap.direction.x, ap.direction.y, ap.direction.z);
            float cos_a = clamp(dot(neg_dir, ap_dir), -1.0, 1.0);
            float ang   = acos(cos_a);
            if (ang > ap.half_angle) continue;
            float p_cross = cross_power(payload.tx_power,
                                         payload.atm_total_atten,
                                         r.coupling_factor,
                                         ap.aperture_area,
                                         r.band_match);
            if (p_cross < 1e-9) continue;
            // Accept first qualifying route (pre-sorted by priority)
            payload.is_cross_induction = 1;
            payload.rx_sensor_id = r.rx_sensor_id;
            payload.cross_type   = r.cross_type;
            payload.cross_power  = p_cross;
            payload.cross_delay  = gl_HitTEXT / 2.99792458e8;
            payload.cross_band_match = r.band_match;
            return;
        }
    }
}
