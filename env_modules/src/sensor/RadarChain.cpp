/// @file RadarChain.cpp —— B6/P1.1: 雷达回波链 GPU 前端 + P1 段处理 GPU 化 (纯 compute)。
///
/// P1.1 改造: 散射体提取由 radar_scatter RT 三段改为 radar_scatter.comp
/// (读 P1.1 GBufferPool 的雷达视角 G-buffer → 散射体缓冲), 本链整链纯 compute:
///   清零(compute) → 散射体提取(compute) → [barrier 细分] → 回波合成(compute)
///   → 脉压 FFT×2 → 慢时间 FFT → RD 图 → product。
/// P1.2: 常驻命令挂独立 vsg::CommandGraph (compute CG), 由上层挂入独立
/// RecordAndSubmitTask —— record/submit 解耦由 task 承担, 队列间依赖经
/// semaphore 声明 (成像 G-buffer trace 完成 → 雷达链启动)。
/// P1.3: 段间全局 memory barrier 改为按 buffer 的 VkBufferMemoryBarrier
/// (echo/tmp/range/rd/product 各自), 允许驱动重叠无关资源访问。
/// 回读与清零解耦: readback* 仅 dump/验收路径 (一次性传输 CB, 不再清零)。
#include "EnvCommon.h"          // resolveEnvShader (src/ 私有)
#include "rocky_env/RadarChain.h"
#include "rocky_env/RadarSampling.h"
#include "rocky_env/GBufferPool.h"
#include "rocky_env/SharedSceneResources.h"

#include <vsg/all.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/commands/Commands.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

namespace rockyenv {
namespace sensor {

namespace {

constexpr uint32_t kMaxAnt = 256;   // UBO ant/beam 数组容量 (= 着色器)
constexpr uint32_t kEchoSeg = 8;    // P2.1 合成 pass 散射体段数 (并行度: 组数 = 块×脉冲×S)

// ---- P2 验收: echo kernel GPU 时间戳命令 (vsg 1.1.15 无 QueryPool 封装) ----
struct WriteTimestamp : vsg::Inherit<vsg::Command, WriteTimestamp> {
    VkQueryPool pool = VK_NULL_HANDLE;
    uint32_t slot = 0;
    void record(vsg::CommandBuffer& cb) const override {
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, pool, slot);
    }
};
struct ResetTimestamps : vsg::Inherit<vsg::Command, ResetTimestamps> {
    VkQueryPool pool = VK_NULL_HANDLE;
    void record(vsg::CommandBuffer& cb) const override {
        vkCmdResetQueryPool(cb, pool, 0, 4);   // 链首复位, 每帧时间戳独立
    }
};

bool loadStage(VkShaderStageFlagBits stage, const char* name,
               vsg::ref_ptr<vsg::ShaderStage>& out) {
    out = vsg::ShaderStage::read(stage, "main", resolveEnvShader(name));
    if (!out) {
        std::fprintf(stderr, "[RadarChain] shader 加载失败: %s\n", name);
        return false;
    }
    return true;
}

// FFT/product push 块 (与 radar_fft.comp / radar_product.comp 严格对齐, 32B)。
struct FftPush {
    vsg::uivec4 u;   // (N, count, W, multRef)
    vsg::vec4 f;    // (scale, sign, transpose, 0)
};
struct ProdPush {
    vsg::uivec4 u;   // (nCells, nGroups, 0, 0)
    vsg::vec4 f;    // (mode, 0, 0, 0)
};

// 就地 radix-2 FFT (double, 脉压参考谱预计算; 与 RadarStages::fft 同约定)。
void fftRef(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = (inverse ? 2.0 : -2.0) * M_PI / double(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= double(n);
}

// 一次性传输 CB: device local → host visible 拷出 (dump/验收路径专用)。
bool readbackDeviceBuffer(vsg::Device& deviceArg, vsg::Buffer* src,
                          VkDeviceSize bytes, void* dst) {
    if (!src) return false;
    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);
    const int gfxFamily = device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    auto queue = device->getQueue(uint32_t(gfxFamily));

    auto host = vsg::Buffer::create(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_SHARING_MODE_EXCLUSIVE);
    host->compile(device.get());
    auto hostMem = vsg::DeviceMemory::create(device.get(), host->getMemoryRequirements(device->deviceID),
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    host->bind(hostMem, 0);

    auto pool = vsg::CommandPool::create(device, uint32_t(gfxFamily));
    auto cb = pool->allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    // [B6] 帧提交 (compute) 与读回 CB 间存在跨提交竞争 (两次运行结果漂移
    // 实证), 验收路径设备空闲等待换确定性。
    if(queue->waitIdle()!=VK_SUCCESS) return false;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb->vk(), &bi);

    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb->vk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

    VkBufferCopy region{0, 0, bytes};
    vkCmdCopyBuffer(cb->vk(), src->vk(device->deviceID), host->vk(device->deviceID), 1, &region);
    vkEndCommandBuffer(cb->vk());
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    VkCommandBuffer vkCb = cb->vk();
    si.pCommandBuffers = &vkCb;
    auto fence = vsg::Fence::create(device);
    if (queue->submit(si, fence.get()) != VK_SUCCESS || fence->wait(UINT64_MAX) != VK_SUCCESS) {
        std::fprintf(stderr, "[RadarChain] 缓冲回读提交失败\n");
        return false;
    }
    void* mapped = nullptr;
    if (hostMem->map(0, bytes, 0, &mapped) != VK_SUCCESS || !mapped) {
        std::fprintf(stderr, "[RadarChain] 缓冲 map 失败\n");
        return false;
    }
    std::memcpy(dst, mapped, size_t(bytes));
    hostMem->unmap();
    return true;
}

} // namespace

// ===========================================================================
// RadarChain::build
// ===========================================================================
RadarChain::Result RadarChain::build(vsg::Window& window,
                                     GBufferPool* gbuffer,
                                     const RadarChainParams& params,
                                     const std::vector<RadarMaterial>* materials,
                                     SharedSceneResources* shared) { return build(*window.getOrCreateDevice(), gbuffer, params, materials, shared); }

RadarChain::Result RadarChain::build(vsg::Device& deviceArg,
                                     GBufferPool* gbuffer,
                                     const RadarChainParams& params,
                                     const std::vector<RadarMaterial>* materials,
                                     SharedSceneResources* shared) {
    Result result;
    params_ = params;
    try {
        params_.sample_rate_hz = RadarSampling::make(params_.bandwidth_hz, params_.sample_rate_hz).rate_hz;
    } catch (const std::invalid_argument& e) {
        result.error = e.what();
        return result;
    }
    if (!std::isfinite(params_.transmit_power_w) || params_.transmit_power_w<0 ||
        !std::isfinite(params_.receiver_noise_iq_rms) || params_.receiver_noise_iq_rms<0 ||
        !std::isfinite(params_.prf) || params_.prf <= 0.0 ||
        !std::isfinite(params_.lambda) || params_.lambda <= 0.0 ||
        !std::isfinite(params_.pulse_width_s) || params_.pulse_width_s <= 0.0 ||
        params_.cpi_pulses < 2 || (params_.cpi_pulses & (params_.cpi_pulses - 1)) != 0) {
        result.error = "invalid radar timing, wavelength or CPI (requires power of two >= 2)";
        return result;
    }
    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);

