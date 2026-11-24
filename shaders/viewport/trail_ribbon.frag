#version 460
/// trail_ribbon.frag — 运动拖尾条带片元 shader
/// 沿条带方向渐隐: 新(uv.x→1)浓 → 旧(uv.x→0)淡
layout(location = 0) in vec2 inUV;

layout(push_constant) uniform PC {
    float time_sec;
    float param1;   ///< 阵营色 (0=红, 0.5=蓝, 1=中立)
    float param2;   ///< 条带亮度因子
    float pad;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // 阵营色
    vec3 col;
    if (pc.param1 < 0.25) col = vec3(1.0, 0.32, 0.32);       // 红
    else if (pc.param1 < 0.75) col = vec3(0.27, 0.54, 1.0); // 蓝
    else col = vec3(0.62, 0.62, 0.62);                        // 中立

    // 沿条带方向渐隐: uv.x=0 最旧 → uv.x=1 最新
    float fade = smoothstep(0.0, 0.15, inUV.x) * pow(inUV.x, 0.6);
    float alpha = fade * pc.param2 * 0.38;

    // 宽度方向中心浓、边缘淡
    float edge = 1.0 - abs(inUV.y * 2.0 - 1.0);
    alpha *= mix(0.5, 1.0, edge);

    outColor = vec4(col, alpha);
}
