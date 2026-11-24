// miss.rmiss
// Handles rays that exit the scene without hitting any geometry.
// Still accumulates:
//   - Atmospheric attenuation along the full ray length
//   - Path phase: phi = 2*pi * L / lambda (free-space propagation)

#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "vsgraysim_glsl_structs.glsl"

layout(location = 0) rayPayloadEXT RayPayload payload;

#include "atmosphere_common.glsl"

void main() {
    // Determine band channel from sensor's payload band_type (band_mask)
    int band_channel = 0;
    if ((payload.band_type & 0x2u) != 0u) band_channel = 1;
    else if ((payload.band_type & 0x4u) != 0u) band_channel = 2;
    else if ((payload.band_type & 0x8u) != 0u) band_channel = 3;

    // Integrate atmosphere along the full miss distance
    atmosphere_integrate(payload,
                          vec3(payload.origin.x, payload.origin.y, payload.origin.z),
                          vec3(payload.dir.x, payload.dir.y, payload.dir.z),
                          gl_RayTmaxEXT,
                          band_channel);

    // Accumulate free-space path phase: phi = 2*pi * L / lambda
    float path_phase = 2.0 * 3.14159265 * gl_RayTmaxEXT / payload.wavelength;
    payload.phase_accum += path_phase;

    payload.distance = gl_RayTmaxEXT;
    payload.hit_geo_unit_id = 0xFFFFFFFFu;
    payload.hit_object_id   = 0xFFFFFFFFu;
}
