#version 460
/// cloud_sheet.vert — 高空层云片 (水平大贴片, 相机跟随, 风漂移在片元)。
/// 世界坐标连续平铺 (v4: UBO 块补全至 160-float 布局)。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;
    vec4 wind;
    vec4 misc;        // x=层尺寸 y=覆盖率 z=高度 w=类型枚举
    vec4 ext0;        // x=降水 y=层厚 z=密度
    vec4 ext1;
    vec4 atm;
    vec4 deepCol;
    vec4 shallowCol;
    vec4 sunLight;
    vec4 ambient;
    vec4 skyHorizon;
    vec4 skyZenith;
    vec4 moon;
    vec4 astro;
    vec4 oceanWave[16];
    vec4 oceanOpt;
} env;

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vAlt;

void main() {
    // 相机跟随: 网格中心跟随相机 ENU 位置 (xz), y 保持不变 (层云高度)。
    // 关键修复 (CLD-1): 网格平移后 UV 必须基于世界坐标连续平铺,
    // 否则相机移动时云层图案会"粘在"网格上一起平移 (视觉重复/穿帮)。
    vec3 pos = inPosition;
    pos.x += env.camPosTime.x;
    pos.z += env.camPosTime.z;

    // 世界坐标连续平铺: UV = 世界坐标 / 特征尺度, 保证云层无限延伸
    // 且相机移动时图案固定在地球上 (不随网格平移)。
    vec2 worldXZ = pos.xz;
    vUV = worldXZ / 600.0;  // 云团特征尺度 ~ 600m
    vAlt = inPosition.y;    // 保持原始高度用于着色
    gl_Position = env.view_proj * env.model * vec4(pos, 1.0);
}