    // ---- P2 验收: echo kernel 时间戳查询池 (NVIDIA timestampPeriod≈1ns/tick) ----
    tsDevice_ = device;
    {
        VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpi.queryCount = 4;   // 槽0=分桶前 / 槽1=echo前 / 槽2=echo后
        if (vkCreateQueryPool(device->vk(), &qpi, nullptr, &tsPool_) == VK_SUCCESS) {
            tsPeriodNs_ = double(device->getPhysicalDevice()->getProperties().limits.timestampPeriod);
            tsValid_ = true;
        } else {
            std::fprintf(stderr, "[RadarChain] 时间戳查询池创建失败 (耗时统计不可用)\n");
        }
    }
    const uint32_t w = params_.width, h = params_.height;
    const uint32_t nFast = params_.n_fast;
    if (params_.cpi_pulses > kMaxAnt) {
        result.error = "cpi_pulses > 256 (UBO 容量)";
        return result;
    }
    // P2.2: n_fast 扩容至 4096 —— 行变换 512·F 分段 (radar_fft 模式2 + tail),
    // 列变换走全局转置; n_fast ≤ 512 保持原单段路径 (零回归)。
    if (nFast < 2 || (nFast & (nFast - 1)) != 0 || nFast > 4096) {
        result.error = "n_fast 须为 2..4096 的 2 的幂 (P2.2 扩容上限)";
        return result;
    }
    if (nFast > 512 && (nFast % 512) != 0) {
        result.error = "n_fast>512 须为 512·F (F=2/4/8) —— 行变换分段约束";
        return result;
    }
    const uint32_t nScat = w * h;
    const uint32_t nCells = params_.cpi_pulses * params_.n_fast;
    const VkDeviceSize cpxBytes = VkDeviceSize(2) * nCells * sizeof(float);   // 复数交错

    // ---- P1.1 G-buffer 池判定: 必需 (散射提取消费者) ----
    if (!gbuffer || !gbuffer->ready()) {
        result.error = "G-buffer 池缺失/未就绪 (P1.1 散射提取依赖)";
        return result;
    }
    if (gbuffer->width() != w || gbuffer->height() != h) {
        result.error = "G-buffer 分辨率与散射体栅格不符 (" +
                       std::to_string(gbuffer->width()) + "x" + std::to_string(gbuffer->height()) +
                       " vs " + std::to_string(w) + "x" + std::to_string(h) + ")";
        return result;
    }
    shared_ = shared;

    // ---- σ 材质表 SSBO (P1.1: 从 G-buffer 池取, 与池同源) ----
    auto matInfo = gbuffer->matDataInfo;
    if (!matInfo) {
        result.error = "G-buffer 池材质表缺失";
        return result;
    }

