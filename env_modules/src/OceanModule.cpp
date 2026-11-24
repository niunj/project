/// @file OceanModule.cpp —— rocky 海洋模块 (S1: 波场真值源版):
///   CPU 侧 WaveField (JONSWAP 谱 → Gerstner 谐波表) 为单一真值源, 每帧把
///   谐波写入 UBO oceanWave 段 (offset 92..123) 下发 ocean.vert/frag;
///   片元做菲涅尔 + 天空/深水色混合 + 雅可比白沫 + 海岸线掩膜 + 雾效。
///   对外实现 IOceanSampler (CPU/GPU 同公式采样) 与 IOceanControl (运行时控制)。
///   几何在 ENU 系, 由 EnvFrame.model 定位到地球; 近海尺度 (边长数 km)。
#include <rocky_env/EnvApi.h>
#include "EnvCommon.h"
#include "WaveField.h"
#include "OceanFFTField.h"

#include <vsg/all.h>   // 与传感器链 (RadarChain/BandRT) 一致, 拉齐 Commands/BindDescriptorSet 等完整定义

#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/nodes/Node.h>
#include <vsg/app/Window.h>
#include <vsg/commands/Commands.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
#include <mutex>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>

namespace rockyenv {

namespace {

/// UBO oceanWave 段: 共享常量见 EnvCommon.h (kOceanWaveUboOffset=92)。
/// v2 布局: oceanWave[16] (92..155) + oceanOpt (156..159)。
constexpr unsigned kOceanWaveUboFloats = 64;   // 16 × vec4
constexpr unsigned kOceanOptUboOffset = 156;

/// O1: 网格非均匀重映射 (幂次分布)。
/// f ∈ [0,1] → 局部坐标 [-half, half]。p>1 时格点向中心聚集: 以 size=3200/
/// grid=128 为例, 中心格 ~2 m (近场波浪细节), 边缘格 ~40 m (远场由距离雾/
/// 粗糙度掩盖, 掠射下不可分辨)。网格以相机为中心逐帧平移, 中心永远=近场。
/// 构造期顶点与 CPU updateTerrainHeights 必须使用同一函数, 保证地形掩膜
/// 采样点与顶点严格对齐。
constexpr float kGridPower = 1.6f;
inline float gridRemap(float f, float half) {
    const float s = 2.0f * f - 1.0f;
    return half * ((s < 0.0f) ? -std::pow(-s, kGridPower) : std::pow(s, kGridPower));
}

class OceanModule : public IEnvModule,
                    public IOceanSampler,
                    public IOceanControl {
public:
    explicit OceanModule(const OceanParams& p) : p_(p) {
        if (!std::isfinite(p_.fft_patch_size) || p_.fft_patch_size <= 0)
            throw std::invalid_argument("rocky_ocean: fft_patch_size must be finite and positive metres");
        // FFT 模式的 CPU/GPU 共用 h0；谐波表只提供海况谱参数。
        fft_mode_ = (p_.wave_mode == 2);
        buildGeometry();
        if(fft_mode_) {
            fft_=std::make_unique<OceanFFTField>();
            fft_->initializeCpu(512,p_.fft_patch_size);
            fft_->updateSpectrum(wave_field_,currentDepth_,wind_dir_x_,wind_dir_z_);
            spectrum_dirty_=false;
        }
        // 非 FFT 模式: 构造期直接建管线 (无 set=1 采样组, 向后兼容)。
        if (!fft_mode_) buildPipeline();
    }

    const char* name() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return "rocky_ocean"; }
    vsg::Node* rootNode() override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return holder_.get(); }

