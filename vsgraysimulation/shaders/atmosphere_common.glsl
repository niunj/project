// atmosphere_common.glsl
// Shared atmospheric sampling helpers used by raygen / closesthit shaders.
// Mirrors the CPU reference implementation in AtmosphereIntegrator.

#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 1, std430) readonly buffer AtmParamsBuf {
    AtmLayerParams g_atm_params[];
} atm_buf;

layout(set = 0, binding = 2, std430) readonly buffer GeoPhysBuf {
    GeoUnitPhysParams g_geo_phys_params[];
} geo_buf;

layout(set = 0, binding = 3, std430) readonly buffer BandParamMapBuf {
    BandParamMap g_band_param_map[];
} band_map_buf;

// Returns the atmospheric layer index whose slab contains the given altitude.
int atmosphere_layer_index(float altitude_m) {
    int n = atm_buf.g_atm_params.length();
    // Linear search: layers are few (<32 typically).
    for (int i = 0; i < n; ++i) {
        AtmLayerParams L = atm_buf.g_atm_params[i];
        if (altitude_m >= L.height_min && altitude_m < L.height_max) {
            return i;
        }
    }
    return n - 1;
}

// Sample (with linear interpolation) the per-band extinction coefficient.
float atmosphere_extinction(int layer_id, int band_channel) {
    if (layer_id < 0) return 0.0;
    int n = atm_buf.g_atm_params.length();
    int i = clamp(layer_id, 0, n - 1);
    int j = clamp(i + 1, 0, n - 1);
    AtmLayerParams a = atm_buf.g_atm_params[i];
    AtmLayerParams b = atm_buf.g_atm_params[j];
    float t = 0.5; // For interior slabs we are inside slab `i`; use 0.
    return mix(a.ext_coeff[band_channel], b.ext_coeff[band_channel], t);
}

float atmosphere_scatter(int layer_id, int band_channel) {
    if (layer_id < 0) return 0.0;
    int n = atm_buf.g_atm_params.length();
    int i = clamp(layer_id, 0, n - 1);
    return atm_buf.g_atm_params[i].scatter_coeff[band_channel];
}

float atmosphere_refract_index(int layer_id) {
    if (layer_id < 0) return 1.0003;
    int n = atm_buf.g_atm_params.length();
    int i = clamp(layer_id, 0, n - 1);
    return atm_buf.g_atm_params[i].refract_index;
}

// March the ray segment [origin, origin + dir*distance] updating the
// payload's atmospheric accumulators.  Called from closesthit / anyhit.
void atmosphere_integrate(inout RayPayload payload,
                          vec3 origin, vec3 dir, float distance,
                          int band_channel) {
    if (distance <= 0.0) return;
    float step_min = 10.0;
    float step_max = 1000.0;
    float travelled = 0.0;
    while (travelled < distance) {
        vec3 p = origin + dir * travelled;
        float alt = length(p);
        int lid = atmosphere_layer_index(alt);
        payload.atm_layer_id = lid;
        float density = atm_buf.g_atm_params[lid].density;
        float step = clamp(step_min * (1.225 / max(0.001, density)),
                           step_min, step_max);
        step = min(step, distance - travelled);
        float sigma_e = atmosphere_extinction(lid, band_channel);
        float sigma_s = atmosphere_scatter(lid, band_channel);
        payload.atm_total_atten *= exp(-sigma_e * step);
        payload.atm_scatter_power += payload.tx_power * payload.atm_total_atten
                                     * sigma_s * step;
        travelled += step;
    }
}

// Compute the crosstalk power: P_cross = P_tx * L_atm * eta * S * band_match
float cross_power(float tx_power, float atm_atten,
                  float coupling, float aperture_area,
                  float band_match) {
    return tx_power * atm_atten * coupling * aperture_area * band_match;
}
