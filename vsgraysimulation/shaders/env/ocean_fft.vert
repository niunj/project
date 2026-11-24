#version 460
/// ocean_fft.vert — ENU 相机跟随海面网格 + GPU FFT 位移采样 (wave_mode=2)。
/// 与 ocean.vert (Gerstner 谐波) 输出契约一致 (vWorldPos/vNormal/vUV/vJacobian),
/// 但波形来自 set=1 的 FFT 位移/法线泡沫纹理 (OceanFFTField compute 产出):
///   fftDispMap (chopX, h, chopZ, 0) —— 顶点位移 (choppy + 高度)
///   fftFoamMap (Nx, Ny, Nz, J)      —— 法线 + 水平雅可比行列式
/// 平铺边长/流向由 oceanOpt2 (offset 160..163) 下发；CPU IOceanSampler 使用
/// 同一初始谱、相位及归一化的 IFFT 缓存，按此纹理的周期双线性规则采样。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Env {
    mat4 view_proj;
    mat4 model;
    vec4 camPosTime;
    vec4 sun;
    vec4 wind;
    vec4 misc;   // x=seaLevel y=size z=Hs(含涌浪) w=peakLen
    vec4 ext0;   // 海洋边界: x=centerX y=centerZ z=radius或halfX w=halfZ
    vec4 ext1;   // x=fadeWidth y=shape(0=圆盘 1=矩形) z=choppyQ w=beaufort
    vec4 atm;    // x=能见度(米) y=降水强度
    vec4 deepCol;
    vec4 shallowCol;
    vec4 sunLight;   // rgb=太阳直射色 w=昼因子
    vec4 ambient;    // rgb=天光环境色
    vec4 skyHorizon; // rgb=地平线天色
    vec4 skyZenith;  // rgb=天顶天色
    vec4 moon;
    vec4 astro;
    vec4 oceanWave[16];  // (谐波分支不使用; 保留布局对齐 CPU/传感器采样契约)
    vec4 oceanOpt;       // x=swellH y=reflection z=curvature w=waveCount
    vec4 oceanOpt2;      // P3 FFT: x=平铺边长 y=流向x z=流向z w=FFT开关(=1)
} env;

// P3 GPU FFT: set=1 采样位移/法线泡沫纹理 (OceanFFTField compute 输出)。
layout(set = 1, binding = 0) uniform sampler2D fftDispMap;
layout(set = 1, binding = 1) uniform sampler2D fftFoamMap;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vJacobian;

const float R_EARTH = 6371000.0;

void main() {
    float seaLevel = env.misc.x;
    vec2 cam = env.camPosTime.xz;

    // 网格中心跟随相机, 钳制在海洋边界内 (与 ocean.vert 一致)。
    vec2 bc = env.ext0.xy;
    vec2 bh = (env.ext1.y > 0.5) ? env.ext0.zw : vec2(env.ext0.z);
    float meshHalf = env.misc.y * 0.5;
    vec2 span = max(bh - vec2(meshHalf), vec2(0.0));
    vec2 center = bc + clamp(cam - bc, -span, span);
    vec2 xz = inPosition.xz + center;

    // ---- GPU FFT 位移/法线/雅可比采样 ----
    // UV: ENU 世界坐标 / 平铺边长 → 平铺域 (采样器 REPEAT 平铺)。
    // 谱已含流向 (OceanFFTField.updateSpectrum 以流向为轴旋转), 此处不再转。
    // +0.5 偏移: 避免负 ENU 坐标经 fract 折返后与谱相位错相。
    vec2 fuv = xz / env.oceanOpt2.x + 0.5;
    vec3 disp = texture(fftDispMap, fuv).rgb;   // (chopX, h, chopZ)
    vec4 foamN = texture(fftFoamMap, fuv);      // (Nx, Ny, Nz, J)
    // 法线已是归一化 (compose 输出), 直接转坡度 (与谐波分支的 -nx/-nz 同义)。
    float nx = -foamN.x;
    float nz = -foamN.z;
    float jac = foamN.a;

    vec3 pos = vec3(xz.x + disp.x, seaLevel + disp.y, xz.y + disp.z);

    // ---- 球面曲率 (oceanOpt.z, 与谐波分支同公式) ----
    if (env.oceanOpt.z > 0.5) {
        vec2 d2 = pos.xz;
        float dist2 = dot(d2, d2);
        pos.y -= dist2 / (2.0 * R_EARTH);
    }

    vNormal = normalize(foamN.xyz);
    if (env.oceanOpt.z > 0.5)
        vNormal = normalize(vNormal + vec3(pos.x / R_EARTH, 0.0, pos.z / R_EARTH) * vNormal.y);
    vWorldPos = pos;
    vUV = inUV;
    vJacobian = jac;

    gl_Position = env.view_proj * env.model * vec4(pos, 1.0);
}
