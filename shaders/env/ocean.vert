#version 460
/// ocean.vert — ENU 相机跟随海面网格 + Gerstner 波列 (S2: 16 谐波 + 涌浪 + 曲率)。
/// 波形参数全部来自 UBO oceanWave[16] (CPU WaveField 下发, 与 IOceanSampler
/// 同参数同公式 —— 所见即所采); oceanOpt 开启球面曲率时远海下沉。
/// 注: wave_mode=2 GPU FFT 用 ocean_fft.vert (set=1 采样位移纹理); 本着色器
///     为谐波模式 (wave_mode=0/1), 管线布局仅 set=0 环境 UBO, 无 set=1。
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
    vec4 oceanWave[16];  // x=波长L y=振幅A z=cosθ w=sinθ (风浪在前, 涌浪在后)
    vec4 oceanOpt;       // x=swellH y=reflection z=curvature w=waveCount
    vec4 oceanOpt2;      // FFT 配置；保持与 ocean_fft.vert/ocean.frag 的接口一致
} env;

layout(set=1,binding=2) uniform WaveDynamics {vec4 state[16];} dynamics;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vJacobian;   // 波陡雅可比 (frag 白沫用)

const float G = 9.80665;
const float R_EARTH = 6371000.0;

void main() {
    float time = env.camPosTime.w;
    float seaLevel = env.misc.x;
    float Q = clamp(env.ext1.z, 0.0, 1.0);

    vec2 cam = env.camPosTime.xz;

    // 网格中心跟随相机, 但钳制在海洋边界内 (与 CPU updateTerrainHeights 一致)。
    vec2 bc = env.ext0.xy;
    vec2 bh = (env.ext1.y > 0.5) ? env.ext0.zw : vec2(env.ext0.z);
    float meshHalf = env.misc.y * 0.5;
    vec2 span = max(bh - vec2(meshHalf), vec2(0.0));
    vec2 center = bc + clamp(cam - bc, -span, span);
    vec2 xz = inPosition.xz + center;

    // ---- Gerstner 波列: 逐谐波累加 (与 CPU WaveField 同公式) ----
    vec3 disp = vec3(0.0);
    float nx = 0.0, nz = 0.0;
    // P2 完整 2×2 雅可比累加项 (与 CPU WaveField::jacobian 同式):
    //   sxx=ΣQkA·dx²·sinφ  szz=ΣQkA·dz²·sinφ  sxz=ΣQkA·dx·dz·sinφ
    //   J = (1-sxx)(1-szz) - sxz²  (含剪切; 旧对角近似只对单波向准确)
    float sxx = 0.0, szz = 0.0, sxz = 0.0;
    int count = int(max(env.oceanOpt.w, 0.0));
    for (int i = 0; i < 16; ++i) {
        if (i >= count) break;
        float L = env.oceanWave[i].x;
        float A = env.oceanWave[i].y;
        if (A <= 0.0 || L <= 0.0) continue;
        vec2 d = vec2(env.oceanWave[i].z, env.oceanWave[i].w);
        float k = 6.2831853 / L;
        float omega = dynamics.state[i].x;
        // 相位: 与 CPU 一致 (无每谐波相移 —— CPU/GPU 公式严格对齐)。
        float ph = k * dot(d, xz) - omega * time + dynamics.state[i].y;
        float cph = cos(ph), sph = sin(ph);
        disp.x += Q * A * d.x * cph;
        disp.z += Q * A * d.y * cph;
        disp.y += A * sph;
        nx += k * A * d.x * cph;
        nz += k * A * d.y * cph;
        float qkas = Q * k * A * sph;
        sxx += qkas * d.x * d.x;
        szz += qkas * d.y * d.y;
        sxz += qkas * d.x * d.y;
    }
    float jac = (1.0 - sxx) * (1.0 - szz) - sxz * sxz;

    vec3 pos = vec3(xz.x + disp.x, seaLevel + disp.y, xz.y + disp.z);

    // ---- S2: 球面曲率 —— 远处海面沿地表下沉 (d²/2R) ----
    // 相机高程: ENU y (平面模式即海拔); 地球模式近似用锚点切平面即可。
    if (env.oceanOpt.z > 0.5) {
        vec2 d2 = pos.xz;
        float dist2 = dot(d2, d2);
        pos.y -= dist2 / (2.0 * R_EARTH);
    }

    vNormal = normalize(vec3(-nx, 1.0, -nz));
    if (env.oceanOpt.z > 0.5)
        vNormal = normalize(vNormal + vec3(pos.x / R_EARTH, 0.0, pos.z / R_EARTH) * vNormal.y);
    vWorldPos = pos;
    vUV = inUV;
    vJacobian = jac;

    gl_Position = env.view_proj * env.model * vec4(pos, 1.0);
}