    // ---- 散射体 SSBO (compute 写 / compute 读, 每槽位 2 vec4; 自管 device local ——
    //      P1.3 barrier 在 build 期需要有效 buffer 引用, 不用 vsg 延迟分配) ----
    const VkDeviceSize scatBytes = VkDeviceSize(nScat) * sizeof(vsg::vec4);   // [pos.xyz, hit] / [σ.re, σ.im, R0, 0]
    auto makeScatter = [&](vsg::ref_ptr<vsg::Buffer>& buf,
                           vsg::ref_ptr<vsg::DeviceMemory>& mem) {
        buf = vsg::Buffer::create(scatBytes,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_SHARING_MODE_EXCLUSIVE);
        buf->compile(device.get());
        mem = vsg::DeviceMemory::create(device.get(), buf->getMemoryRequirements(device->deviceID),
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        buf->bind(mem, 0);
    };
    makeScatter(scatterPosBuf_, scatterPosMem_);
    makeScatter(scatterSigBuf_, scatterSigMem_);
    scatterPosInfo_ = vsg::BufferInfo::create(scatterPosBuf_, 0, scatBytes);
    scatterSigInfo_ = vsg::BufferInfo::create(scatterSigBuf_, 0, scatBytes);

    // ---- 回波 SSBO (自管 device local: STORAGE|TRANSFER_SRC|TRANSFER_DST) ----
    echoBuf_ = vsg::Buffer::create(cpxBytes,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_SHARING_MODE_EXCLUSIVE);
    echoBuf_->compile(device.get());
    echoMem_ = vsg::DeviceMemory::create(device.get(), echoBuf_->getMemoryRequirements(device->deviceID),
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    echoBuf_->bind(echoMem_, 0);
    auto echoInfo = vsg::BufferInfo::create(echoBuf_, 0, cpxBytes);

    // ---- P1 段处理缓冲 (device local; 复数中间 ×3 + 幅度 + 组峰值) ----
    auto makeStorage = [&](VkDeviceSize bytes, VkBufferUsageFlags extra,
                           vsg::ref_ptr<vsg::Buffer>& buf,
                           vsg::ref_ptr<vsg::DeviceMemory>& mem) -> vsg::ref_ptr<vsg::BufferInfo> {
        buf = vsg::Buffer::create(bytes,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | extra,
                                  VK_SHARING_MODE_EXCLUSIVE);
        buf->compile(device.get());
        mem = vsg::DeviceMemory::create(device.get(), buf->getMemoryRequirements(device->deviceID),
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        buf->bind(mem, 0);
        return vsg::BufferInfo::create(buf, 0, bytes);
    };
    auto tmpInfo   = makeStorage(cpxBytes, 0, tmpBuf_, tmpMem_);
    auto rangeInfo = makeStorage(cpxBytes, 0, rangeBuf_, rangeMem_);
    auto rdInfo    = makeStorage(cpxBytes, 0, rdBuf_, rdMem_);
    const VkDeviceSize prodBytes = VkDeviceSize(nCells) * sizeof(float);
    auto prodInfo  = makeStorage(prodBytes, 0, prodBuf_, prodMem_);
    const uint32_t nGroupsProd = (nCells + 255) / 256;
    auto gmaxInfo  = makeStorage(VkDeviceSize(nGroupsProd) * sizeof(float), 0, gmaxBuf_, gmaxMem_);

    // ---- P2.1 分桶 CSR 缓冲 (uint SSBO; offs 多 1 槽存桶尾界=有效散射体总数) ----
    auto countInfo  = makeStorage(VkDeviceSize(nFast) * 4, 0, countBuf_, countMem_);
    auto cursorInfo = makeStorage(VkDeviceSize(nFast) * 4, 0, cursorBuf_, cursorMem_);
    auto offsInfo   = makeStorage((VkDeviceSize(nFast) + 1) * 4, 0, offsBuf_, offsMem_);
    auto spanInfo   = makeStorage(4, 0, spanBuf_, spanMem_);
    auto metaInfo   = makeStorage(VkDeviceSize(nScat) * 4, 0, metaBuf_, metaMem_);
    auto idxInfo    = makeStorage(VkDeviceSize(nScat) * 4, 0, idxBuf_, idxMem_);
    // 桶序载荷重排 (散布写 → 合成 coalesced 读, 消除逐脉冲随机回读)
    auto posSortInfo = makeStorage(scatBytes, 0, posSortBuf_, posSortMem_);
    auto sigSortInfo = makeStorage(scatBytes, 0, sigSortBuf_, sigSortMem_);
    auto metaSortInfo = makeStorage(VkDeviceSize(nScat) * 4, 0, metaSortBuf_, metaSortMem_);
    // 段部分和 (S 份 echo 布局; 合成 pass 每组单写者槽, reduce pass 无原子汇总)
    auto partSumInfo = makeStorage(cpxBytes * kEchoSeg, 0, partSumBuf_, partSumMem_);
    const uint32_t groundTileWidth=(w+63)/64,groundTileHeight=(h+63)/64;
    terrainCells_=params_.terrain_footprint?
        ((w+groundTileWidth-1)/groundTileWidth)*((h+groundTileHeight-1)/groundTileHeight):0;
    const VkDeviceSize groundBytes=VkDeviceSize(terrainCells_)*sizeof(vsg::vec4);
    auto groundInfo=terrainCells_?makeStorage(groundBytes,0,terrainBuf_,terrainMem_):nullptr;
    // ---- P2.2 行变换分段 + 列转置中间缓冲 (仅 n_fast > 512) ----
    vsg::ref_ptr<vsg::BufferInfo> tmpAInfo, tmpBInfo, tmp2Info, rdTInfo;
    if (nFast > 512) {
        tmpAInfo = makeStorage(cpxBytes, 0, tmpABuf_, tmpAMem_);
        tmpBInfo = makeStorage(cpxBytes, 0, tmpBBuf_, tmpBMem_);
        tmp2Info = makeStorage(cpxBytes, 0, tmp2Buf_, tmp2Mem_);
        rdTInfo  = makeStorage(cpxBytes, 0, rdTBuf_, rdTMem_);
    }

    // ---- P1 脉压共轭参考谱 (CPU 预计算, host visible 上传一次) ----
    {
        const double fs = params_.sample_rate_hz;
        const double K = params_.bandwidth_hz / std::max(params_.pulse_width_s, 1e-12);
        const uint32_t nChirp = std::min<uint32_t>(
            params_.n_fast, uint32_t(std::round(params_.pulse_width_s * fs)));
        std::vector<std::complex<double>> ref(params_.n_fast);
        for (uint32_t n = 0; n < nChirp; ++n) {
            const double t = double(n) / fs;
            ref[n] = std::polar(1.0, M_PI * K * t * t);
        }
        fftRef(ref, false);   // 正变换 → 共轭谱 (GPU 载入时乘, 逆变换在 pass B)
        const VkDeviceSize refBytes = VkDeviceSize(params_.n_fast) * sizeof(vsg::vec2);
        refSpecBuf_ = vsg::Buffer::create(refBytes,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          VK_SHARING_MODE_EXCLUSIVE);
        refSpecBuf_->compile(device.get());
        refSpecMem_ = vsg::DeviceMemory::create(device.get(), refSpecBuf_->getMemoryRequirements(device->deviceID),
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        refSpecBuf_->bind(refSpecMem_, 0);
        void* mapped = nullptr;
        if (refSpecMem_->map(0, refBytes, 0, &mapped) != VK_SUCCESS || !mapped) {
            result.error = "参考谱上传 map 失败";
            return result;
        }
        auto* rf = static_cast<float*>(mapped);
        for (uint32_t i = 0; i < params_.n_fast; ++i) {
            rf[2 * i + 0] = float(ref[i].real());
            rf[2 * i + 1] = float(-ref[i].imag());   // conj(ref 谱)
        }
        refSpecMem_->unmap();
    }
    auto refSpecInfo = vsg::BufferInfo::create(refSpecBuf_, 0,
                                               VkDeviceSize(params_.n_fast) * sizeof(vsg::vec2));

    // ---- compute 参数 UBO: ant[256] + beam[256] + m0 + m1 + m2 (DYNAMIC_DATA) ----
    constexpr uint32_t kUboFloats = kMaxAnt * 8 + 16;
    paramsData_ = vsg::floatArray::create(kUboFloats);
    std::memset(paramsData_->dataPointer(), 0, size_t(kUboFloats) * sizeof(float));
    paramsData_->properties.dataVariance = vsg::DYNAMIC_DATA;
    float* p = paramsData_->data(kMaxAnt * 8);
    p[0] = float(params_.lambda);
    p[1] = 2.99792458e8f;
    p[2] = float(params_.range_min_m);
    p[3] = float(1.0 / params_.sample_rate_hz); // physical fast-time period, not FFT bin count
    p[4] = float(params_.cpi_pulses);
    p[5] = float(params_.n_fast);
    p[6] = 0.0f;                                // m1.z enable (setPulses 置位)
    p[7] = float(nScat);
    // m2: P5 (beamWidth, gExp, hasBeam, 0) —— gExp 由 -3dB 波束宽度标定:
    // cos(bw)^gExp = 0.5 → gExp = ln0.5 / ln cos(bw) (限幅防退化)
    p[8] = float(params_.beam_width_rad);
    float gExp = 1.0f;
    if (params_.beam_width_rad > 0.0 && params_.beam_width_rad < 1.5) {
        gExp = std::clamp(float(std::log(0.5) / std::log(std::cos(float(params_.beam_width_rad)))),
                          1.0f, 5000.0f);
    }
    p[9] = gExp;
    p[10] = 0.0f;                               // m2.z hasBeam (setPulses 置位)
    p[12] = float(params_.receiver_noise_iq_rms);

    // ---- 散射提取描述符 (b0 G0 / 1 G1 / 2 scatterPos / 3 scatterSig / 4 matData) ----
    auto scatDsl = vsg::DescriptorSetLayout::create();
    scatDsl->addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    scatDsl->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    for (uint32_t b = 2; b <= 4; ++b)
        scatDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto g0Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, gbuffer->g0,
                                         VK_IMAGE_LAYOUT_GENERAL);
    auto g1Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, gbuffer->g1,
                                         VK_IMAGE_LAYOUT_GENERAL);
    auto scatSet = vsg::DescriptorSet::create(scatDsl, vsg::Descriptors{
        vsg::DescriptorImage::create(g0Info, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(g1Info, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterPosInfo_}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterSigInfo_}, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{matInfo}, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });

    // ---- 回波 compute 描述符 (P2.1 CSR 消费: b0 posSorted / 1 sigSorted /
    //      2 partSum / 3 params UBO / 4 metaSorted / 5 offs / 6 span) ----
    auto compDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 6; ++b) {
        compDsl->addBinding(b,
                            b == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            1, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    auto compSet = vsg::DescriptorSet::create(compDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{posSortInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{sigSortInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{partSumInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(paramsData_, 3, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{metaSortInfo}, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{offsInfo}, 5, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{spanInfo}, 6, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    // 段归约 reduce: b0 partSum / b1 echo
    auto reduceDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 1; ++b)
        reduceDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    reduceDsl->addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto reduceSet = vsg::DescriptorSet::create(reduceDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{partSumInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{echoInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(paramsData_, 2, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
    });

    // ---- P2.1 分桶/扫描/散布 描述符 (zero/bin/scan/csr) ----
    auto zeroDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 1; ++b)
        zeroDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto zeroSet = vsg::DescriptorSet::create(zeroDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{countInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{spanInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    auto binDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 5; ++b) {
        binDsl->addBinding(b,
                           b == 5 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           1, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    auto binSet = vsg::DescriptorSet::create(binDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterPosInfo_}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterSigInfo_}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{countInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{spanInfo}, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{metaInfo}, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(paramsData_, 5, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
    });
    auto scanDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 2; ++b)
        scanDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto scanSet = vsg::DescriptorSet::create(scanDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{countInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{offsInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{cursorInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    auto csrDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 7; ++b)
        csrDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto csrSet = vsg::DescriptorSet::create(csrDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{metaInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{cursorInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{idxInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterPosInfo_}, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterSigInfo_}, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{posSortInfo}, 5, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{sigSortInfo}, 6, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{metaSortInfo}, 7, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });

    // ---- P1 段处理描述符 ----
    // clear: b0 echo
    auto clearDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 1; ++b)
        clearDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto clearSet = vsg::DescriptorSet::create(clearDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{echoInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{partSumInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    // fft: b0 src / b1 dst / b2 refSpec (三 pass 各一组 set)
    auto fftDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 2; ++b)
        fftDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto fftSetA = vsg::DescriptorSet::create(fftDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{echoInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{refSpecInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    auto fftSetB = vsg::DescriptorSet::create(fftDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{rangeInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{refSpecInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    auto fftSetC = vsg::DescriptorSet::create(fftDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{rangeInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{rdInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{refSpecInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    // P2.2 分段/转置描述符 (仅 n_fast > 512, 中间缓冲存在时创建):
    //   R1_A: echo→tmpA (512 点分段子正变换)  R1_B: tmp→tmpB (分段子逆变换, 载入乘共轭谱)
    //   C2: tmp2→rdT (列变换行模式, 转置后布局 [e][t])
    //   tail: b0 src / b1 dst (A: tmpA→tmp, B: tmpB→range)
    //   trans: b0 src / b1 dst (1: range→tmp2, 2: rdT→rd)
    vsg::ref_ptr<vsg::DescriptorSet> tailSetA, tailSetB, transSet1, transSet2;
    vsg::ref_ptr<vsg::DescriptorSet> fftSetR1A, fftSetR1B, fftSetC2;
    vsg::ref_ptr<vsg::PipelineLayout> tailLayout, transLayout;
    if (nFast > 512) {
        fftSetR1A = vsg::DescriptorSet::create(fftDsl, vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{echoInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpAInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{refSpecInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        });
        fftSetR1B = vsg::DescriptorSet::create(fftDsl, vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpBInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{refSpecInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        });
        fftSetC2 = vsg::DescriptorSet::create(fftDsl, vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmp2Info}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{rdTInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{refSpecInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        });
        auto tailDsl = vsg::DescriptorSetLayout::create();
        for (uint32_t b = 0; b <= 1; ++b)
            tailDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
        tailSetA = vsg::DescriptorSet::create(tailDsl, vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpAInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        });
        tailSetB = vsg::DescriptorSet::create(tailDsl, vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmpBInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{rangeInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        });
        auto transDsl = vsg::DescriptorSetLayout::create();
        for (uint32_t b = 0; b <= 1; ++b)
            transDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
        transSet1 = vsg::DescriptorSet::create(transDsl, vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{rangeInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{tmp2Info}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        });
        transSet2 = vsg::DescriptorSet::create(transDsl, vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{rdTInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{rdInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        });
        tailLayout = vsg::PipelineLayout::create(
            vsg::DescriptorSetLayouts{tailDsl},
            vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FftPush)}});
        transLayout = vsg::PipelineLayout::create(
            vsg::DescriptorSetLayouts{transDsl},
            vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(vsg::uivec4)}});
    }
    // product: b0 rd / b1 prod / b2 gmax (两 pass 共享 set, push 区分)
    auto prodDsl = vsg::DescriptorSetLayout::create();
    for (uint32_t b = 0; b <= 2; ++b)
        prodDsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    auto prodSet = vsg::DescriptorSet::create(prodDsl, vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{rdInfo}, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{prodInfo}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{gmaxInfo}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });

    // ---- compute shader 加载 (散射 + 分桶 4 段 + 回波/归约 + FFT/product + 条件 tail/trans) ----
    vsg::ref_ptr<vsg::ShaderStage> shScat, shClear, shZero, shBin, shScan, shCsr,
                                   shEcho, shReduce, shFft, shProduct, shTail, shTrans, shGround;
    if (!loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_scatter.comp", shScat) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_clear.comp", shClear) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_zero.comp", shZero) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_bin.comp", shBin) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_scan.comp", shScan) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_csr.comp", shCsr) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_echo.comp", shEcho) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_echo_reduce.comp", shReduce) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_fft.comp", shFft) ||
        !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_product.comp", shProduct) ||
        (terrainCells_&&!loadStage(VK_SHADER_STAGE_COMPUTE_BIT,"radar_ground_plot.comp",shGround)) ||
        (nFast > 512 && (!loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_fft_tail.comp", shTail) ||
                         !loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "radar_transpose.comp", shTrans)))) {
        result.error = "compute shader 加载失败";
        return result;
    }
    auto compiler = vsg::ShaderCompiler::create();
    // 逐段编译定位失败 shader (glslang 运行时吞错, 整批编译失败无从分辨)
    struct CompDiag { const char* name; vsg::ref_ptr<vsg::ShaderStage> stage; };
    for (const CompDiag& d : {CompDiag{"radar_scatter.comp", shScat},
                              CompDiag{"radar_clear.comp", shClear},
                              CompDiag{"radar_zero.comp", shZero},
                              CompDiag{"radar_bin.comp", shBin},
                              CompDiag{"radar_scan.comp", shScan},
                              CompDiag{"radar_csr.comp", shCsr},
                              CompDiag{"radar_echo.comp", shEcho},
                              CompDiag{"radar_echo_reduce.comp", shReduce},
                              CompDiag{"radar_fft.comp", shFft},
                              CompDiag{"radar_product.comp", shProduct},
                              CompDiag{"radar_ground_plot.comp", shGround},
                              CompDiag{"radar_fft_tail.comp", shTail},
                              CompDiag{"radar_transpose.comp", shTrans}}) {
        if (!d.stage) continue;   // n_fast ≤ 512 时不加载 tail/trans
        vsg::ShaderStages one{d.stage};
        if (!compiler || !compiler->supported() || !compiler->compile(one)) {
            result.error = std::string("compute shader 编译失败 (glslang): ") + d.name;
            return result;
        }
    }

    const VkPushConstantRange clearPushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(vsg::uivec4)};
    const VkPushConstantRange fftPushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FftPush)};
    const VkPushConstantRange prodPushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ProdPush)};
    const VkPushConstantRange scatPushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(vsg::vec4)};   // 批次4: 载频
    auto scatLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{scatDsl},
                                                  vsg::PushConstantRanges{scatPushRange});
    auto clearLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{clearDsl},
                                                   vsg::PushConstantRanges{clearPushRange});
    auto compLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{compDsl},
                                                  vsg::PushConstantRanges{});
    auto fftLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{fftDsl},
                                                 vsg::PushConstantRanges{fftPushRange});
    auto prodLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{prodDsl},
                                                  vsg::PushConstantRanges{prodPushRange});
    // P2.1 分桶 pass 布局: zero/scan/csr 用 uivec4 push, bin 参数全走 UBO (无 push)
    auto zeroLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{zeroDsl},
                                                  vsg::PushConstantRanges{clearPushRange});
    auto binLayout  = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{binDsl},
                                                  vsg::PushConstantRanges{});
    auto scanLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{scanDsl},
                                                  vsg::PushConstantRanges{clearPushRange});
    auto csrLayout  = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{csrDsl},
                                                  vsg::PushConstantRanges{clearPushRange});
    auto reduceLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{reduceDsl},
                                                    vsg::PushConstantRanges{clearPushRange});
    auto cpScat = vsg::ComputePipeline::create(scatLayout, shScat);
    auto cpClear = vsg::ComputePipeline::create(clearLayout, shClear);
    auto cpZero = vsg::ComputePipeline::create(zeroLayout, shZero);
    auto cpBin = vsg::ComputePipeline::create(binLayout, shBin);
    auto cpScan = vsg::ComputePipeline::create(scanLayout, shScan);
    auto cpCsr = vsg::ComputePipeline::create(csrLayout, shCsr);
    auto cp = vsg::ComputePipeline::create(compLayout, shEcho);
    auto cpReduce = vsg::ComputePipeline::create(reduceLayout, shReduce);
    auto cpFft = vsg::ComputePipeline::create(fftLayout, shFft);
    auto cpProduct = vsg::ComputePipeline::create(prodLayout, shProduct);
    vsg::ref_ptr<vsg::ComputePipeline> cpTail, cpTrans;   // P2.2 (n_fast > 512)
    if (nFast > 512) {
        cpTail = vsg::ComputePipeline::create(tailLayout, shTail);
        cpTrans = vsg::ComputePipeline::create(transLayout, shTrans);
    }

    // ---- push 值 ----
    // 批次4: 散射提取 push — x=雷达载频 Hz (c/λ, 构建期定值; 介电模型频变消费)
    auto scatPush = vsg::Value<vsg::vec4>::create(
        vsg::vec4(float(2.99792458e8 / std::max(params_.lambda, 1e-9)), float(params_.transmit_power_w), 0.0f, 0.0f));
    // P1 段处理 push (构建期定值; 段参数随 params 固定)
    auto clearPush = vsg::Value<vsg::uivec4>::create(
        vsg::uivec4(2u * nCells, 2u * nCells * kEchoSeg, 0u, 0u));   // echo + partSum 清零个数
    auto fftPushA = vsg::Value<FftPush>::create(FftPush{
        vsg::uivec4(params_.n_fast, params_.cpi_pulses, params_.n_fast, 0u),   // echo→tmp 正变换
        vsg::vec4(1.0f, -1.0f, 0.0f, 0.0f)});
    auto fftPushB = vsg::Value<FftPush>::create(FftPush{
        vsg::uivec4(params_.n_fast, params_.cpi_pulses, params_.n_fast, 1u),   // tmp→range 逆变换×conj谱
        vsg::vec4(1.0f / float(params_.n_fast), 1.0f, 0.0f, 0.0f)});
    auto fftPushC = vsg::Value<FftPush>::create(FftPush{
        vsg::uivec4(params_.cpi_pulses, params_.n_fast, params_.n_fast, 0u),   // range→rd 慢时间列变换
        vsg::vec4(1.0f, -1.0f, 1.0f, 0.0f)});   // f.z=1: 转置寻址 (列模式 addr(e)=e*W+t)
    auto prodPush1 = vsg::Value<ProdPush>::create(ProdPush{
        vsg::uivec4(nCells, nGroupsProd, 0u, 0u), vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f)});
    auto prodPush2 = vsg::Value<ProdPush>::create(ProdPush{
        vsg::uivec4(nCells, nGroupsProd, 0u, 0u), vsg::vec4(1.0f, 0.0f, 0.0f, 0.0f)});

    // P2.1 分桶 pass push (zero: count+span 清零个数; scan: 桶数; csr: 散射体数;
    //                     reduce: nCells/脉冲数/nFast/段数)
    auto zeroPush = vsg::Value<vsg::uivec4>::create(vsg::uivec4(nFast, 1u, 0u, 0u));
    auto scanPush = vsg::Value<vsg::uivec4>::create(vsg::uivec4(nFast, 0u, 0u, 0u));
    auto csrPush = vsg::Value<vsg::uivec4>::create(vsg::uivec4(nScat, 0u, 0u, 0u));
    auto reducePush = vsg::Value<vsg::uivec4>::create(
        vsg::uivec4(nCells, params_.cpi_pulses, nFast, kEchoSeg));
    selectPulse_=[data=paramsData_,reducePush,count=params_.cpi_pulses](int pulse) {
        if(pulse < -1 || pulse>=int(count)) throw std::invalid_argument("radar pulse index outside CPI");
        (*data)[kMaxAnt*8+11]=float(pulse+1); data->dirty();
        reducePush->value().w=kEchoSeg | (uint32_t(pulse+1)<<16);
    };

    // P2.2 行变换分段 (n_fast = 512·F) push; scale 约定: R1_B 不缩放 (载入乘共轭谱),
    // 1/N 总尺度在 tailB 写出 (与 CPU 基准 RadarStages::fft 同约定)。
    vsg::ref_ptr<vsg::Data> fftPushR1A, tailPushA, fftPushR1B, tailPushB, fftPushC2v, transPush1, transPush2;
    if (nFast > 512) {
        const uint32_t F = nFast / 512;
        // 正变换: echo --R1_A(512 点子序列, 跨步 F)--> tmpA --tailA(twiddle+外 DFT)--> tmp
        fftPushR1A = vsg::Value<FftPush>::create(FftPush{
            vsg::uivec4(512u, params_.cpi_pulses * F, F, 0u),
            vsg::vec4(1.0f, -1.0f, 2.0f, float(nFast))});   // f.z=2 模式2, f.w=整行长
        tailPushA = vsg::Value<FftPush>::create(FftPush{
            vsg::uivec4(nFast, 512u, F, 0u),
            vsg::vec4(1.0f, -1.0f, 0.0f, 0.0f)});
        // 逆变换: tmp --R1_B(载入乘共轭谱, 512 点子序列)--> tmpB --tailB(×1/N)--> range
        fftPushR1B = vsg::Value<FftPush>::create(FftPush{
            vsg::uivec4(512u, params_.cpi_pulses * F, F, 1u),
            vsg::vec4(1.0f, 1.0f, 2.0f, float(nFast))});
        tailPushB = vsg::Value<FftPush>::create(FftPush{
            vsg::uivec4(nFast, 512u, F, 0u),
            vsg::vec4(1.0f / float(nFast), 1.0f, 0.0f, 0.0f)});
        // 列变换行模式: range --trans1--> tmp2 ([t][e]→[e][t]) --fftC2(行, N=cpi)--> rdT --trans2--> rd
        fftPushC2v = vsg::Value<FftPush>::create(FftPush{
            vsg::uivec4(params_.cpi_pulses, nFast, params_.cpi_pulses, 0u),
            vsg::vec4(1.0f, -1.0f, 0.0f, 0.0f)});
        transPush1 = vsg::Value<vsg::uivec4>::create(vsg::uivec4(nFast, params_.cpi_pulses, 0u, 0u));
        transPush2 = vsg::Value<vsg::uivec4>::create(vsg::uivec4(params_.cpi_pulses, nFast, 0u, 0u));
    }

    // ---- P1.3 按段 barrier (全局 memory barrier → 按 buffer 的 BufferMemoryBarrier) ----
    auto bufBarrier = [](vsg::Buffer* buf, VkDeviceSize size) {
        return vsg::BufferMemoryBarrier::create(
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            vsg::ref_ptr<vsg::Buffer>(buf), 0, size);
    };

    // ---- 常驻命令链 (独立 compute commandGraph, P1.2):
    //      清零 → 散射提取 → 回波合成 → 脉压 → 慢时间 FFT → product ----
    commandGraph = vsg::CommandGraph::create(device,
                                             device->getPhysicalDevice()->getQueueFamily(
                                                 VK_QUEUE_GRAPHICS_BIT));   // 同 family (gfx|compute); queue index 由上层 task 选

    // [P2] 链首时间戳复位 (每帧时间戳独立)
    if (tsValid_) {
        auto rst = ResetTimestamps::create();
        rst->pool = tsPool_;
        commandGraph->addChild(rst);
    }

    // [P1] 链首清零 (echo + partSum; 与回读解耦)
    const uint32_t clearElems = 2u * nCells * kEchoSeg;
    const uint32_t clearGroups = (clearElems + 255) / 256;
    commandGraph->addChild(vsg::BindComputePipeline::create(cpClear));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, clearLayout, 0, vsg::DescriptorSets{clearSet}));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, clearPush));
    commandGraph->addChild(vsg::Dispatch::create(clearGroups, 1, 1));
    // 清零(echo 写) → 散射/回波 有序 (P1.3: 按 echo buffer)
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(echoBuf_.get(), cpxBytes)));

    const auto clearEnd=commandGraph->children.size();
    // [P1.1] 散射体提取 (radar_scatter.comp, 读 G-buffer → 散射体缓冲)
    const uint32_t nScatGroups = (nScat + 255) / 256;
    commandGraph->addChild(vsg::BindComputePipeline::create(cpScat));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, scatLayout, 0, vsg::DescriptorSets{scatSet}));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, scatPush));   // 批次4: 载频
    commandGraph->addChild(vsg::Dispatch::create(nScatGroups, 1, 1));
    // 散射体写 → 回波读 (P1.3: 按 scatterPos/Sig buffer)
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(scatterPosInfo_->buffer.get(), VkDeviceSize(nScat) * sizeof(vsg::vec4)),
        bufBarrier(scatterSigInfo_->buffer.get(), VkDeviceSize(nScat) * sizeof(vsg::vec4))));

    if(terrainCells_){
        auto dsl=vsg::DescriptorSetLayout::create();
        for(uint32_t binding=0;binding<3;++binding)
            dsl->addBinding(binding,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT);
        dsl->addBinding(3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT);
        auto set=vsg::DescriptorSet::create(dsl,vsg::Descriptors{
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterPosInfo_},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{scatterSigInfo_},1,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(vsg::BufferInfoList{groundInfo},2,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            vsg::DescriptorBuffer::create(paramsData_,3,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)});
        auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{clearPushRange});
        auto push=vsg::Value<vsg::uivec4>::create(vsg::uivec4(w,h,groundTileWidth,groundTileHeight));
        commandGraph->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,shGround)));
        commandGraph->addChild(vsg::BindDescriptorSets::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,vsg::DescriptorSets{set}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,push));
        commandGraph->addChild(vsg::Dispatch::create((terrainCells_+63)/64,1,1));
    }

    // ---- [P2.1] 分桶 CSR 四 pass: zero → bin → scan → csr ----
    // bin 逐散射体精确 bmin/bmax + 桶计数 + maxSpan 汇聚 (数据驱动, 无速度假设);
    // scan 单工作组排他前缀和; csr 桶序散布+载荷重排 → echo 合成 pass 顺序消费。
    const uint32_t scatGroups = (nScat + 255) / 256;
    const uint32_t fastGroups = (nFast + 255) / 256;
    if (tsValid_) {   // 槽0: 分桶链起点 (bin+scan+csr 耗时 = 槽1−槽0)
        auto ts0 = WriteTimestamp::create();
        ts0->pool = tsPool_;
        ts0->slot = 0;
        commandGraph->addChild(ts0);
    }
    commandGraph->addChild(vsg::BindComputePipeline::create(cpZero));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, zeroLayout, 0, vsg::DescriptorSets{zeroSet}));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, zeroPush));
    commandGraph->addChild(vsg::Dispatch::create(fastGroups, 1, 1));
    commandGraph->addChild(vsg::BindComputePipeline::create(cpBin));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, binLayout, 0, vsg::DescriptorSets{binSet}));
    commandGraph->addChild(vsg::Dispatch::create(scatGroups, 1, 1));
    // bin 写 (count/meta/span) → scan 读 count (P1.3 按缓冲 barrier)
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(countBuf_.get(), VkDeviceSize(nFast) * 4),
        bufBarrier(metaBuf_.get(), VkDeviceSize(nScat) * 4),
        bufBarrier(spanBuf_.get(), 4)));
    commandGraph->addChild(vsg::BindComputePipeline::create(cpScan));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, scanLayout, 0, vsg::DescriptorSets{scanSet}));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, scanPush));
    commandGraph->addChild(vsg::Dispatch::create(1, 1, 1));   // 单工作组 1024 线程 ×4 元素
    // scan 写 (offs/cursor) → csr 原子散布
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(offsBuf_.get(), VkDeviceSize(nFast + 1) * 4),
        bufBarrier(cursorBuf_.get(), VkDeviceSize(nFast) * 4)));
    commandGraph->addChild(vsg::BindComputePipeline::create(cpCsr));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, csrLayout, 0, vsg::DescriptorSets{csrSet}));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, csrPush));
    commandGraph->addChild(vsg::Dispatch::create(scatGroups, 1, 1));
    // csr 写 (idx + 桶序载荷重排) → echo 顺序读
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(idxBuf_.get(), VkDeviceSize(nScat) * 4),
        bufBarrier(posSortBuf_.get(), scatBytes),
        bufBarrier(sigSortBuf_.get(), scatBytes),
        bufBarrier(metaSortBuf_.get(), VkDeviceSize(nScat) * 4)));

    // [B6/P2.1] 回波合成 (二维工作组 = 128-bin 块 × 单脉冲; 无 CPI enable=0 早退);
    // [P2] 时间戳打点 (槽1=echo前, 槽2=echo后; 槽0 在分桶链前)
    const uint32_t echoGroupsX = (nFast + 127) / 128;
    commandGraph->addChild(vsg::BindComputePipeline::create(cp));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, compLayout, 0, vsg::DescriptorSets{compSet}));
    if (tsValid_) {   // 槽1: csr barrier 之后 (分桶四 pass 耗时 = 槽1−槽0)
        auto tsEchoStart = WriteTimestamp::create();
        tsEchoStart->pool = tsPool_;
        tsEchoStart->slot = 1;
        commandGraph->addChild(tsEchoStart);
    }
    commandGraph->addChild(vsg::Dispatch::create(echoGroupsX, params_.cpi_pulses, kEchoSeg));
    if (tsValid_) {
        auto ts1 = WriteTimestamp::create();
        ts1->pool = tsPool_;
        ts1->slot = 2;
        commandGraph->addChild(ts1);
    }
    // 段部分和写 → reduce 读 (P1.3: 按 partSum buffer)
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(partSumBuf_.get(), cpxBytes * kEchoSeg)));
    // [P2.1] 段归约: 每 (脉冲, bin) 一线程无原子汇总 S 段 → echo
    const uint32_t reduceGroups = (nCells + 255) / 256;
    commandGraph->addChild(vsg::BindComputePipeline::create(cpReduce));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, reduceLayout, 0, vsg::DescriptorSets{reduceSet}));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, reducePush));
    commandGraph->addChild(vsg::Dispatch::create(reduceGroups, 1, 1));
    // echo 写 → 脉压读 (P1.3: 按 echo buffer)
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(echoBuf_.get(), cpxBytes)));

    const auto pulseEnd=commandGraph->children.size();
    if (nFast <= 512) {
        // ---- [P1] 原单段 FFT 路径 (n_fast ≤ 512, 零回归) ----
        // 脉压: 正变换 (echo→tmp) → 逆变换×共轭谱 (tmp→range, 1/N)
        commandGraph->addChild(vsg::BindComputePipeline::create(cpFft));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, fftLayout, 0, vsg::DescriptorSets{fftSetA}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, fftPushA));
        commandGraph->addChild(vsg::Dispatch::create(params_.cpi_pulses, 1, 1));
        // tmp 写 → pass B 读 (P1.3: 按 tmp buffer)
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(tmpBuf_.get(), cpxBytes)));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, fftLayout, 0, vsg::DescriptorSets{fftSetB}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, fftPushB));
        commandGraph->addChild(vsg::Dispatch::create(params_.cpi_pulses, 1, 1));
        // range 写 → 慢时间读 (P1.3: 按 range buffer)
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(rangeBuf_.get(), cpxBytes)));

        // 慢时间 FFT (range→rd, 列变换转置寻址)
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, fftLayout, 0, vsg::DescriptorSets{fftSetC}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, fftPushC));
        commandGraph->addChild(vsg::Dispatch::create(params_.n_fast, 1, 1));
    } else {
        // ---- [P2.2] 行变换分段扩容路径 (n_fast = 512·F): ----
        // R1_A(512 点跨步 F 子序列) → tailA(twiddle+外层 F 点 DFT) →
        // R1_B(载入乘共轭谱) → tailB(×1/N) → trans1 → fftC2(列变换行模式) → trans2
        commandGraph->addChild(vsg::BindComputePipeline::create(cpFft));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, fftLayout, 0, vsg::DescriptorSets{fftSetR1A}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, fftPushR1A));
        commandGraph->addChild(vsg::Dispatch::create(params_.cpi_pulses * (nFast / 512), 1, 1));
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(tmpABuf_.get(), cpxBytes)));
        commandGraph->addChild(vsg::BindComputePipeline::create(cpTail));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, tailLayout, 0, vsg::DescriptorSets{tailSetA}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, tailPushA));
        commandGraph->addChild(vsg::Dispatch::create((params_.cpi_pulses * 512 + 255) / 256, 1, 1));
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(tmpBuf_.get(), cpxBytes)));
        commandGraph->addChild(vsg::BindComputePipeline::create(cpFft));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, fftLayout, 0, vsg::DescriptorSets{fftSetR1B}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, fftPushR1B));
        commandGraph->addChild(vsg::Dispatch::create(params_.cpi_pulses * (nFast / 512), 1, 1));
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(tmpBBuf_.get(), cpxBytes)));
        commandGraph->addChild(vsg::BindComputePipeline::create(cpTail));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, tailLayout, 0, vsg::DescriptorSets{tailSetB}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, tailPushB));
        commandGraph->addChild(vsg::Dispatch::create((params_.cpi_pulses * 512 + 255) / 256, 1, 1));
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(rangeBuf_.get(), cpxBytes)));
        // 列变换: 全局转置 (coalesced) → 行模式 N=cpi → 转置还原
        commandGraph->addChild(vsg::BindComputePipeline::create(cpTrans));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, transLayout, 0, vsg::DescriptorSets{transSet1}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, transPush1));
        commandGraph->addChild(vsg::Dispatch::create((nFast + 31) / 32, (params_.cpi_pulses + 31) / 32, 1));
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(tmp2Buf_.get(), cpxBytes)));
        commandGraph->addChild(vsg::BindComputePipeline::create(cpFft));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, fftLayout, 0, vsg::DescriptorSets{fftSetC2}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, fftPushC2v));
        commandGraph->addChild(vsg::Dispatch::create(nFast, 1, 1));
        commandGraph->addChild(vsg::PipelineBarrier::create(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            bufBarrier(rdTBuf_.get(), cpxBytes)));
        commandGraph->addChild(vsg::BindComputePipeline::create(cpTrans));
        commandGraph->addChild(vsg::BindDescriptorSets::create(
            VK_PIPELINE_BIND_POINT_COMPUTE, transLayout, 0, vsg::DescriptorSets{transSet2}));
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, transPush2));
        commandGraph->addChild(vsg::Dispatch::create((params_.cpi_pulses + 31) / 32, (nFast + 31) / 32, 1));
    }
    // rd 写 → product 读 (P1.3: 按 rd buffer)
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(rdBuf_.get(), cpxBytes)));

    // [P1] product: 取模+组峰值 → 全局归一化
    commandGraph->addChild(vsg::BindComputePipeline::create(cpProduct));
    commandGraph->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, prodLayout, 0, vsg::DescriptorSets{prodSet}));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, prodPush1));
    commandGraph->addChild(vsg::Dispatch::create(nGroupsProd, 1, 1));
    // prod/gmax 写 → pass2 读 (P1.3: 按 prod/gmax buffer)
    commandGraph->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        bufBarrier(prodBuf_.get(), prodBytes),
        bufBarrier(gmaxBuf_.get(), VkDeviceSize(nGroupsProd) * sizeof(float))));
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, prodPush2));
    commandGraph->addChild(vsg::Dispatch::create(nGroupsProd, 1, 1));

    clearCommands=vsg::Commands::create(); pulseCommands=vsg::Commands::create(); processCommands=vsg::Commands::create();
    if(tsValid_) {
        auto reset=ResetTimestamps::create(); reset->pool=tsPool_;
        pulseCommands->addChild(reset); // timestamps are per pulse, not per CPI
    }
    for(std::size_t i=0;i<commandGraph->children.size();++i)
        (i<clearEnd ? clearCommands : i<pulseEnd ? pulseCommands : processCommands)->addChild(commandGraph->children[i].cast<vsg::Command>());
    gpuChainReady_ = true;

    std::printf("[RadarChain] 回波链就绪 (P2 分桶归约): 散射体栅格 %ux%u (%u 槽, G-buffer "
                "消费), CPI=%u×PRF=%.0fHz, λ=%.4fm 距离门 %.0f-%.0fm (n_fast=%u @%.1fMHz, "
                "分辨率 %.1fm), echo %u×%u 组, 独立 compute commandGraph (P1.2)\n",
                w, h, nScat, params_.cpi_pulses, params_.prf, params_.lambda,
                params_.range_min_m, params_.range_max_m, params_.n_fast,
                params_.bandwidth_hz * 1e-6,
                2.99792458e8 / (2.0 * params_.bandwidth_hz), echoGroupsX, params_.cpi_pulses);
    std::printf("[RadarChain]   回波链: 清零→散射→[zero→bin→scan→csr]→回波(分桶归约, 共享内存累加)→"
                "段处理, P1.3 按 buffer barrier, 中间缓冲 %.1fKB, 参考谱 %u 点, gExp=%.0f%s\n",
                double(cpxBytes) / 1024.0, params_.n_fast, gExp,
                nFast > 512 ? " [P2.2 FFT 分段扩容]" : "");
    if (materials) {
        std::printf("[RadarChain]   σ 材质 %u 个 (G-buffer 池材质表, radar_x_params 菲涅尔)\n",
                    uint32_t(materials->size()));
    }

    result.ok = true;
    return result;
}

