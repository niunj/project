/// @file OceanFFTField.cpp —— P3 GPU FFT 波场实现 (对标 Triton FFT 管线)。
///
/// 三段 compute: ocean_fft_spectrum (频域演化) → ocean_ifft×2 (行+列) →
/// ocean_fft_compose (合成位移/法线/泡沫)。h0 复振幅由 CPU 按 JONSWAP 频谱 +
/// Mitsuyasu 方向谱预计算 (与 WaveField 谱参数同源), 经 RG32F 纹理上传。
///
/// 频谱公式 (对标 Triton FourierAmplitudeJONSWAP + 本项目 WaveField):
///   S(ω) = αg²/ω⁵ · exp(-1.25(ωp/ω)⁴) · γ^r        (JONSWAP, WaveField 同源)
///   D(ω,θ) = N(s)·cos^{2s}(Δθ/2),  s=11.5(g/ωpU)^2.5(ω/ωp)^μ  (Mitsuyasu)
///   h0(k) = √(S·D·dkx·dky/2)·(ξr+i·ξi)               (确定性高斯相位)
#include "OceanFFTField.h"
#include "WaveField.h"
#include "EarthShader.h"

#include <vsg/all.h>
#include <vsg/commands/PipelineBarrier.h>

#include <cmath>
#include <cstdio>
#include <vector>
#include <array>
#include <complex>
#include <stdexcept>
#include <algorithm>

