#version 460
/// ocean_spray.vert —— P4 喷雾粒子点精灵顶点 (重力抛物线 + 相机公告板)。
layout(location = 0) in vec3 inPosition;   // 占位 (点图元, 顶点 id 索引粒子)
layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;   // xyz=相机 ENU, w=time
    vec4 sun;
    vec4 wind;
    vec4 misc;         // x=seaLevel
} env;

struct Particle { vec4 posVel; vec4 velMisc; };
layout(set = 0, binding = 1) buffer Particles { Particle particles[]; };

layout(location = 0) out float vAlpha;
layout(location = 1) out vec2 vUV;

void main() {
    uint pid = gl_VertexIndex;
    vec4 posVel = particles[pid].posVel;
    vec4 velMisc = particles[pid].velMisc;
    float life = posVel.w;
    float t = env.camPosTime.w;

    // 出生时间编码: 用粒子 id 与帧时间错位 (简化: 所有粒子共享全局时钟相位)
    // 真实生命周期由 emit 端覆盖写入控制; 这里按 posVel.w 衰减透明度。
    float age = fract(t * 0.5 + float(pid) * 0.00061) * 2.0;   // 伪年龄相位 0..2s
    float a = clamp(age / max(life, 0.01), 0.0, 1.0);
    if (life <= 0.0 || a >= 1.0) { gl_Position = vec4(0, -2e6, 0, 1); vAlpha = 0.0; vUV=vec2(0); return; }

    // p(t) = p0 + v0·age + 0.5·g·age²
    const vec3 G = vec3(0.0, -9.81, 0.0);
    vec3 pos = posVel.xyz + velMisc.xyz * age + 0.5 * G * age * age;

    gl_Position = env.view_proj * env.model * vec4(pos, 1.0);
    gl_PointSize = clamp(velMisc.w * 800.0 / max(gl_Position.w, 1.0), 1.0, 32.0);
    vAlpha = (1.0 - a) * (1.0 - a) * 0.6;   // 出生亮, 衰减
    vUV = vec2(0.5);
}