// ===========================================================================
// P2 验收: 时间戳生命周期 + echo kernel 耗时读取
// ===========================================================================
RadarChain::~RadarChain() {
    if (tsPool_ && tsDevice_) vkDestroyQueryPool(tsDevice_->vk(), tsPool_, nullptr);
}
void RadarChain::selectPulse(int pulse) {
    if(!selectPulse_) throw std::logic_error("build radar chain before selecting a pulse");
    selectPulse_(pulse);
}

double RadarChain::echoKernelMs(vsg::Device* device, double* bucketMs) {
    if (!tsValid_ || !tsPool_ || !device) return -1.0;
    uint64_t ts[4] = {};
    const VkResult r = vkGetQueryPoolResults(device->vk(), tsPool_, 0, 3,
                                             sizeof(ts), ts, sizeof(uint64_t),
                                             VK_QUERY_RESULT_64_BIT);
    if (r != VK_SUCCESS || !ts[0] || !ts[1] || !ts[2]) return -1.0;   // 未就绪 (须 device 空闲后)
    if (bucketMs) *bucketMs = double(ts[1] - ts[0]) * tsPeriodNs_ * 1e-6;   // 分桶四 pass
    return double(ts[2] - ts[1]) * tsPeriodNs_ * 1e-6;                      // 纯 echo 合成
}