    /// P3 钩子: 宿主创建模块后注入窗口 (wave_mode=2 时立即 build GPU FFT
    /// 波场 + set=1 采样组管线)。非 FFT 模式可不调用 (构造期已建管线)。
    /// 必须在 viewer->compile() 之前调用, 使描述符/管线得以编译。
    void setWindow(vsg::Window* w) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        if (fft_mode_ && w) setDevice(w->getOrCreateDevice().get());
    }

    void setDevice(vsg::Device* device) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        if (fft_mode_ && device) buildFFT(*device);
    }

    vsg::Command* fftCommands() override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        return (fft_ && fft_->commands()) ? fft_->commands() : nullptr;
    }

    void onFrame(const EnvFrame& f) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        const uint64_t terrainRevision=p_.terrain ? p_.terrain->revision() : 0;
        const bool terrainChanged=terrainRevision!=wave_terrain_revision_;
        wave_terrain_revision_=terrainRevision;
        // S1: 风场/参数变化时重建波场 (脏标记节流; 相位随 time 在公式内推进)。
        maybeSyncWind(f,terrainChanged);

        // The global pass consumes the cheap sea-state spectrum at altitude.
        // Realize the FFT texture and coastline mask when their weight is nonzero.
        if(f.detailWeight<=0.f) {
            if(ub_ && (*ub_)[59]!=0.f) { (*ub_)[59]=0.f; ub_->dirty(); }
            return;
        }
        if(fft_mode_ && fft_) {
            if(spectrum_dirty_) {
                fft_->updateSpectrum(wave_field_,currentDepth_,wind_dir_x_,wind_dir_z_);
                spectrum_dirty_=false;
            }
            fft_->onFrame(float(f.time_sec),currentDepth_,wave_field_.choppy(),1);
        }
        if(!ub_)return; // Headless sampling remains valid without a Window.

        float misc[4] = {p_.sea_level, p_.size,
                         wave_field_.totalWaveHeight(),
                         wave_field_.peakWaveLength()};
        // 海洋边界扩展 UBO (ext0/ext1, 见 ocean.vert/frag):
        //   ext0 = (centerX, centerZ, radius或halfX, halfZ)
        //   ext1 = (fadeWidth, shape, choppy Q, beaufort)
        const auto& b = p_.bounds;
        const float ext[8] = {
            b.center_x, b.center_z,
            b.shape == OceanBounds::Rectangle ? b.half_x : b.radius,
            b.half_z,
            b.fade_width, static_cast<float>(b.shape),
            wave_field_.choppy(),
            static_cast<float>(wave_field_.beaufort())
        };
        // P3 FFT 平铺参数 (oceanOpt2): x=平铺边长 y=流向x z=流向z w=FFT开关。
        // 仅 FFT 就绪时 w=1 (ocean.vert 转采样位移纹理); 否则 0 → 谐波分支。
        const float opt2[4] = {
            fft_built_ ? fft_->patchSize() : 0.0f,
            wind_dir_x_, wind_dir_z_,
            fft_built_ ? 1.0f : 0.0f
        };
        // 海洋深浅水色 (OceanParams) 写入 offset 60..67, 供 frag 使用。
        fillEnvUBO(*ub_, localRenderFrame(f), misc, ext, &p_.deep_color, &p_.shallow_color, opt2);

        // S1: 谐波表下发 UBO oceanWave 段 (92..123) —— 谐波分支的波形来源。
        // (FFT 分支 shader 转采样纹理, 此段保持填充以保 CPU/传感器采样契约。)
        writeHarmonicsToUBO();

        // 海岸线掩膜: 经地形桥查询顶点高程 (节流, 见 updateTerrainHeights)。
        terrain_bridge_ = p_.terrain;
        if (terrain_bridge_ && uv_array_) {
            updateTerrainHeights(f);
        }
    }

    void setEnabled(bool on) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        enabled_ = on;
        if (holder_) holder_->setAllChildren(on);
    }
    bool enabled() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return enabled_; }

    // ---- IOceanSampler (CPU/GPU 同公式; onFrame 后有效) ----
    SeaState seaState() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        SeaState s;
        s.significant_wave_height = wave_field_.totalWaveHeight();  // 含涌浪
        s.peak_period = wave_field_.peakPeriod();
        s.beaufort = wave_field_.beaufort();
        // 主波向 = 谱峰分量方向 (谐波绕风向展开, 峰值沿风向)
        s.peak_direction_rad = std::atan2(wind_dir_z_, wind_dir_x_);
        return s;
    }
    float waveHeight(double ex, double ez, double t) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        const float height=(fft_mode_ && fft_) ? fftSurface(ex,ez,t).displacement.y : wave_field_.waveHeight(ex,ez,t);
        return height-(p_.curvature ? float((ex*ex+ez*ez)/(2*6371000.)) : 0.f);
    }
    vsg::vec3 waveNormal(double ex, double ez, double t) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        vsg::vec3 normal;
        if(fft_mode_ && fft_)normal=fftSurface(ex,ez,t).normal;
        else wave_field_.waveNormal(ex,ez,t,normal.x,normal.y,normal.z);
        if(p_.curvature)normal=vsg::normalize(normal+vsg::vec3(float(ex/6371000.)*normal.y,0,float(ez/6371000.)*normal.y));
        return normal;
    }
    vsg::vec3 orbitalVelocity(double ex, double ez, double t) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        vsg::vec3 velocity;
        if(fft_mode_ && fft_)velocity=fftSurface(ex,ez,t).velocity;
        else wave_field_.orbitalVelocity(ex,ez,t,velocity.x,velocity.y,velocity.z);
        if(p_.curvature)velocity.y-=float((ex*velocity.x+ez*velocity.z)/6371000.);
        return velocity;
    }
    float foamCoverage(double ex, double ez, double t) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        if(fft_mode_ && fft_) {
            const float j=fftSurface(ex,ez,t).jacobian;
            return std::clamp(1.f-std::clamp((j-.62f)/.2f,0.f,1.f)+.0035f*std::max(p_.wind_speed-6.5f,0.f),0.f,1.f);
        }
        return wave_field_.foamCoverage(ex, ez, t);
    }
    float seaTempK() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return p_.sea_temp_k; }   // 批次3: 传感器链真值
    int waveHarmonics(float out[16][4], float& q) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        q = wave_field_.choppy();
        if(fft_mode_)return 0; // A Fourier field cannot be represented by the legacy 16 harmonics.
        int n = 0;
        for (const auto& h : wave_field_.harmonics()) {
            if (n >= 16) break;
            out[n][0] = h.wave_length;
            out[n][1] = h.amplitude;
            out[n][2] = h.dir_cos;
            out[n][3] = h.dir_sin;
            ++n;
        }
        return n;
    }

    // ---- IOceanControl (运行时控制, 下一帧生效) ----
    void setSeaLevel(float sea_level) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); p_.sea_level = sea_level; has_sample_=false; wave_dirty_=true; }
    void setWaveHeight(float hs) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        // hs>0: 手动锁定; hs<=0: 恢复气象驱动 (auto_sea_state)。
        if (hs > 0.0f) {
            p_.wave_height = hs;
            p_.auto_sea_state = false;
        } else {
            p_.auto_sea_state = true;
        }
        wave_dirty_ = true;
    }
    void setWaveLength(float peak_len) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        p_.wave_length = peak_len;  // 0 = 按 JONSWAP 谱峰自动
        wave_dirty_ = true;
    }
    void setWind(float speed, vsg::vec3 dir) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        p_.wind_speed = speed;
        const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (len > 1e-6f) {
            wind_dir_x_ = dir.x / len;
            wind_dir_z_ = dir.z / len;
        }
        manual_wind_ = true;   // 显式设定后本帧起不再跟随 EnvFrame 风
        wave_dirty_ = true;
    }
    void setWaterColor(vsg::vec3 deep, vsg::vec3 shallow) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        p_.deep_color = deep;
        p_.shallow_color = shallow;
    }
    void setBounds(const OceanBounds& b) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); p_.bounds = b; has_sample_=false; wave_dirty_=true; }

    // ---- v3 (S2) ----
    void setSwell(float height, float length_m, float dir_deg) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        p_.swell_height = std::max(0.0f, height);
        p_.swell_length = std::max(20.0f, length_m);
        p_.swell_dir_deg = dir_deg;
        wave_dirty_ = true;
    }
    void setReflection(int level) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        // P6: 档位 2 = 方向相关 + 全偏振菲涅尔 (Fs/Fp 精确式); 0/1 旧行为。
        p_.reflection = std::clamp(level, 0, 2);
    }
    void setCurvature(bool on) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); p_.curvature = on; }

    float getSeaLevel() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return p_.sea_level; }
    float getWaveHeight() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        return wave_field_.totalWaveHeight();  // 含涌浪
    }
    float getWaveLength() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return wave_field_.peakWaveLength(); }
    float getWindSpeed() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return p_.wind_speed; }
    vsg::vec3 getWindDir() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        return vsg::vec3(wind_dir_x_, 0.0f, wind_dir_z_);
    }
    OceanBounds getBounds() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return p_.bounds; }

    // ---- v3 (S2) ----
    float getSwellHeight() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return wave_field_.swellWaveHeight(); }
    int   getReflection() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return p_.reflection; }
    bool  getCurvature() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return p_.curvature; }

