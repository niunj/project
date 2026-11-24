// sensor_display.frag —— 最终显示 pass: 左半 = 观察视口(可见光), 右半 = 传感器
// 视口(P4 image 产品, AGC/ADC/Gamma 后灰度)。中缝 2px 白线分隔。
#version 460

layout(set = 0, binding = 1) uniform sampler2D texVis;
layout(set = 0, binding = 2) uniform sampler2D texElec;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 col;
    if (vUv.x < 0.5) {
        // 左半: 观察视口 (可见光近似, 朗伯+太阳+天空渐变)
        col = texture(texVis, vec2(vUv.x * 2.0, vUv.y)).rgb;
    } else {
        // 右半: 传感器视口 (P4 image 产品)
        col = texture(texElec, vec2((vUv.x - 0.5) * 2.0, vUv.y)).rgb;
    }
    // 中缝分隔线
    if (abs(vUv.x - 0.5) < 0.001) col = vec3(0.9);
    outColor = vec4(col, 1.0);
}