namespace rockyenv {

namespace {

constexpr float kG = 9.80665f;
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;

/// 确定性高斯对 (与 WaveField wfHash01 同族, index-hash → Box-Muller)。
float fftHash(int x, int y, int c) {
    float s = std::sin(float(x * 127 + y * 311 + c * 74) * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}
void gaussPair(int x, int y, int c, float& g1, float& g2) {
    const float u1 = std::max(fftHash(x, y, c), 1e-7f);
    const float u2 = fftHash(x, y, c + 1);
    const float r = std::sqrt(-2.0f * std::log(u1));
    g1 = r * std::cos(kTwoPi * u2);
    g2 = r * std::sin(kTwoPi * u2);
}

/// JONSWAP 频谱 (WaveField jonswapHs/jonswapPeakFreq 同源形状)。
/// S(ω) 的频域振幅; ωp=2π/Tp, α 由 Hs 反解保证能量归一。
float jonswapS(float w, float wp, float gamma) {
    if (w <= 1e-5f) return 0.0f;
    const float sigma = (w <= wp) ? 0.07f : 0.09f;
    const float dw = (w - wp) / (sigma * wp);
    const float r = std::exp(-0.5f * dw * dw);
    const float pm = (0.0081f * kG * kG) / std::pow(w, 5.0f) *
                     std::exp(-1.25f * std::pow(wp / w, 4.0f));
    return pm * std::pow(gamma, r);
}

/// Mitsuyasu 方向权重 (与 WaveField mitsuyasuWeight 同式, 未归一)。
float mitsuyasuW(float w, float wp, float u10, float dTheta) {
    if (wp <= 1e-6f || u10 <= 1e-3f) return 1.0f;
    const float mu = (w <= wp) ? 5.0f : -2.5f;
    const float s = 11.5f * std::pow(kG / (wp * u10), 2.5f) * std::pow(w / wp, mu);
    if (s <= 1e-4f) return 1.0f;
    if (s > 60.0f) return (std::abs(dTheta) < 0.05f) ? 1.0f : 0.0f;
    float dt = std::fmod(dTheta, kTwoPi);
    if (dt > kPi) dt -= kTwoPi;
    if (dt < -kPi) dt += kTwoPi;
    const float c = std::cos(dt * 0.5f);
    return std::pow(std::max(c, 0.0f), 2.0f * s);
}

vsg::ref_ptr<vsg::ImageView> makeStorageImg(vsg::Device* device, VkFormat fmt,
                                            uint32_t w, uint32_t h) {
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = fmt;
    img->extent = VkExtent3D{w, h, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return vsg::createImageView(device, img, VK_IMAGE_ASPECT_COLOR_BIT);
}

vsg::ref_ptr<vsg::ImageView> makeSampledImg(vsg::Device* device, VkFormat fmt,
                                            uint32_t w, uint32_t h, vsg::Data* data) {
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = fmt;
    img->extent = VkExtent3D{w, h, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img->data = data;
    return vsg::createImageView(device, img, VK_IMAGE_ASPECT_COLOR_BIT);
}

}  // namespace

struct OceanFFTField::Impl {
    vsg::ref_ptr<vsg::Device> device;
    vsg::ref_ptr<vsg::Sampler> h0Sampler;
    vsg::ref_ptr<vsg::ImageView> h0View;
    // 时域谱 (5 路) + ping-pong IFFT 缓冲
    vsg::ref_ptr<vsg::ImageView> spec[5];    // H, chopX, chopZ, slopeX, slopeZ
    vsg::ref_ptr<vsg::ImageView> fftA[5];
    vsg::ref_ptr<vsg::ImageView> fftB[5];
    vsg::ref_ptr<vsg::PipelineLayout> specLayout, ifftLayout, compLayout;
    vsg::ref_ptr<vsg::ComputePipeline> specPipe, ifftPipe, compPipe;
    vsg::ref_ptr<vsg::DescriptorSet> specDS, compDS;
    vsg::ref_ptr<vsg::DescriptorSet> ifftDS[5][2];  // 每场 × (行/列)
};

OceanFFTField::OceanFFTField() = default;
OceanFFTField::~OceanFFTField() { delete impl_; }

}  // namespace rockyenv

namespace rockyenv {

OceanFFTField::Result OceanFFTField::build(vsg::Window& window, uint32_t N, float patchSize) {
    return buildOffscreen(*window.getOrCreateDevice(),N,patchSize);
}
OceanFFTField::Result OceanFFTField::buildOffscreen(vsg::Device& deviceArg,uint32_t N,float patchSize) {
    Result r;
    if (!impl_) impl_ = new Impl();
    initializeCpu(N, patchSize);
    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);
    impl_->device = device;

    // ---- 采样器 (h0/输出纹理, REPEAT 供平铺采样) ----
    auto rep = vsg::Sampler::create();
    rep->magFilter = VK_FILTER_LINEAR; rep->minFilter = VK_FILTER_LINEAR;
    rep->addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    rep->addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ = rep;
    auto np = vsg::Sampler::create();
    np->magFilter = VK_FILTER_NEAREST; np->minFilter = VK_FILTER_NEAREST;
    np->addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    np->addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    impl_->h0Sampler = np;

    // ---- h0 纹理 (RG32F N×N, CPU 预计算上传) ----
    h0Data_ = vsg::vec2Array2D::create(N, N);
    h0Data_->properties.format = VK_FORMAT_R32G32_SFLOAT;
    h0Data_->properties.dataVariance = vsg::DYNAMIC_DATA;
    impl_->h0View = makeSampledImg(device, VK_FORMAT_R32G32_SFLOAT, N, N, h0Data_);

    // ---- 时域谱 5 路 + IFFT ping-pong (RG32F STORAGE|SAMPLED) ----
    for (int i = 0; i < 5; ++i) {
        impl_->spec[i] = makeStorageImg(device, VK_FORMAT_R32G32_SFLOAT, N, N);
        impl_->fftA[i] = makeStorageImg(device, VK_FORMAT_R32G32_SFLOAT, N, N);
        impl_->fftB[i] = makeStorageImg(device, VK_FORMAT_R32G32_SFLOAT, N, N);
    }
    // ---- 输出: displacement/slopeFoam (RGBA32F STORAGE|SAMPLED) ----
    dispView_ = makeStorageImg(device, VK_FORMAT_R32G32B32A32_SFLOAT, N, N);
    foamView_ = makeStorageImg(device, VK_FORMAT_R32G32B32A32_SFLOAT, N, N);

    // ---- shader ----
    auto shSpec = vsgraysim::earth::earthShader("ocean_fft_spectrum.comp");
    auto shIfft = vsgraysim::earth::earthShader("ocean_ifft.comp");
    auto shComp = vsgraysim::earth::earthShader("ocean_fft_compose.comp");

    // ---- spectrum 管线 (binding0 h0 sampler, 1..5 输出 spec) ----
    {
        vsg::DescriptorSetLayoutBindings b{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        for (uint32_t i = 1; i <= 5; ++i)
            b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        auto dsl = vsg::DescriptorSetLayout::create(b);
        impl_->specLayout = vsg::PipelineLayout::create(
            vsg::DescriptorSetLayouts{dsl},
            vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float) * 6}});
        impl_->specPipe = vsg::ComputePipeline::create(impl_->specLayout, shSpec);
        auto h0Info = vsg::ImageInfo::create(impl_->h0Sampler, impl_->h0View,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vsg::Descriptors ds{vsg::DescriptorImage::create(h0Info, 0)};
        for (uint32_t i = 0; i < 5; ++i)
            ds.push_back(vsg::DescriptorImage::create(
                vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, impl_->spec[i],
                                       VK_IMAGE_LAYOUT_GENERAL), i + 1, 0,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
        impl_->specDS = vsg::DescriptorSet::create(dsl, ds);
    }

    // ---- ifft 管线 (binding0 src storage, 1 dst storage; push N,count,scale,transpose) ----
    {
        vsg::DescriptorSetLayoutBindings b{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        auto dsl = vsg::DescriptorSetLayout::create(b);
        impl_->ifftLayout = vsg::PipelineLayout::create(
            vsg::DescriptorSetLayouts{dsl},
            vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2 + sizeof(float) * 2}});
        impl_->ifftPipe = vsg::ComputePipeline::create(impl_->ifftLayout, shIfft);
        // 每场: 行 (spec→fftA, transpose=0), 列 (fftA→fftB, transpose=1)
        for (int i = 0; i < 5; ++i) {
            impl_->ifftDS[i][0] = vsg::DescriptorSet::create(dsl, vsg::Descriptors{
                vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},
                    impl_->spec[i], VK_IMAGE_LAYOUT_GENERAL), 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
                vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},
                    impl_->fftA[i], VK_IMAGE_LAYOUT_GENERAL), 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)});
            impl_->ifftDS[i][1] = vsg::DescriptorSet::create(dsl, vsg::Descriptors{
                vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},
                    impl_->fftA[i], VK_IMAGE_LAYOUT_GENERAL), 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
                vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},
                    impl_->fftB[i], VK_IMAGE_LAYOUT_GENERAL), 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)});
        }
    }

    // ---- compose 管线 (binding0..4 采样 fftB, 5/6 输出 disp/foam) ----
    {
        vsg::DescriptorSetLayoutBindings b;
        for (uint32_t i = 0; i < 5; ++i)
            b.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        b.push_back({5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        b.push_back({6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        auto dsl = vsg::DescriptorSetLayout::create(b);
        impl_->compLayout = vsg::PipelineLayout::create(
            vsg::DescriptorSetLayouts{dsl},
            vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float) * 4}});
        impl_->compPipe = vsg::ComputePipeline::create(impl_->compLayout, shComp);
        vsg::Descriptors ds;
        for (int i = 0; i < 5; ++i)
            ds.push_back(vsg::DescriptorImage::create(
                vsg::ImageInfo::create(sampler_, impl_->fftB[i], VK_IMAGE_LAYOUT_GENERAL), i));
        ds.push_back(vsg::DescriptorImage::create(
            vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, dispView_, VK_IMAGE_LAYOUT_GENERAL),
            5, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
        ds.push_back(vsg::DescriptorImage::create(
            vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, foamView_, VK_IMAGE_LAYOUT_GENERAL),
            6, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
        impl_->compDS = vsg::DescriptorSet::create(dsl, ds);
    }

    // ---- 每帧 commands: spectrum → barrier → ifft 行/列×5 → barrier → compose ----
    // push 常量对象 (build 期创建, onFrame 改值)
    specPush_ = vsg::Value<SpecPush>::create(SpecPush{0.0f, patch_, kG, -1.0f, 0.65f, 1.0f});
    ifftPushRow_ = vsg::Value<IfftPush>::create(IfftPush{N_, N_, 1.0f / float(N_), 0.0f});
    ifftPushCol_ = vsg::Value<IfftPush>::create(IfftPush{N_, N_, 1.0f / float(N_), 1.0f});
    compPush_ = vsg::Value<CompPush>::create(CompPush{patch_ / float(N_), 0.65f, 1.0f, 0.0f});

    commands_ = vsg::Commands::create();
    // spectrum
    commands_->addChild(vsg::BindComputePipeline::create(impl_->specPipe));
    commands_->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, impl_->specLayout, 0, vsg::DescriptorSets{impl_->specDS}));
    // 布局: 5 路 spec UNDEFINED→GENERAL
    {
        auto pre = vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
        for (int i = 0; i < 5; ++i)
            for(const auto& image:{impl_->spec[i],impl_->fftA[i],impl_->fftB[i]})
                pre->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(
                    0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));
        commands_->addChild(pre);
    }
    commands_->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, specPush_));
    const uint32_t gx = (N_ + 7) / 8, gy = (N_ + 7) / 8;
    commands_->addChild(vsg::Dispatch::create(gx, gy, 1));
    // spec→ifft 读
    commands_->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)));

    // ifft: 每场 行(transpose=0, spec→fftA) + 列(transpose=1, fftA→fftB)
    for (int i = 0; i < 5; ++i) {
        // 行
        commands_->addChild(vsg::BindComputePipeline::create(impl_->ifftPipe));
        commands_->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, impl_->ifftLayout, 0,
            vsg::DescriptorSets{impl_->ifftDS[i][0]}));
        commands_->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, ifftPushRow_));
        commands_->addChild(vsg::Dispatch::create(N_, 1, 1));
        commands_->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)));
        // 列
        commands_->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, impl_->ifftLayout, 0,
            vsg::DescriptorSets{impl_->ifftDS[i][1]}));
        commands_->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, ifftPushCol_));
        commands_->addChild(vsg::Dispatch::create(N_, 1, 1));
        commands_->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)));
    }

    // compose
    commands_->addChild(vsg::BindComputePipeline::create(impl_->compPipe));
    commands_->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, impl_->compLayout, 0, vsg::DescriptorSets{impl_->compDS}));
    {
        auto pre = vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
        pre->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, dispView_->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));
        pre->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, foamView_->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));
        commands_->addChild(pre);
    }
    commands_->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, compPush_));
    commands_->addChild(vsg::Dispatch::create(gx, gy, 1));
    commands_->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)));

    ok_ = true;
    r.ok = true;
    std::printf("[OceanFFTField] P3 GPU FFT 就绪: N=%u patch=%.0fm, 5 路 IFFT\n", N_, patch_);
    return r;
}

}  // namespace rockyenv

