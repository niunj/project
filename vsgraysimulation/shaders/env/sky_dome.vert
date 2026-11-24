#version 460
/// sky_dome.vert — 原生平面天空穹顶 (ENU 相机跟随, 零 rocky 依赖)。
/// 几何为以原点为心的大球; 每帧把穹顶中心平移到相机 ENU 位置 (与云环/
/// 海面同一跟随模式), 射线方向取顶点局部方向, 散射在片元内按虚拟行星解析求交。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;   // xyz=相机 ENU, w=时间
    vec4 sun;          // xyz=太阳方向 ENU, w=高度角(度)
    vec4 wind;
    vec4 misc;         // x=曝光 y=浊度 z=相机海拔(米) w=保留
    // 48..83 为环境模块公共光色段 (本阶段不使用, 但块布局须与 frag 一致)
    vec4 ext0; vec4 ext1;
    vec4 atm;
    vec4 deepCol; vec4 shallowCol;
    vec4 sunLight;
    vec4 ambient;
    vec4 skyHorizon;
    vec4 skyZenith;
    // 84..91 天空专属: 月盘与星空
    vec4 moon;         // xyz=月亮方向 ENU, w=月相 0..1
    vec4 astro;        // x=本地恒星时(rad) y=纬度(rad) z=月亮高度角(度)
    vec4 diskOptics;  // observer-centred Sun and Moon angular radii
} env;

layout(location = 0) out vec3 vDir;   // 射线方向 (ENU, 归一化)

void main() {
    vDir = normalize(inPosition);
    vec3 pos = inPosition + env.camPosTime.xyz;  // 穹顶中心跟随相机
    gl_Position = env.view_proj * env.model * vec4(pos, 1.0);
    // Reverse-Z celestial background. The globe may be much farther than the dome radius.
    gl_Position.z = 0.0;
}