private:
    OceanFFTField::Sample fftSurface(double x,double z,double time) const {
        // Invert choppy horizontal displacement so callers query Eulerian x/z.
        double u=x,v=z;OceanFFTField::Sample result;
        for(int i=0;i<4;++i) {result=fft_->sample(u,v,time);u=x-result.displacement.x;v=z-result.displacement.z;}
        return fft_->sample(u,v,time);
    }
    // ---- P3: 几何构建 (顶点网格/地形掩膜, 与管线解耦) ----
    void buildGeometry() {
        const int g = p_.grid;
        const float half = p_.size * 0.5f;
        std::vector<vsg::vec3> v, n;
        std::vector<vsg::vec2> uv;
        std::vector<uint32_t> idx;
        v.reserve(static_cast<size_t>(g + 1) * (g + 1));
        n.reserve(v.capacity());
        uv.reserve(v.capacity());
        idx.reserve(static_cast<size_t>(g) * g * 6);
        for (int j = 0; j <= g; ++j) {
            for (int i = 0; i <= g; ++i) {
                const float fx = static_cast<float>(i) / g;
                const float fz = static_cast<float>(j) / g;
                v.emplace_back(gridRemap(fx, half), 0.0f, gridRemap(fz, half));
                n.emplace_back(0.0f, 1.0f, 0.0f);
                uv.emplace_back(-1000.0f, 1000.0f);
            }
        }
        for (int j = 0; j < g; ++j) {
            for (int i = 0; i < g; ++i) {
                const uint32_t a = j * (g + 1) + i;
                const uint32_t b = a + 1;
                const uint32_t c = a + (g + 1);
                const uint32_t d = c + 1;
                idx.push_back(a); idx.push_back(c); idx.push_back(b);
                idx.push_back(b); idx.push_back(c); idx.push_back(d);
            }
        }
        auto va = vsg::vec3Array::create(v.size());
        auto na = vsg::vec3Array::create(n.size());
        auto ua = vsg::vec2Array::create(uv.size());
        for (size_t i = 0; i < v.size(); ++i) { (*va)[i] = v[i]; (*na)[i] = n[i]; (*ua)[i] = uv[i]; }
        auto ia = vsg::uintArray::create(idx.size());
        for (size_t i = 0; i < idx.size(); ++i) (*ia)[i] = idx[i];
        auto vid = vsg::VertexIndexDraw::create();
        vid->assignArrays({va, na, ua});
        vid->assignIndices(ia);
        vid->indexCount = static_cast<uint32_t>(idx.size());
        vid->instanceCount = 1;
        draw_ = vid;
        wave_dynamics_=vsg::floatArray::create(64);
        wave_dynamics_->properties.dataVariance=vsg::DYNAMIC_DATA;
        for(auto& value:*wave_dynamics_)value=0;
        uv_array_ = ua;
        rebuildWaveField();   // S1: 初始波场 (首帧 onFrame 按 EnvFrame 风重建)
        holder_ = vsg::Switch::create();
    }

    // ---- 管线构建 (非 FFT 构造期 / FFT setWindow 后; 重建时替换旧 holder) ----
    void buildPipeline() {
        if (!holder_ || !draw_)
            throw std::runtime_error("rocky_ocean: ocean geometry is unavailable");
        vsg::ref_ptr<vsg::floatArray> ub;
        vsg::Descriptors set1;
        if (fft_mode_ && fft_built_ && fft_) {
            // P3 FFT: set=1 采样位移/法线泡沫纹理。
            auto s = vsg::ref_ptr<vsg::Sampler>(fft_->sampler());
            set1.push_back(vsg::DescriptorImage::create(
                vsg::ImageInfo::create(s,
                    vsg::ref_ptr<vsg::ImageView>(fft_->displacementMap()),
                    VK_IMAGE_LAYOUT_GENERAL), 0));
            set1.push_back(vsg::DescriptorImage::create(
                vsg::ImageInfo::create(s,
                    vsg::ref_ptr<vsg::ImageView>(fft_->slopeFoamMap()),
                    VK_IMAGE_LAYOUT_GENERAL), 1));
        }
        set1.push_back(vsg::DescriptorBuffer::create(wave_dynamics_,2));
        // P3: FFT 模式用 ocean_fft.vert (set=1 采样位移纹理), 谐波模式用
        // ocean.vert (仅 set=0 环境 UBO)。两条管线布局各自自洽。
        const char* vert = (fft_mode_ && fft_built_ && fft_)
                         ? "ocean_fft.vert" : "ocean.vert";
        auto sg = buildEnvPipeline(vert, "ocean.frag", true,
                                   /*depth_test=*/true, ub,
                                   set1.empty() ? nullptr : &set1,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   &bind_set1_);
        if (!sg)
            throw std::runtime_error(std::string("rocky_ocean: graphics pipeline initialization failed: ")+vert+"/ocean.frag");
        // 直接挂 (VertexIndexDraw 继承 Command, 与 Group::addChild(ref_ptr<Node>)
        // 兼容 —— VSG Command 与 Node 同为 Object 子类, ref_ptr 隐式转换)。
        sg->addChild(vsg::ref_ptr<vsg::Node>(draw_.get()));
        // 就地替换场景子节点 (holder 是场景图已挂载节点, 不可重建): 清空后挂新
        // StateGroup, 保持挂载引用与显隐状态不变。
        const bool on = enabled_;
        holder_->children.clear();
        holder_->addChild(true, vsg::ref_ptr<vsg::Node>(sg.get()));
        holder_->setAllChildren(on);
        sg_ = sg;
        ub_ = ub;
    }

    // ---- P3: GPU FFT 波场构建 + set=1 采样组管线重建 ----
    void buildFFT(vsg::Device& device) {
        if (fft_built_) {
            if (fft_device_ != &device) throw std::logic_error("create a new ocean module for a different Vulkan device");
            return;
        }
        if(!fft_)fft_ = std::make_unique<OceanFFTField>();
        // Wave resolution is independent of the extent covered by the mesh.
        // World ENU sampling preserves phase when the observer/grid moves.
        auto r = fft_->buildOffscreen(device, 512, p_.fft_patch_size);
        if (!r.ok) {
            throw std::runtime_error(std::string("rocky_ocean: requested FFT initialization failed: ")+r.error);
        }
        fft_device_=&device;
        fft_built_ = true;
        fft_->updateSpectrum(wave_field_, currentDepth_,wind_dir_x_,wind_dir_z_);
        spectrum_dirty_=false;
        try {
            buildPipeline();   // 重建带 set=1 采样组的管线 (编译期注入真实纹理)
        } catch (...) {
            // Preserve the requested FFT model and permit an explicit retry;
            // the host receives the error instead of an empty sea or new phases.
            fft_built_ = false;
            throw;
        }
        std::printf("[rocky_ocean] wave_mode=2 GPU FFT 波场启用 (patch=%.0fm)\n",
                    fft_->patchSize());
    }

    // ---- S1: 波场管理 ----
    void rebuildWaveField(bool rebuildSpectrum=true) {
        const WaveField::Mode mode = (p_.wave_mode == 0)
            ? WaveField::GerstnerLegacy : WaveField::GerstnerSpectral;
        // P1 浅水色散: 有地形桥时取海域中心代表水深 (>0 启用 tanh(kd) 修正);
        // 无地形/查询失败保持深水 (-1, ω=√gk 与旧版一致)。
        float depth = -1.0f;
        if (p_.terrain) {
            const float th = p_.terrain->waterBedENU(p_.bounds.center_x,p_.bounds.center_z,p_.sea_level);
            if (!std::isnan(th)) depth = std::max(0.0f, p_.sea_level - th);
        }
        currentDepth_=depth;
        wave_field_.generate(
            mode, p_.wind_speed, wind_dir_x_, wind_dir_z_,
            p_.fetch_m, p_.duration, p_.harmonics,
            /*hs_override=*/(!p_.auto_sea_state && p_.wave_height > 0.0f)
                ? p_.wave_height : 0.0f,
            /*peak_len_override=*/(p_.wave_length > 0.0f) ? p_.wave_length : 0.0f,
            /*depth_m=*/depth);
        wave_field_.setChoppy(p_.choppy);
        // S2: 涌浪叠加 (风浪谐波之后)。
        WaveField::Swell sw;
        sw.height = p_.swell_height;
        sw.length = p_.swell_length;
        sw.dir_off_deg = p_.swell_dir_deg;
        wave_field_.addSwell(sw, wind_dir_x_, wind_dir_z_);
        // Bathymetry changes dispersion. h0 amplitudes and swell phases depend on
        // sea state and wind; OceanFFTField::onFrame applies the current depth.
        if(rebuildSpectrum) spectrum_dirty_=true;
    }

    // 风同步: 气象驱动 (auto_sea_state && !manual_wind_) 跟随 EnvFrame 风;
    // setWind 显式设定后锁定为手动风。任一来源变化 → 重建波场。
    void maybeSyncWind(const EnvFrame& f,bool terrainChanged) {
        bool dirty = wave_dirty_;
        if (p_.auto_sea_state && !manual_wind_) {
            const float len = std::sqrt(f.wind_dir_enu.x * f.wind_dir_enu.x +
                                        f.wind_dir_enu.z * f.wind_dir_enu.z);
            const float wx = len > 1e-6f ? f.wind_dir_enu.x / len : wind_dir_x_;
            const float wz = len > 1e-6f ? f.wind_dir_enu.z / len : wind_dir_z_;
            if (std::abs(f.wind_speed - p_.wind_speed) > 0.05f ||
                std::abs(wx - wind_dir_x_) > 1e-3f ||
                std::abs(wz - wind_dir_z_) > 1e-3f) {
                p_.wind_speed = f.wind_speed;
                wind_dir_x_ = wx;
                wind_dir_z_ = wz;
                dirty = true;
            }
        }
        if (dirty || terrainChanged) {
            rebuildWaveField(dirty);
            wave_dirty_ = false;
        }
    }

    void writeHarmonicsToUBO() {
        const auto& waves = wave_field_.harmonics();
        const unsigned n = std::min<unsigned>(static_cast<unsigned>(waves.size()),
                                              kOceanWaveMax);
        for (unsigned i = 0; i < n; ++i) {
            (*ub_)[kOceanWaveUboOffset + i * 4 + 0] = waves[i].wave_length;
            (*ub_)[kOceanWaveUboOffset + i * 4 + 1] = waves[i].amplitude;
            (*ub_)[kOceanWaveUboOffset + i * 4 + 2] = waves[i].dir_cos;
            (*ub_)[kOceanWaveUboOffset + i * 4 + 3] = waves[i].dir_sin;
            (*wave_dynamics_)[i*4]=waves[i].omega;
            (*wave_dynamics_)[i*4+1]=waves[i].phase;
        }
        // 尾部空槽振幅填 0 (shader 端 A<=0 跳过该谐波)。
        for (unsigned i = n; i < kOceanWaveMax; ++i) {
            for (unsigned c = 0; c < 4; ++c)
                (*ub_)[kOceanWaveUboOffset + i * 4 + c] = 0.0f;
        }
        // oceanOpt (S2): x=涌浪Hs y=反射档位 z=曲率 w=谐波数。
        (*ub_)[kOceanOptUboOffset + 0] = wave_field_.swellWaveHeight();
        (*ub_)[kOceanOptUboOffset + 1] = static_cast<float>(p_.reflection);
        (*ub_)[kOceanOptUboOffset + 2] = p_.curvature ? 1.0f : 0.0f;
        (*ub_)[kOceanOptUboOffset + 3] = static_cast<float>(n);
        ub_->dirty();wave_dynamics_->dirty();
    }

    // 更新顶点 UV 中的地形高度信息 (与 ocean.vert 的网格中心钳制逻辑一致)。
    // 性能: 每帧全量 (g+1)^2 次地形查询过重 —— 采用 stride=2 采样 (同 2x2 单元
    // 共享一个查询值, 光栅化时双线性插值) + 移动阈值节流 (网格中心移动不足一个
    // 单元时复用上一帧结果)。
    void updateTerrainHeights(const EnvFrame& f) {
        if (!uv_array_ || !terrain_bridge_) return;

        const int g = p_.grid;
        const float half = p_.size * 0.5f;
        const float cell = p_.size / static_cast<float>(g);

        // 计算网格中心 (与 vertex shader 一致)。
        const auto& b = p_.bounds;
        const float cam_x = static_cast<float>(f.cam_pos_enu.x);
        const float cam_z = static_cast<float>(f.cam_pos_enu.z);
        const float bc_x = b.center_x;
        const float bc_z = b.center_z;
        float bh_x, bh_z;
        if (b.shape == OceanBounds::Rectangle) {
            bh_x = b.half_x;
            bh_z = b.half_z;
        } else {
            bh_x = b.radius;
            bh_z = b.radius;
        }
        const float meshHalf = p_.size * 0.5f;
        const float span_x = std::max(bh_x - meshHalf, 0.0f);
        const float span_z = std::max(bh_z - meshHalf, 0.0f);
        const float center_x = bc_x + std::clamp(cam_x - bc_x, -span_x, span_x);
        const float center_z = bc_z + std::clamp(cam_z - bc_z, -span_z, span_z);

        // 节流: 网格中心移动不足一个单元时跳过 (顶点位移随相机, 掩膜滞后 ≤2 单元)。
        const uint64_t revision=terrain_bridge_->revision();
        const bool changed=revision!=terrain_revision_ || sampled_sea_level_!=p_.sea_level;
        const auto now=std::chrono::steady_clock::now();
        if(!changed && !has_sample_ && now<next_terrain_retry_)return;
        if (has_sample_ && !changed) {
            const float dx = center_x - last_cx_;
            const float dz = center_z - last_cz_;
            if (dx * dx + dz * dz < cell * cell) return;
        }
        last_cx_ = center_x;
        last_cz_ = center_z;
        has_sample_ = true;terrain_revision_=revision;sampled_sea_level_=p_.sea_level;
        next_terrain_retry_=now+std::chrono::milliseconds(1000);

        // stride=2 采样: 偶数格点查询, 同 2x2 单元的 4 个顶点共享。
        // O1: xz 用与顶点相同的 gridRemap, 保证采样点与顶点对齐。
        for (int j = 0; j <= g; j += 2) {
            for (int i = 0; i <= g; i += 2) {
                const float fx = static_cast<float>(i) / g;
                const float fz = static_cast<float>(j) / g;
                const float xz_x = gridRemap(fx, half) + center_x;
                const float xz_z = gridRemap(fz, half) + center_z;

                float terrainH = terrain_bridge_->waterBedENU(xz_x,xz_z,p_.sea_level);
                // 查询不可用 (NaN) 时按深水处理, 避免 NaN 污染 discard/mix。
                const bool valid=std::isfinite(terrainH);
                if(!valid)has_sample_=false;
                const float depth=valid ? std::max(0.f,p_.sea_level-terrainH) : -1e20f;
                if(!valid)terrainH=p_.sea_level; // Hidden by the explicit invalid-depth mask.
                const vsg::vec2 hv(terrainH, depth);

                // 写入 2x2 单元 (边界处 i+1/j+1 可能越界, 逐顶点判定)。
                for (int dj = 0; dj < 2 && j + dj <= g; ++dj) {
                    for (int di = 0; di < 2 && i + di <= g; ++di) {
                        const size_t idx = static_cast<size_t>(j + dj) * (g + 1) + (i + di);
                        (*uv_array_)[idx] = hv;
                    }
                }
            }
        }
        uv_array_->dirty();
    }

    mutable std::recursive_mutex state_mutex_;
    OceanParams p_;
    uint64_t terrain_revision_=std::numeric_limits<uint64_t>::max(),wave_terrain_revision_=0;
    float sampled_sea_level_=std::numeric_limits<float>::quiet_NaN();
    std::chrono::steady_clock::time_point next_terrain_retry_{};
    float wind_dir_x_ = 1.0f;      ///< 风向单位向量 x (ENU; OceanParams 无方向字段)
    float wind_dir_z_ = 0.0f;      ///< 风向单位向量 z
    bool  manual_wind_ = false;    ///< setWind 显式设定后锁定, 不再跟随 EnvFrame
    bool  has_sample_ = false;     ///< 地形掩膜是否已至少采样一次
    float last_cx_ = 0.0f;         ///< 上次采样的网格中心 ENU x
    float last_cz_ = 0.0f;         ///< 上次采样的网格中心 ENU z
    vsg::ref_ptr<vsg::StateGroup> sg_;
    vsg::ref_ptr<vsg::Switch> holder_;
    vsg::ref_ptr<vsg::floatArray> ub_,wave_dynamics_;
    vsg::ref_ptr<vsg::VertexIndexDraw> draw_;     // P3: 网格与管线解耦 (复用于重建)
    vsg::ref_ptr<vsg::vec2Array> uv_array_;  // Phase 2: 顶点 UV 数组
    vsg::ref_ptr<vsg::BindDescriptorSet> bind_set1_;  // P3: FFT set=1 采样组绑定
    ITerrainBridge* terrain_bridge_ = nullptr;  // Phase 2: 地形查询接口
    bool enabled_ = true;

    // S1: 波场真值源与脏标记
    WaveField wave_field_;
    bool wave_dirty_ = false;
    bool spectrum_dirty_ = true;

    // P3: GPU FFT 波场 (wave_mode=2; 宿主 setWindow 注入时同步 build,
    // 须早于 viewer->compile 以编译 set=1 采样描述符与管线)
    bool fft_mode_ = false;
    bool fft_built_ = false;
    vsg::Device* fft_device_ = nullptr; // owned by the built FFT field
    std::unique_ptr<OceanFFTField> fft_;
    float currentDepth_ = -1.0f;   // P1 代表水深 (浅水色散/FFT 谱共用)
};

}  // namespace

std::unique_ptr<IEnvModule> createOceanModule(const OceanParams& p)
{
    return std::unique_ptr<IEnvModule>(new OceanModule(p));
}

}  // namespace rockyenv