namespace rockyenv {

// ===========================================================================
// h0 复振幅重建 (JONSWAP + Mitsuyasu + 确定性高斯相位; 与 WaveField 谱参数同源)
// ===========================================================================
void OceanFFTField::updateSpectrum(const WaveField& wf, float depth_m, float wind_x, float wind_z) {
    std::lock_guard<std::mutex> lock(cpu_mutex_);
    depth_ = depth_m;
    h0_.assign(size_t(N_)*N_,{});
    cpu_time_=std::numeric_limits<float>::quiet_NaN();
    const uint32_t N = N_;
    const float L = patch_;
    const float hs = wf.significantWaveHeight();
    const float tp = wf.peakPeriod();
    const float wp = (tp > 1e-4f) ? (kTwoPi / tp) : 1.0f;
    // 风速反解: JONSWAP Hs ≈ 0.21·U²/g (PM 上限) 是下界; 取 seaState 蒲福对应
    // 风区中值更稳。此处用 Hs 反推 U10 (PM 式), 仅用于 Mitsuyasu 方向宽度。
    const float u10 = std::max(std::sqrt(std::max(hs, 0.01f) * kG / 0.21f), 0.5f);
    const float gamma = 3.3f;   // JONSWAP 标准峰增强

    // α 归一: 使 4·√(ΣS·dk²) ≈ Hs (Hs=4σ, σ²=∫∫S·D dkx dky)
    std::vector<float> ssum(N * N, 0.0f);
    double total = 0.0;
    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            const float kx = kTwoPi * float(int(x) - int(N) / 2) / L;
            const float kz = kTwoPi * float(int(y) - int(N) / 2) / L;
            const float kLen = std::sqrt(kx * kx + kz * kz);
            if (kLen < 1e-5f) continue;
            const float w = std::sqrt(kG * kLen);
            const float theta = std::atan2(kz, kx) - std::atan2(-wind_z, -wind_x);   // 相对 +x (风向由外部统一旋转)
            // 方向相对风向 (0): Mitsuyasu 以 +x 为轴; 渲染端 UV 旋转对齐实际风向
            const float dw = mitsuyasuW(w, wp, u10, theta);
            const float s = jonswapS(w, wp, gamma) * dw;
            ssum[y * N + x] = s;
            total += double(s);
        }
    }
    if (total <= 0.0) total = 1.0;
    // σ²_target = (Hs/4)²; 当前 σ² = ΣS·dkx·dky; 幅值 ∝ √S
    const float dk = kTwoPi / L;
    const double sigma2 = double(hs / 4.0f) * double(hs / 4.0f);
    const double cur = total * double(dk) * double(dk);
    const float norm = (cur > 0.0) ? float(std::sqrt(sigma2 / cur)) : 1.0f;

    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            const float s = ssum[y * N + x] * norm * norm;   // 能量归一
            // h0 = √(S·dk²/2)·(ξr+iξi)
            // IFFT is normalized by N². h0(k) is paired with h0*(-k),
            // so its expected variance has a factor two.
            const float amp = std::sqrt(std::max(s, 0.0f) * dk * dk * 0.5f) * float(N*N) / std::sqrt(2.f);
            float g1, g2;
            gaussPair(int(x), int(y), 7, g1, g2);
            h0_[size_t(y)*N+x]={amp*g1,amp*g2};
        }
    }
    // Preserve the configured swell in the very same Fourier field. Frequencies
    // are quantized to this patch, and normalized to the requested swell Hs.
    const auto& harmonics=wf.harmonics();
    const size_t first=std::min<size_t>(wf.windHarmonicCount(),harmonics.size());
    double variance=0;for(size_t i=first;i<harmonics.size();++i)variance+=.5*harmonics[i].amplitude*harmonics[i].amplitude;
    const double scale=variance>0 ? wf.swellWaveHeight()/(4*std::sqrt(variance)) : 0;
    for(size_t i=first;i<harmonics.size();++i) {
        const auto& h=harmonics[i];
        const int kx=int(std::round(-h.dir_cos*L/h.wave_length));
        const int kz=int(std::round(-h.dir_sin*L/h.wave_length));
        if((kx==0&&kz==0)||std::abs(kx)>=int(N/2)||std::abs(kz)>=int(N/2))continue;
        const auto index=size_t(kz+int(N/2))*N+size_t(kx+int(N/2));
        h0_[index]+=std::polar(float(.5*scale*h.amplitude*N*N),h.phase);
    }
    if(h0Data_) {
        auto& arr=*static_cast<vsg::vec2Array2D*>(h0Data_.get());
        for(uint32_t y=0;y<N;++y)for(uint32_t x=0;x<N;++x) {
            const auto h=h0_[size_t(y)*N+x];arr.set(x,y,{h.real(),h.imag()});
        }
        h0Data_->dirty();
    }
}

