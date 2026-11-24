#version 460
/// faction_halo.frag — 阵营光晕穹顶片元 shader
/// 底部浓顶部淡 + 呼吸脉冲 + 阵营色
layout(location = 0) in vec2 inUV;

layout(push_constant) uniform PC {
    float time_sec;
    float param1;     ///< 阵营 (0=红, 0.5=蓝, 1=中立)
    float param2;     ///< 脉冲强度
    float pad;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // 阵营色
    vec3 col;
    if (pc.param1 < 0.25) col = vec3(1.0, 0.32, 0.32);        // 红
    else if (pc.param1 < 0.75) col = vec3(0.27, 0.54, 1.0);  // 蓝
    else col = vec3(0.62, 0.62, 0.62);                         // 中立

    // 底部浓 (uv.y=0) → 顶部淡 (uv.y=1)
    float vert = 1.0 - inUV.y;
    float alpha = vert * vert * 0.28;

    // 呼吸脉冲
    float pulse = 0.8 + 0.2 * sin(pc.time_sec * 2.5);
    alpha *= pulse;

    // 穹顶边缘渐隐 (水平方向边缘)
    float edge = 1.0 - abs(inUV.x * 2.0 - 1.0);
    alpha *= mix(0.3, 1.0, edge);

    outColor = vec4(col, alpha);
}
