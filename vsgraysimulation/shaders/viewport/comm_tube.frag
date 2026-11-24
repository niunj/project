#version 460
/// comm_tube.frag — 通信/交战管道片元 shader
/// 数据流滚动 + 通断状态 + 管壁半透明
layout(location = 0) in vec2 inUV;
layout(location = 1) in float outFlow;

layout(push_constant) uniform PC {
    float time_sec;
    float param1;       ///< 0=活跃, 1=断开
    float param2;       ///< 数据流速度因子
    float pad;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // 管道基础半透明色 (通信青 / 制导橙 由 push-constant 外部设置纹理,
    // 这里用 param2 > 0.5 区分: >0.5 → 橙, ≤0.5 → 青)
    vec3 base = mix(vec3(0.30, 0.82, 0.88),  // 通信青
                    vec3(1.0, 0.65, 0.15),   // 制导橙
                    step(0.5, pc.param2));

    // 断开状态: 整体变暗 + 红色闪烁
    float disconnected = pc.param1;
    float blink = step(0.5, fract(pc.time_sec * 2.0));
    vec3 break_col = vec3(0.8, 0.15, 0.1) * blink;
    base = mix(base, break_col * 0.5 + base * 0.3, disconnected);

    // 数据流高亮: 沿管道方向滚动的亮带
    float flow = outFlow;
    float pulse = smoothstep(0.7, 1.0, flow) * smoothstep(1.0, 0.9, flow + 0.15);
    vec3 flow_col = base * 1.8 + vec3(0.4) * pulse;

    // 断开时数据流停止
    vec3 col = mix(flow_col, base * 0.3, disconnected);

    // 管壁 alpha: 基础半透明 + 数据流增强
    float alpha = 0.18 + pulse * 0.3;
    alpha *= (1.0 - disconnected * 0.3);

    outColor = vec4(col, alpha);
}
