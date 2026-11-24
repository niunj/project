#version 460
/// detection_volume.frag — 探测体积片元 shader
/// 按沿径向距离渐隐 + 扫描波束高亮区域 + 边缘渐隐
layout(location = 0) in vec2 inUV;
layout(location = 1) in float inDist;

layout(push_constant) uniform PC {
    float time_sec;
    float scan_az;       ///< 当前扫描方位 (rad)
    float param2;
    float pad;
} pc;

layout(location = 0) out vec4 outColor;

/// 基础探测色 (按 inUV.y 编码的方位/俯仰查找, 简化: 用 push-constant 传入色调)
/// param2 = 0.0 → 雷达蓝, 0.33 → 干扰橙, 0.66 → 红外红
vec3 baseColor() {
    float t = pc.param2;
    if (t < 0.5) return mix(vec3(0.4, 0.7, 1.0), vec3(1.0, 0.65, 0.2), t * 2.0);
    return mix(vec3(1.0, 0.65, 0.2), vec3(1.0, 0.3, 0.15), (t - 0.5) * 2.0);
}

void main() {
    // 径向渐隐: 内浓外淡
    float radial_alpha = (1.0 - inDist) * 0.22 + 0.02;

    // 扫描波束高亮: 在 scan_az 附近 ±15 度的区域增亮
    // inUV.y 编码方位角度分数 (0→1 对应方位范围)
    float beam_az = inUV.y * 6.28318;  // 近似: 0→2π
    float ang_diff = abs(beam_az - pc.scan_az);
    ang_diff = min(ang_diff, 6.28318 - ang_diff);  // 取最短角差
    float beam_gain = exp(-ang_diff * ang_diff / (0.26 * 0.26));  // 高斯
    float beam_alpha = beam_gain * (1.0 - inDist) * 0.35;

    float total_alpha = radial_alpha + beam_alpha;
    vec3 col = baseColor() + vec3(0.3) * beam_gain;

    outColor = vec4(col, total_alpha);
}
