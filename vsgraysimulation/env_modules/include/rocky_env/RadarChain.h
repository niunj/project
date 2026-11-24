#pragma once
/// @file RadarChain.h —— B6/P1.1: 雷达回波链 GPU 前端 (纯 compute 链)。
///
/// P1.1 改造: 散射体提取由 radar_scatter RT 三段 (rgen/rchit/rmiss) 改为
/// radar_scatter.comp 读 G-buffer (P1.1 GBufferPool 全场景唯一 TraceRays 的
/// 雷达视角产物) → 本链整链纯 compute (清零→散射→回波→段处理), 无 RT 管线。
///
/// GPU 职责 (每帧一次, 独立 commandGraph, P1.2 挂 async compute queue):
///   0. 回波缓冲清零 (radar_clear compute, 链首 —— 与回读解耦)。
///   1. 散射体提取 (radar_scatter.comp): G-buffer 每像素 → 散射体缓冲
///      (位置 ENU + 复散射系数 σ, 材质 radar_x_params 菲涅尔模型)。
///   2. 回波合成 (radar_echo compute): 逐脉冲相位历史原子浮点累加 →
///      E(n, fast)。天线脉冲位姿 + P5 逐脉冲波束指向由 CPU 下发 (setPulses)。
///   3. 段处理 (P1, 同一命令链): 脉压 FFT×2 (radar_fft, 频域匹配滤波) →
///      慢时间 FFT → RD 图 → 归一化幅度图 (radar_product) —— 全程显存,
///      零 CPU 回读; CPU 段 (RadarStages) 保留为验收交叉基准。
/// P1.3 同步细分: 段间 barrier 由全局 memory barrier 改为按 buffer 的
/// VkBufferMemoryBarrier (echo/tmp/range/rd/product 各自), 允许驱动重叠
/// 无关资源访问。
///
/// 执行: commandGraph 为独立 compute command graph (P1.2 由上层挂入独立
/// RecordAndSubmitTask + 队列间 semaphore 依赖); 无待处理 CPI 时 echo compute
/// 早退 (params.m1.z enable 门控), FFT/product 空转代价可忽略。

#include <rocky_env/SceneSource.h>    // RadarMaterial

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>

#include <complex>
#include <string>
#include <vector>

namespace vsg {
class Window;
class Node;
class Buffer;
class CommandGraph;
class Commands;
class Group;
class Device;
}
// Vulkan 前向声明 (避免头文件引入 vulkan.h): VkQueryPool 即 VkQueryPool_T*
struct VkQueryPool_T;

namespace rockyenv {
namespace sensor {

class SharedSceneResources;   // P0 场景级资源池 (上层创建, 引用传递)
class GBufferPool;            // P1.1 全场景唯一求交 (上层创建, 引用传递)

/// 雷达链参数 (SensorDesc 同名映射 + CLI 覆盖; Param > 场景 JSON > 默认)。
struct RadarChainParams {
    uint32_t width = 640, height = 512;   ///< 散射体栅格 (G-buffer 像素一一对应)
    double lambda = 0.03;                 ///< 波长 (m)
    double prf = 4000.0;                  ///< PRF (Hz)
    uint32_t cpi_pulses = 128;            ///< CPI 脉冲数 (≤256, 2 的幂)
    double pulse_width_s = 1e-6;          ///< 脉宽 (s)
    double bandwidth_hz = 30e6;           ///< 带宽 (Hz)
    double range_min_m = 0.0;             ///< 距离门起始 (m)
    double range_max_m = 8000.0;          ///< 距离门结束 (m)
    double fov_h_deg = 60.0;              ///< 水平视场 (°, 散射体栅格张角)
    double fov_v_deg = 30.0;              ///< 垂直视场 (°)
    uint32_t n_fast = 512;                ///< 快时间采样数 (2 的幂, FFT)
    double beam_width_rad = 0.05;         ///< P5 波束宽度 (rad, 方向图 gExp 标定)
    double sample_rate_hz = 0.0;          ///< 0 = bandwidth_hz; independent of FFT padding
    double transmit_power_w = 1.0;       ///< power scaling; receiver calibration remains separate
    double receiver_noise_iq_rms = 0.0;  ///< RMS magnitude of the complex receiver noise, in relative signal units
    bool terrain_footprint = false;       ///< Optional sparse ground-surface response from the CPI center pulse
};

/// 雷达回波链 GPU 前端 (P1.1 纯 compute: G-buffer 散射 + 回波合成 + 段处理)。
class RadarChain {
public:
    struct Result {
        bool ok = false;
        std::string error;
    };

    /// 建 compute 管线/描述符/缓冲 + 独立 commandGraph (viewer->compile())。
    ///   gbuffer    P1.1 G-buffer 池 (必需; 分辨率校验 + σ 材质表来源)
    ///   shared     P0 资源池 (可空; 当前仅保留接口一致性 —— 几何/TLAS 已
    ///              收敛至 GBufferPool, 本链无几何依赖)
    Result build(vsg::Window& window,
                 GBufferPool* gbuffer,
                 const RadarChainParams& params,
                 const std::vector<RadarMaterial>* materials,
                 SharedSceneResources* shared = nullptr);
    Result build(vsg::Device& device,
                 GBufferPool* gbuffer,
                 const RadarChainParams& params,
                 const std::vector<RadarMaterial>* materials,
                 SharedSceneResources* shared = nullptr);