// ===========================================================================
// 每帧更新
// ===========================================================================
void RadarChain::setPulses(const std::vector<vsg::dvec3>& antPos, bool enable,
                           const std::vector<vsg::dvec3>& beamDirs, float beam_width_rad, uint32_t receiver_seed) {
    if (!paramsData_) return;
    float* f = paramsData_->data();
    const uint32_t n = std::min<uint32_t>(uint32_t(antPos.size()), kMaxAnt);
    for (uint32_t i = 0; i < n; ++i) {
        f[i * 4 + 0] = float(antPos[i].x);
        f[i * 4 + 1] = float(antPos[i].y);
        f[i * 4 + 2] = float(antPos[i].z);
        f[i * 4 + 3] = float(params_.range_max_m);
    }
    // P5: 逐脉冲波束指向 (空 → hasBeam=0, 增益 1 旧行为)
    const uint32_t nb = std::min<uint32_t>(uint32_t(beamDirs.size()), kMaxAnt);
    for (uint32_t i = 0; i < nb; ++i) {
        const vsg::dvec3 d = vsg::normalize(beamDirs[i]);
        float* b = f + kMaxAnt * 4 + i * 4;
        b[0] = float(d.x); b[1] = float(d.y); b[2] = float(d.z); b[3] = 0.0f;
    }
    f[kMaxAnt * 8 + 6] = enable ? 1.0f : 0.0f;   // m1.z enable
    f[kMaxAnt * 8 + 8] = beam_width_rad > 0.0f ? beam_width_rad : float(params_.beam_width_rad);
    f[kMaxAnt * 8 + 10] = nb > 0 ? 1.0f : 0.0f;  // m2.z hasBeam
    f[kMaxAnt * 8 + 13] = float(receiver_seed & 0xffffffu);
    paramsData_->dirty();                        // DYNAMIC_DATA: 触发上传
}

