#version 460
/// ocean_spray.frag —— P4 喷雾粒子片元 (软圆点, 水雾白)。
layout(location = 0) in float vAlpha;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    if (vAlpha <= 0.001) discard;
    // gl_PointCoord 中心软圆衰减
    vec2 d = gl_PointCoord - 0.5;
    float r2 = dot(d, d) * 4.0;
    float alpha = vAlpha * clamp(1.0 - r2, 0.0, 1.0);
    if (alpha < 0.004) discard;
    outColor = vec4(vec3(0.90, 0.93, 0.95), alpha);
}