    ~RadarChain();   // 释放 GPU 时间戳查询池 (P2 验收)

    /// echo kernel GPU 耗时 (ms, 时间戳查询; 不可用/未就绪返回 -1)。
    /// bucketMs 非空时写入分桶四 pass (zero→bin→scan→csr) 耗时。
    /// 须在帧提交后 (device 空闲, 如 metrics 回读路径) 调用。
    double echoKernelMs(vsg::Device* device, double* bucketMs = nullptr);

    /// CPI 脉冲天线位姿 + P5 逐脉冲波束指向下发; enable=false 时 compute 早退
    /// (本帧无 CPI)。cpi_pulses ≤ 256; beamDirs 为空 → 波束增益 1 (旧行为)。
    void setPulses(const std::vector<vsg::dvec3>& antPos, bool enable,
                   const std::vector<vsg::dvec3>& beamDirs = {},
                   float beam_width_rad = 0.0f, uint32_t receiver_seed = 0);
    /// Select one pulse for dynamic-scene execution. -1 restores legacy whole-CPI mode.
    void selectPulse(int pulse);
    vsg::ref_ptr<vsg::Commands> clearCommands,pulseCommands,processCommands;

    // ---- P1 GPU 常驻引用 (显存驻留; 显控 GPU 句柄发布的数据源) ----
    vsg::ref_ptr<vsg::Buffer> gpuEcho() const { return echoBuf_; }      ///< 原始 IQ 回波
    vsg::ref_ptr<vsg::Buffer> gpuTerrainFootprint() const { return terrainBuf_; }
    uint32_t terrainFootprintSize() const { return terrainCells_; }
    vsg::ref_ptr<vsg::Buffer> gpuRdMap() const { return rdBuf_; }       ///< RD 复图
    vsg::ref_ptr<vsg::Buffer> gpuProduct() const { return prodBuf_; }   ///< 归一化幅度图
    /// P1 段处理 GPU 链是否就绪 (build 成功后恒 true)。
    bool gpuChainReady() const { return gpuChainReady_; }

    // ---- 验收/显式回读 (仅 dump/--metrics/--radar-cpu-stages 路径) ----
    /// 回波复数读回 (不再清零 —— 清零已挪到常驻链首)。out.size() = cpi×nFast。
    bool readbackEcho(vsg::Window& window, std::vector<std::complex<float>>& out);
    bool readbackEcho(vsg::Device& device, std::vector<std::complex<float>>& out);
    /// RD 复图读回 (P1 GPU 产物; rdMetrics 交叉验证)。
    bool readbackRd(vsg::Window& window, std::vector<std::complex<float>>& out);
    bool readbackRd(vsg::Device& device, std::vector<std::complex<float>>& out);
    /// P2.2 调试: 脉压输出 (tailB 写出的 range 缓冲) 读回, 段级验收/错误层级定位。
    bool readbackRange(vsg::Window& window, std::vector<std::complex<float>>& out);
    bool readbackRange(vsg::Device& device, std::vector<std::complex<float>>& out);
    /// 归一化幅度图读回 (--dump-rd PPM)。
    bool readbackProduct(vsg::Window& window, std::vector<float>& out);
    bool readbackProduct(vsg::Device& device, std::vector<float>& out);

    /// 散射体缓冲读回统计 (B6 调试/验收辅助: 命中率 + σ 分布)。
    struct ScatterStats {
        uint32_t total = 0, hits = 0;
        float sigma_max = 0.0f, sigma_mean = 0.0f;
        vsg::dvec3 pos_first{0.0, 0.0, 0.0};   ///< 首个命中散射体位置 (ENU)
    };
    ScatterStats scatterStats(vsg::Window& window);
    ScatterStats scatterStats(vsg::Device& device);