// ===========================================================================
// 验收/显式回读 (一次性 CB; 清零已挪到常驻链首, 此处只拷不改)
// ===========================================================================
bool RadarChain::readbackEcho(vsg::Window& window, std::vector<std::complex<float>>& out) { return readbackEcho(*window.getOrCreateDevice(), out); }

bool RadarChain::readbackEcho(vsg::Device& deviceArg, std::vector<std::complex<float>>& out) {
    if (!echoBuf_) return false;
    const VkDeviceSize bytes = echoBuf_->size;
    const size_t nFloat = size_t(bytes) / sizeof(float);
    out.resize(nFloat / 2);
    if (!readbackDeviceBuffer(deviceArg, echoBuf_, bytes, out.data())) return false;
    return true;
}

bool RadarChain::readbackRd(vsg::Window& window, std::vector<std::complex<float>>& out) { return readbackRd(*window.getOrCreateDevice(), out); }

bool RadarChain::readbackRd(vsg::Device& deviceArg, std::vector<std::complex<float>>& out) {
    if (!rdBuf_ || !gpuChainReady_) return false;
    const VkDeviceSize bytes = rdBuf_->size;
    const size_t nFloat = size_t(bytes) / sizeof(float);
    out.resize(nFloat / 2);
    return readbackDeviceBuffer(deviceArg, rdBuf_, bytes, out.data());
}

