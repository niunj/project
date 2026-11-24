#version 460
/// cloud_volume.vert — P3-4b 体积积云壳体 (ENU 相机跟随网格, 与 cloud_sheet 同模式)。
/// 与公告板不同, 本网格以相机为中心在锚点 ENU 系内铺满 (size × size),
/// 每个片元对应稳定的水平世界坐标 → ray-march 一个真正共享的 3D 体积场
/// (cloud_volume.frag 内从相机到片元的射线在层底/层顶云盒内 march)。
/// 网格中心跟随相机, 但钳制在云层水平范围内 (与 sheet 漂移对齐)。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;   // xyz=相机 ENU, w=时间
    vec4 sun;
    vec4 wind;         // x=风东向 y=风南向 z=风速 w=云量
    vec4 misc;         // x=覆盖率 y=类型(Cumulus=3/Cumulonimbus=4) z=降水 w=密度
    vec4 ext0;         // x=层底高度 y=层厚 z=unused w=覆盖调制
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
layout(location = 1) out vec3 vWorldPos;   // 片元 ENU 世界坐标 (壳体上表面)
layout(location = 2) out float vBase;      // 云底层底高度
layout(location = 3) out float vThick;     // 云层厚

void main() {
    vUV = inUV;
    vBase = env.ext0.x;
    vThick = env.ext0.y;

    // 壳体: 水平网格 (size × size) 铺在云底高度 (ray-march 在此上方体积内进行)。
    float size = 6000.0;   // 与层云片同量级; 太大 march 距离过长, 太小覆盖不足。
    vec2 cam = env.camPosTime.xz;
    vec2 center = cam;   // 相机跟随; 体积场连续 (噪声平铺), 不需额外钳制。
    vec3 pos = vec3(inPosition.x + center.x, vBase, inPosition.z + center.y);

    vWorldPos = pos;
    gl_Position = env.view_proj * env.model * vec4(pos, 1.0);
}