    vsg::ref_ptr<vsg::CommandGraph> commandGraph;  ///< P1.2: 独立 compute command graph
    const RadarChainParams& params() const { return params_; }
    uint32_t scatterCount() const { return params_.width * params_.height; }

private:
    std::function<void(int)> selectPulse_;
    RadarChainParams params_{};
    SharedSceneResources* shared_ = nullptr;            ///< P0 池 (非拥有; 上层保证生命周期)
    vsg::ref_ptr<vsg::floatArray> paramsData_;          // compute UBO (ant+beam+m0..m2)
    vsg::ref_ptr<vsg::Buffer> scatterPosBuf_;           // 散射体 SSBO (自管 device local)
    vsg::ref_ptr<vsg::DeviceMemory> scatterPosMem_;
    vsg::ref_ptr<vsg::Buffer> scatterSigBuf_;
    vsg::ref_ptr<vsg::DeviceMemory> scatterSigMem_;
    vsg::ref_ptr<vsg::BufferInfo> scatterPosInfo_;      // 散射体 SSBO (compute 写/读)
    vsg::ref_ptr<vsg::BufferInfo> scatterSigInfo_;
    vsg::ref_ptr<vsg::Buffer> echoBuf_;                 // 回波 SSBO (device local, 自管)
    vsg::ref_ptr<vsg::DeviceMemory> echoMem_;
    vsg::ref_ptr<vsg::Buffer> echoHost_;                // 回读接收 (host visible, 惰性)
    vsg::ref_ptr<vsg::DeviceMemory> echoHostMem_;
    bool hostReady_ = false;
    vsg::ref_ptr<vsg::Buffer> scatterHost_;             // 散射体读回接收 (host visible, 惰性)
    vsg::ref_ptr<vsg::DeviceMemory> scatterHostMem_;
    bool scatterHostReady_ = false;

    // ---- P1: 段处理 GPU 资源 (中间缓冲 device local + 脉压参考谱 host 上传) ----
    vsg::ref_ptr<vsg::Buffer> tmpBuf_;          // FFT pass A 输出 (脉压正变换)
    vsg::ref_ptr<vsg::Buffer> rangeBuf_;        // 脉压输出 (RangeComp 对应)
    vsg::ref_ptr<vsg::Buffer> rdBuf_;           // RD 复图 (RdMap 对应)
    vsg::ref_ptr<vsg::Buffer> prodBuf_;         // 归一化幅度图 (Product 对应)
    vsg::ref_ptr<vsg::Buffer> gmaxBuf_;         // product pass1 组峰值
    vsg::ref_ptr<vsg::DeviceMemory> tmpMem_, rangeMem_, rdMem_, prodMem_, gmaxMem_;
    vsg::ref_ptr<vsg::Buffer> refSpecBuf_;      // 脉压共轭参考谱 (host 上传一次)
    vsg::ref_ptr<vsg::DeviceMemory> refSpecMem_;
    bool gpuChainReady_ = false;

    // ---- P2.1: 回波分桶 CSR 资源 (device local) ----
    vsg::ref_ptr<vsg::Buffer> countBuf_;        // 桶计数 (nFast)
    vsg::ref_ptr<vsg::DeviceMemory> countMem_;
    vsg::ref_ptr<vsg::Buffer> cursorBuf_;       // 散布工作游标 (nFast)
    vsg::ref_ptr<vsg::DeviceMemory> cursorMem_;
    vsg::ref_ptr<vsg::Buffer> offsBuf_;         // 排他前缀和 (nFast+1, 尾=总数)
    vsg::ref_ptr<vsg::DeviceMemory> offsMem_;
    vsg::ref_ptr<vsg::Buffer> spanBuf_;         // CPI 内 bin 漂移全局上界 (1)
    vsg::ref_ptr<vsg::DeviceMemory> spanMem_;
    vsg::ref_ptr<vsg::Buffer> metaBuf_;         // 逐散射体 bmin|bmax<<12 (nScat)
    vsg::ref_ptr<vsg::DeviceMemory> metaMem_;
    vsg::ref_ptr<vsg::Buffer> idxBuf_;          // CSR 散射体索引 (nScat)
    vsg::ref_ptr<vsg::DeviceMemory> idxMem_;
    // 桶序载荷重排 (合成 pass coalesced 读; 散布 pass 写)
    vsg::ref_ptr<vsg::Buffer> posSortBuf_, sigSortBuf_, metaSortBuf_;
    vsg::ref_ptr<vsg::DeviceMemory> posSortMem_, sigSortMem_, metaSortMem_;
    // 段部分和 (S×nPulses×nFast 复数; 合成 pass 单写者槽 → reduce pass 无原子汇总)
    vsg::ref_ptr<vsg::Buffer> partSumBuf_;
    vsg::ref_ptr<vsg::DeviceMemory> partSumMem_;
    vsg::ref_ptr<vsg::Buffer> terrainBuf_;
    vsg::ref_ptr<vsg::DeviceMemory> terrainMem_;
    uint32_t terrainCells_=0;

    // ---- P2.2: 行变换分段 + 列转置中间缓冲 (n_fast > 512 时分配) ----
    vsg::ref_ptr<vsg::Buffer> tmpABuf_, tmpBBuf_, tmp2Buf_, rdTBuf_;
    vsg::ref_ptr<vsg::DeviceMemory> tmpAMem_, tmpBMem_, tmp2Mem_, rdTMem_;

    // ---- P2 验收: echo kernel GPU 时间戳 (vsg 无 QueryPool 封装, 原生管理) ----
    vsg::ref_ptr<vsg::Device> tsDevice_;        // 持引用供析构释放
    VkQueryPool_T* tsPool_ = VK_NULL_HANDLE;    // 槽 0=echo 前, 槽 1=echo 后
    double tsPeriodNs_ = 1.0;
    bool tsValid_ = false;
};

} // namespace sensor
} // namespace rockyenv
