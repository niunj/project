// Analytic first-order response sampled at the sensor exposure time.
float surfaceTemperature(float baseK, vec4 response, vec4 source, vec4 state,
                         float cloudCover, float slopeExposure) {
    const float omega = 0.2617993878;  // radians per solar hour
    const float phase = (state.x - 12.0) * omega;
    const float inertia = response.z * omega;
    const float sun = response.y * cos(phase - atan(inertia)) * inversesqrt(1.0 + inertia * inertia);
    const float elapsed = max(0.0, state.w - source.y);
    const float running = min(elapsed, source.z);
    const float tau = max(source.w, 0.01);
    const float heating = source.x * (1.0 - exp(-running / tau)) * exp(-(elapsed - running) / tau);
    return clamp(baseK + state.y + sun * (1.0 - 0.65 * clamp(cloudCover, 0.0, 1.0))
                 + 0.24 * response.y * slopeExposure
                 - response.w * clamp(state.z, 0.0, 1.0) + heating, 1.0, 5000.0);
}

float wetSurfaceEmissivity(float dry, vec4 response, float wetness) {
    return response.x > 0.5 && response.w > 0.0
        ? mix(dry, max(dry, 0.96), 0.65 * clamp(wetness, 0.0, 1.0)) : dry;
}

vec2 octahedralFold(vec2 v) {
    return (1.0 - abs(v.yx)) * mix(vec2(-1.0),vec2(1.0),greaterThanEqual(v,vec2(0.0)));
}

// Store two 11-bit octahedral coordinates as an exact numeric float in G2.w.
// Zero is reserved for meshes without a shading normal.
float packThermalNormal(vec3 n) {
    const float magnitude = abs(n.x) + abs(n.y) + abs(n.z);
    if (magnitude < 1e-8) return 0.0;
    n /= magnitude;
    const vec2 oct = n.z < 0.0 ? octahedralFold(n.xy) : n.xy;
    const uvec2 encoded = uvec2(round((oct * 0.5 + 0.5) * 2047.0));
    return float(1u + encoded.x + (encoded.y << 11));
}

vec3 unpackThermalNormal(float packed, vec3 fallback) {
    if (packed < 1.0) return fallback;
    const uint encoded = uint(packed) - 1u;
    const vec2 oct = vec2(encoded & 2047u, encoded >> 11) * (2.0 / 2047.0) - 1.0;
    vec3 n = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0) n.xy = octahedralFold(n.xy);
    return normalize(n);
}