bool RadarChain::readbackRange(vsg::Window& window, std::vector<std::complex<float>>& out) { return readbackRange(*window.getOrCreateDevice(), out); }

bool RadarChain::readbackRange(vsg::Device& deviceArg, std::vector<std::complex<float>>& out) {
    if (!rangeBuf_ || !gpuChainReady_) return false;
    const VkDeviceSize bytes = VkDeviceSize(2) * params_.cpi_pulses * params_.n_fast * sizeof(float);
    const size_t nFloat = size_t(bytes) / sizeof(float);
    out.resize(nFloat / 2);
    return readbackDeviceBuffer(deviceArg, rangeBuf_, bytes, out.data());
}

bool RadarChain::readbackProduct(vsg::Window& window, std::vector<float>& out) { return readbackProduct(*window.getOrCreateDevice(), out); }

bool RadarChain::readbackProduct(vsg::Device& deviceArg, std::vector<float>& out) {
    if (!prodBuf_ || !gpuChainReady_) return false;
    const VkDeviceSize bytes = prodBuf_->size;
    out.resize(size_t(bytes) / sizeof(float));
    return readbackDeviceBuffer(deviceArg, prodBuf_, bytes, out.data());
}

// ===========================================================================
// 散射体缓冲读回统计 (B6 调试/验收辅助)
// ===========================================================================
RadarChain::ScatterStats RadarChain::scatterStats(vsg::Window& window) { return scatterStats(*window.getOrCreateDevice()); }

