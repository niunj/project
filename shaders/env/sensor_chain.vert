// sensor_chain.vert —— 信号链/显示公共全屏三角 (无顶点缓冲, 3 顶点)。
#version 460

layout(location = 0) out vec2 vUv;

void main() {
    // 全屏大三角: clip 空间覆盖 [-1,3]
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    vUv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
