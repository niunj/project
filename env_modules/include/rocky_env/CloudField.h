#pragma once
// Continuous world-space cloud field. CPU query and cloud_volume.frag use this
// frequency, advection, height profile and extinction contract.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace rockyenv::cloudfield {
inline float smooth(float lo,float hi,float v) {
    if(lo==hi)return v<lo?0.f:1.f;
    const float t=std::clamp((v-lo)/(hi-lo),0.f,1.f);return t*t*(3.f-2.f*t);
}
inline float hash3(float x,float y,float z) {
    uint32_t h=uint32_t(int32_t(x))*73856093u ^ uint32_t(int32_t(y))*19349663u ^ uint32_t(int32_t(z))*83492791u;
    h^=h>>16;h*=2246822519u;h^=h>>13;h*=3266489917u;h^=h>>16;
    return float(h&0x00ffffffu)/16777216.f;
}
inline float vnoise3(float x, float y, float z) {
    const float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
    const float fx = x - ix, fy = y - iy, fz = z - iz;
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float sz = fz * fz * (3.0f - 2.0f * fz);
    const float a000 = hash3(ix, iy, iz);
    const float a100 = hash3(ix + 1, iy, iz);
    const float a010 = hash3(ix, iy + 1, iz);
    const float a110 = hash3(ix + 1, iy + 1, iz);
    const float a001 = hash3(ix, iy, iz + 1);
    const float a101 = hash3(ix + 1, iy, iz + 1);
    const float a011 = hash3(ix, iy + 1, iz + 1);
    const float a111 = hash3(ix + 1, iy + 1, iz + 1);
    const float x00 = a000 + (a100 - a000) * sx;
    const float x10 = a010 + (a110 - a010) * sx;
    const float x01 = a001 + (a101 - a001) * sx;
    const float x11 = a011 + (a111 - a011) * sx;
    const float y0 = x00 + (x10 - x00) * sy;
    const float y1 = x01 + (x11 - x01) * sy;
    return y0 + (y1 - y0) * sz;
}
/// 5-octave 3D fbm (与 shader fbm3 同参数: 0.5 权, 2.03 频率倍)。
inline float cloudf3(float x, float y, float z) {
    float v = 0.0f, a = 0.5f;
    for (int i = 0; i < 5; ++i) {
        v += a * vnoise3(x, y, z);
        x *= 2.03f; y *= 2.03f; z *= 2.03f;
        a *= 0.5f;
    }
    return v;
}

/// 覆盖率 → fbm 密度阈值 (分位数映射)。
///
/// cloudf2 的 5-octave fbm 值域实测约 [0.15, 0.81] (均值 0.48, 近似正态),
/// 而非理想 [0,1]。若直接用 1-cov 作阈值, 低云量时会完全无云。此处用实测
/// 分位数近似 (均值 0.482, 标准差约 0.13), 令 coverage = 平均云覆盖率。
/// 与 cloud_sheet.frag 的 coverageThreshold() 严格同源 (同常数)。
inline float coverageThreshold(float cov) {
    constexpr float kMean = 0.482f;   // fbm 均值
    constexpr float kSigma = 0.130f;  // fbm 标准差
    // 标准正态的 p 分位数 (Acklam 有理近似, 精度 ~1e-9)。
    const float p = std::clamp(1.0f - cov, 1e-4f, 1.0f - 1e-4f);
    constexpr float plow = 0.02425f, phigh = 1.0f - plow;
    float z;
    if (p < plow) {
        const float q = std::sqrt(-2.0f * std::log(p));
        const float num = (((((  -7.784894002430293e-03f * q - 3.223964580411365e-01f) * q
                             - 2.400758277161838e+00f) * q - 2.549732539343734e+00f) * q
                             + 4.374664141464968e+00f) * q + 2.938163982698783e+00f);
        const float den = ((((  7.784695709041462e-03f * q + 3.224671290700398e-01f) * q
                             + 2.445134137142996e+00f) * q + 3.754408661907416e+00f) * q + 1.0f);
        z = num / den;
    } else if (p <= phigh) {
        const float q = p - 0.5f;
        const float r = q * q;
        const float num = ((((( -3.969683028665376e+01f * r + 2.209460984245205e+02f) * r
                             - 2.759285104469687e+02f) * r + 1.383577518672690e+02f) * r
                             - 3.066479806614716e+01f) * r + 2.506628277459239e+00f);
        const float den = ((((( -5.447609879822406e+01f * r + 1.615858368580409e+02f) * r
                             - 1.556989798598866e+02f) * r + 6.680131188771972e+01f) * r
                             - 1.328068155288572e+01f) * r + 1.0f);
        z = num * q / den;
    } else {
        const float q = std::sqrt(-2.0f * std::log(1.0f - p));
        const float num = (((((  -7.784894002430293e-03f * q - 3.223964580411365e-01f) * q
                             - 2.400758277161838e+00f) * q - 2.549732539343734e+00f) * q
                             + 4.374664141464968e+00f) * q + 2.938163982698783e+00f);
        const float den = ((((  7.784695709041462e-03f * q + 3.224671290700398e-01f) * q
                             + 2.445134137142996e+00f) * q + 3.754408661907416e+00f) * q + 1.0f);
        z = -num / den;
    }
    return kMean + kSigma * z;
}


inline float density(float x,float y,float z,float base,float thickness,float coverage,float strength,
                     float time,float windX,float windZ,float speed) {
    if(thickness<=0 || coverage<=0 || strength<=0)return 0;
    const float n=std::hypot(windX,windZ);if(n>1e-6f){windX/=n;windZ/=n;}else {windX=1;windZ=0;}
    const float px=(x-windX*time*speed)/600.f;
    const float py=y/600.f;
    const float pz=(z-windZ*time*speed)/600.f;
    float shape=cloudf3(px*.5f,py*.5f,pz*.5f)-.15f*cloudf3(px*2.5f+11.7f,py*2.5f+11.7f,pz*2.5f+11.7f);
    const float h=(y-base)/std::max(thickness,1.f);
    shape*=smooth(0,.2f,h)*(1.f-smooth(.6f,1.f,h));
    const float threshold=coverageThreshold(std::clamp(coverage,0.f,1.f)*.55f+.08f);
    return smooth(threshold,threshold+.12f,shape)*std::clamp(strength*1.6f,.4f,1.f);
}
} // namespace rockyenv::cloudfield