RadarChain::ScatterStats RadarChain::scatterStats(vsg::Device& deviceArg) {
    ScatterStats st;
    st.total = scatterCount();
    if (!scatterPosInfo_ || !scatterSigInfo_) return st;
    auto posBuf = scatterPosInfo_->buffer;
    auto sigBuf = scatterSigInfo_->buffer;
    if (!posBuf || !sigBuf) return st;   // compile 前 (不应发生)

    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);
    const int gfxFamily = device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    auto queue = device->getQueue(uint32_t(gfxFamily));
    const VkDeviceSize posBytes = VkDeviceSize(st.total) * sizeof(vsg::vec4);
    const VkDeviceSize sigBytes = posBytes;

    if (!scatterHostReady_) {
        scatterHost_ = vsg::Buffer::create(posBytes + sigBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           VK_SHARING_MODE_EXCLUSIVE);
        scatterHost_->compile(device.get());
        scatterHostMem_ = vsg::DeviceMemory::create(
            device.get(), scatterHost_->getMemoryRequirements(device->deviceID),
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        scatterHost_->bind(scatterHostMem_, 0);
        scatterHostReady_ = true;
    }

    auto pool = vsg::CommandPool::create(device, uint32_t(gfxFamily));
    auto cb = pool->allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    // [B6] 帧提交 (compute) 与读回 CB 间存在跨提交竞争 (两次运行结果漂移实证),
    // 设备空闲等待换确定性 —— 与验收回读同模式。
    if(queue->waitIdle()!=VK_SUCCESS) return st;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb->vk(), &bi);
    // compute 写 → 传输读
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb->vk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    VkBufferCopy r0{0, 0, posBytes};
    vkCmdCopyBuffer(cb->vk(), posBuf->vk(device->deviceID), scatterHost_->vk(device->deviceID), 1, &r0);
    VkBufferCopy r1{0, posBytes, sigBytes};
    vkCmdCopyBuffer(cb->vk(), sigBuf->vk(device->deviceID), scatterHost_->vk(device->deviceID), 1, &r1);
    vkEndCommandBuffer(cb->vk());
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    VkCommandBuffer vkCb = cb->vk();
    si.pCommandBuffers = &vkCb;
    auto fence = vsg::Fence::create(device);
    if (queue->submit(si, fence.get()) != VK_SUCCESS || fence->wait(UINT64_MAX) != VK_SUCCESS) {
        std::fprintf(stderr, "[RadarChain] 散射体读回提交失败\n");
        return st;
    }

    void* mapped = nullptr;
    if (scatterHostMem_->map(0, posBytes + sigBytes, 0, &mapped) != VK_SUCCESS || !mapped) {
        std::fprintf(stderr, "[RadarChain] 散射体 map 失败\n");
        return st;
    }
    const vsg::vec4* pos = static_cast<const vsg::vec4*>(mapped);
    const vsg::vec4* sig = reinterpret_cast<const vsg::vec4*>(
        static_cast<const uint8_t*>(mapped) + posBytes);
    double sigSum = 0.0;
    for (uint32_t i = 0; i < st.total; ++i) {
        if (pos[i].w < 0.5f) continue;   // 本 CPI 未命中 (天空)
        ++st.hits;
        if (st.hits == 1) st.pos_first = vsg::dvec3(pos[i].x, pos[i].y, pos[i].z);
        const float amp = std::sqrt(sig[i].x * sig[i].x + sig[i].y * sig[i].y);
        sigSum += amp;
        st.sigma_max = std::max(st.sigma_max, amp);
    }
    if (st.hits > 0) st.sigma_mean = float(sigSum / st.hits);
    scatterHostMem_->unmap();
    return st;
}

} // namespace sensor
} // namespace rockyenv