// ===========================================================================
// 每帧驱动
// ===========================================================================
void OceanFFTField::onFrame(float time_sec, float depth_m, float chop_q, float height_scale) {
    std::lock_guard<std::mutex> lock(cpu_mutex_);
    if(depth_!=depth_m || chopQ_!=chop_q || heightScale_!=height_scale)
        cpu_time_=std::numeric_limits<float>::quiet_NaN();
    time_ = time_sec; depth_=depth_m; chopQ_ = chop_q; heightScale_ = height_scale;
    if (specPush_) {
        specPush_->value() = SpecPush{time_sec, patch_, kG,
                                      (depth_m > 0.0f) ? depth_m : depth_,
                                      chop_q, height_scale};
    }
    if (compPush_) {
        compPush_->value() = CompPush{patch_ / float(N_), chop_q, 1.0f, 0.0f};
    }
}

}  // namespace rockyenv

namespace rockyenv {
void OceanFFTField::initializeCpu(uint32_t N,float patchSize) {
    if(N<4 || N>512 || (N&(N-1)) || !std::isfinite(patchSize) || patchSize<=0)
        throw std::invalid_argument("ocean FFT requires a power-of-two grid 4..512 and positive patch");
    std::lock_guard<std::mutex> lock(cpu_mutex_);
    N_=N;patch_=patchSize;h0_.assign(size_t(N)*N,{});
    cpu_displacement_.clear();cpu_normal_.clear();cpu_velocity_.clear();
    cpu_time_=std::numeric_limits<float>::quiet_NaN();
}
namespace {
using Complex=std::complex<float>;
// Stockham radix-2, the same order and normalization as ocean_ifft.comp.
void inverseLine(std::vector<Complex>& line,std::vector<Complex>& other,const std::vector<Complex>& roots) {
    const size_t N=line.size();
    for(size_t span=1;span<N;span<<=1) {
        for(size_t tid=0;tid<N/2;++tid) {
            const size_t k=tid&(span-1),i=2*(tid-k)+k;
            const auto tw=roots[k*N/(2*span)];
            const auto a=line[tid],b=tw*line[tid+N/2];
            other[i]=a+b;other[i+span]=a-b;
        }
        line.swap(other);
    }
    for(auto& value:line)value/=float(N);
}
void inverse2D(std::vector<Complex>& data,uint32_t N) {
    std::vector<Complex> line(N),other(N),roots(N/2);
    for(uint32_t k=0;k<N/2;++k)roots[k]=std::polar(1.f,float(6.28318530718*k/N));
    for(uint32_t y=0;y<N;++y) {
        std::copy_n(data.begin()+size_t(y)*N,N,line.begin());inverseLine(line,other,roots);
        std::copy(line.begin(),line.end(),data.begin()+size_t(y)*N);
    }
    for(uint32_t x=0;x<N;++x) {
        for(uint32_t y=0;y<N;++y)line[y]=data[size_t(y)*N+x];inverseLine(line,other,roots);
        for(uint32_t y=0;y<N;++y)data[size_t(y)*N+x]=line[y];
    }
}
}
void OceanFFTField::updateCpuFields(float time) const {
    const auto N=N_;const size_t count=size_t(N)*N;
    std::array<std::vector<Complex>,8> fields;
    for(auto& field:fields)field.resize(count);
    for(uint32_t y=0;y<N;++y)for(uint32_t x=0;x<N;++x) {
        const size_t index=size_t(y)*N+x;
        const float kx=kTwoPi*float(int(x)-int(N/2))/patch_;
        const float kz=kTwoPi*float(int(y)-int(N/2))/patch_;
        const float k=std::hypot(kx,kz);
        const float omega=k>1e-6f ? std::sqrt(kG*k*std::tanh(depth_>0 ? k*depth_ : 1000.f)) : 0;
        const auto e=std::polar(1.f,omega*time);
        const auto a=h0_[index]*e;
        const auto b=std::conj(h0_[size_t((N-y)%N)*N+(N-x)%N])*std::conj(e);
        const auto h=(a+b)*heightScale_;
        const auto velocity=Complex(0,omega)*(a-b)*heightScale_;
        const auto cx=k>1e-6f ? Complex(0,-kx/k*chopQ_) : Complex{};
        const auto cz=k>1e-6f ? Complex(0,-kz/k*chopQ_) : Complex{};
        fields[0][index]=h;fields[1][index]=cx*h;fields[2][index]=cz*h;
        fields[3][index]=Complex(0,kx)*h;fields[4][index]=Complex(0,kz)*h;
        fields[5][index]=velocity;fields[6][index]=cx*velocity;fields[7][index]=cz*velocity;
    }
    for(auto& field:fields)inverse2D(field,N);
    cpu_displacement_.resize(count);cpu_normal_.resize(count);cpu_velocity_.resize(count);
    for(uint32_t y=0;y<N;++y)for(uint32_t x=0;x<N;++x) {
        const size_t i=size_t(y)*N+x;const float sign=((x+y)&1)?-1.f:1.f;
        cpu_displacement_[i]=vsg::vec4{sign*fields[1][i].real(),sign*fields[0][i].real(),sign*fields[2][i].real(),0};
        const auto normal=vsg::normalize(vsg::vec3{-sign*fields[3][i].real(),1,-sign*fields[4][i].real()});
        cpu_normal_[i]=vsg::vec4{normal.x,normal.y,normal.z,1};
        cpu_velocity_[i]=vsg::vec4{sign*fields[6][i].real(),sign*fields[5][i].real(),sign*fields[7][i].real(),0};
    }
    const float two=2*patch_/N;
    for(uint32_t y=0;y<N;++y)for(uint32_t x=0;x<N;++x) {
        const auto l=cpu_displacement_[size_t(y)*N+(x+N-1)%N],r=cpu_displacement_[size_t(y)*N+(x+1)%N];
        const auto d=cpu_displacement_[size_t((y+N-1)%N)*N+x],u=cpu_displacement_[size_t((y+1)%N)*N+x];
        cpu_normal_[size_t(y)*N+x].w=(1+(r.x-l.x)/two)*(1+(u.z-d.z)/two)-(u.x-d.x)*(r.z-l.z)/(two*two);
    }
    cpu_time_=time;
}
OceanFFTField::Sample OceanFFTField::sample(double x,double z,double time) const {
    if(!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(time))throw std::invalid_argument("invalid ocean sample coordinate/time");
    std::lock_guard<std::mutex> lock(cpu_mutex_);
    if(h0_.empty())throw std::logic_error("ocean spectrum is not initialized");
    const float timestamp=float(time);if(cpu_time_!=timestamp)updateCpuFields(timestamp);
    const double px=(x/patch_+.5)*N_-.5,pz=(z/patch_+.5)*N_-.5;
    const double ix=std::floor(px),iz=std::floor(pz);const float fx=float(px-ix),fz=float(pz-iz);
    auto wrap=[&](double value){return uint32_t(std::fmod(std::fmod(value,double(N_))+N_,double(N_)));};
    auto interpolate=[&](const std::vector<vsg::vec4>& values) {
        const auto a=values[size_t(wrap(iz))*N_+wrap(ix)],b=values[size_t(wrap(iz))*N_+wrap(ix+1)];
        const auto c=values[size_t(wrap(iz+1))*N_+wrap(ix)],d=values[size_t(wrap(iz+1))*N_+wrap(ix+1)];
        return (a*(1-fx)+b*fx)*(1-fz)+(c*(1-fx)+d*fx)*fz;
    };
    Sample result;const auto disp=interpolate(cpu_displacement_),normal=interpolate(cpu_normal_),velocity=interpolate(cpu_velocity_);
    result.displacement={disp.x,disp.y,disp.z};result.normal=vsg::normalize(vsg::vec3{normal.x,normal.y,normal.z});
    result.jacobian=normal.w;result.velocity={velocity.x,velocity.y,velocity.z};return result;
}
} // namespace rockyenv
