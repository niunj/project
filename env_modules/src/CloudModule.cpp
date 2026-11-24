/// @file CloudModule.cpp —— rocky 云层模块 (v4: 多层语义化):
///   1) 层云片 (Stratus/Cirrus/Stratocumulus): 高空水平层, fbm 云量图,
///      类型化成形 (卷云拉伸 / 层积云团块); 世界坐标连续平铺 + 风漂移;
///      降水时云体变暗 + 云底雨幡;
///   2) 积云公告板环 (Cumulus/Cumulonimbus): 相机周围环形精灵, 类型化
///      (积雨云塔状 + 砧状顶 + 雨幡), 银边 + 向光面渐变。
///   CPU 侧与 shader 共用同一 2D fbm 云量函数 (cloudf2) —— ICloudQuery
///   的 shadowFactor/totalCoverage 与画面同源。
/// 几何均在锚点 ENU 系 (局部米制, +Y 朝上), 由 EnvFrame.model 定位到地球。
#include <rocky_env/EnvApi.h>
#include <rocky_env/CloudField.h>
#include "EnvCommon.h"

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/nodes/Node.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <mutex>

namespace rockyenv {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// 平滑阶跃 (供 cloudDensity3D / 云影 / coverageAt 共用; 与 GLSL smoothstep 同式)。
float smoothstepf(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.0f : 1.0f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 确定性伪随机 (云块布局, 无需随机库)
float hash01(int i) {
    uint32_t x = static_cast<uint32_t>(i) * 747796405u + 2891336453u;
    x = (x ^ (x >> 13)) * 1274126177u;
    x ^= (x >> 16);
    return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

/// CPU 侧 2D 值噪声 (与 cloud_sheet.frag 的 hash/noise 逐公式一致 —— 同源)。
float hash2(float x, float y) {
    // GLSL: p = fract(p * vec2(123.34, 456.21)); p += dot(p, p+45.32);
    //       return fract(p.x * p.y);
    float px = std::fmod(x * 123.34f, 1.0f);  if (px < 0) px += 1.0f;
    float py = std::fmod(y * 456.21f, 1.0f);  if (py < 0) py += 1.0f;
    const float d = px * (px + 45.32f) + py * (py + 45.32f);
    px += d;  py += d;
    return std::fmod(px * py, 1.0f);
}
float vnoise2(float x, float y) {
    const float ix = std::floor(x), iy = std::floor(y);
    const float fx = x - ix, fy = y - iy;
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash2(ix, iy);
    const float b = hash2(ix + 1.0f, iy);
    const float c = hash2(ix, iy + 1.0f);
    const float d = hash2(ix + 1.0f, iy + 1.0f);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}
/// fbm (与 cloud_sheet.frag 主噪声同参数: 0.35 缩放的 5 octave)。
float cloudf2(float x, float y) {
    float v = 0.0f, a = 0.5f;
    for (int i = 0; i < 5; ++i) {
        v += a * vnoise2(x, y);
        x *= 2.03f; y *= 2.03f;
        a *= 0.5f;
    }
    return v;
}

// ---------------------------------------------------------------------------
// P3-4b: 3D 体积云噪声 (与 cloud_volume.frag 逐公式同源 —— CPU/GPU 同密度场)。
// 卷积语义: sheet 是 2D (xy 分量), 体积云是 3D (xyz 分量); dot(p, p+45.32)
// 同时加到各分量后再 fract, 保证 CPU 云量/云影与画面一致 (AGENT.md 同源契约)。
// ---------------------------------------------------------------------------
float kFbmCoverageThreshold(float cov) { return cloudfield::coverageThreshold(cov); }

/// 生成一个 (segs+1)^2 水平网格 (y=altitude, 边长 size, 中心原点)。
void makeSheetGrid(float size, float altitude, int segs,
                   std::vector<vsg::vec3>& v, std::vector<vsg::vec3>& n,
                   std::vector<vsg::vec2>& uv, std::vector<uint32_t>& idx)
{
    const float half = size * 0.5f;
    for (int j = 0; j <= segs; ++j) {
        for (int i = 0; i <= segs; ++i) {
            const float fx = static_cast<float>(i) / segs;
            const float fz = static_cast<float>(j) / segs;
            v.emplace_back(-half + fx * size, altitude, -half + fz * size);
            n.emplace_back(0.0f, 1.0f, 0.0f);
            uv.emplace_back(fx, fz);
        }
    }
    for (int j = 0; j < segs; ++j) {
        for (int i = 0; i < segs; ++i) {
            const uint32_t a = j * (segs + 1) + i;
            const uint32_t b = a + 1;
            const uint32_t c = a + (segs + 1);
            const uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    }
}

// ---------------------------------------------------------------------------
// 运行时层 (可动态增删改)
// ---------------------------------------------------------------------------
struct CloudLayerRT {
    CloudLayerParams cfg;
    bool enabled = true;
    float sheet_size_ = 6000.0f;   // 层云片边长 (类型查表)
    // 渲染资产
    vsg::ref_ptr<vsg::Switch> holder;       // 层开关
    vsg::ref_ptr<vsg::StateGroup> sg_sheet; // 水平层管线 (Stratus/Cirrus/SC)
    vsg::ref_ptr<vsg::floatArray> ub_sheet;
    vsg::ref_ptr<vsg::StateGroup> sg_spr;   // 公告板/体积管线 (Cu/Cb)
    vsg::ref_ptr<vsg::floatArray> ub_spr;
    vsg::ref_ptr<vsg::vec3Array> spr_v;
    vsg::ref_ptr<vsg::floatArray> ub_vol;   // P3-4b: 体积管线独立 UBO (sprite 切体积时重建)
    // 块布局
    struct Block { float angle, rad, size, alt; };
    std::vector<Block> blocks;
    bool isSheetType() const {
        return cfg.type == CloudLayerType::Stratus ||
               cfg.type == CloudLayerType::Cirrus ||
               cfg.type == CloudLayerType::Stratocumulus;
    }
};

class CloudModule : public IEnvModule,
                    public ICloudControl,
                    public ICloudQuery {
public:
    explicit CloudModule(const CloudParams& p) : p_(p) {
        root_ = vsg::Switch::create();
        coverage_mod_ = p_.cover;

        if (!p_.layers.empty()) {
            for (const auto& lp : p_.layers) addLayer(lp);
        } else {
            // 单层兼容模式: 层云 + 积云环 (一期行为)。
            CloudLayerParams sheet;
            sheet.type = CloudLayerType::Stratus;
            sheet.altitude = p_.stratus_altitude;
            sheet.thickness = 450.0f;
            sheet.coverage = p_.cover;
            sheet.density = 0.4f;
            addLayer(sheet);
            CloudLayerParams cu;
            cu.type = CloudLayerType::Cumulus;
            cu.altitude = p_.cumulus_altitude;
            // Native voxel weather uses the layer envelope as its physical
            // ray-march domain. A 2.4 km shell made all convective clouds look
            // like a sheet from aircraft altitude; 4.2 km admits cumulus
            // congestus towers while the density profile still keeps weak
            // columns shallow.
            cu.thickness = 4200.0f;
            cu.coverage = p_.cover;
            cu.density = 0.45f;
            addLayer(cu);
        }
        std::fprintf(stderr, "[rocky_env] CloudModule v4: %zu 层\n", layers_.size());
    }

    const char* name() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return "rocky_cloud"; }
    vsg::Node* rootNode() override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return root_.get(); }

    void onFrame(const EnvFrame& f) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        frame_ = f;   // ICloudQuery 采样用 (太阳方向/时间/风)
        const float precip = f.precipitation;

        for (auto& L : layers_) {
            if (!L.enabled) continue;
            const float cov = effectiveCoverage(L);
            const float typeF = static_cast<float>(L.cfg.type);

            if (L.ub_sheet) {
                // misc: x=层尺寸 y=覆盖率 z=高度 w=类型枚举
                float misc[4] = {L.sheet_size_, cov, L.cfg.altitude, typeF};
                // ext0: x=降水 y=层厚 z=密度 w=unused
                const float ext[8] = {precip, L.cfg.thickness, L.cfg.density, 0.0f,
                                      0, 0, 0, 0};
                fillEnvUBO(*L.ub_sheet, localRenderFrame(f), misc, ext);
            }
            // 体积层无公告板顶点数组 (spr_v 仅在公告板分支创建), 故判定只查 ub_spr;
            // 公告板路径才需要 spr_v (billboard 更新)。
            if (L.ub_spr && (p_.volume ? true : static_cast<bool>(L.spr_v))) {
                if (p_.volume && !L.isSheetType()) {
                    // P3-4b 体积云: misc=(覆盖,类型,降水,密度); ext0=(层底,层厚,团块尺寸,覆盖调制)。
                    const float sizeK = (L.cfg.type == CloudLayerType::Cumulonimbus) ? 2.2f : 1.0f;
                    const float blockSize = 620.0f * sizeK;
                    float misc[4] = {cov, typeF, precip, L.cfg.density};
                    const float ext[8] = {L.cfg.altitude, L.cfg.thickness, blockSize,
                                          coverage_mod_, 0, 0, 0, 0};
                    fillEnvUBO(*L.ub_spr, localRenderFrame(f), misc, ext);
                } else {
                    float misc[4] = {cov, typeF, precip, L.cfg.density};
                    fillEnvUBO(*L.ub_spr, localRenderFrame(f), misc);
                }
                // 公告板才跟随相机更新顶点; 体积壳体网格静止 (相机跟随在 shader 内)。
                if (!p_.volume && L.spr_v) updateBillboards(L, f);
            }
        }
    }

    void setEnabled(bool on) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        enabled_ = on;
        if (root_) root_->setAllChildren(on);
    }
    bool enabled() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return enabled_; }

    // ---- ICloudControl ----
    void setCoverage(float coverage) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        coverage_mod_ = std::clamp(coverage, 0.0f, 1.0f);
    }
    void setLayers(const std::vector<CloudLayerParams>& layers) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        // 整体重换: 清空重建。
        layers_.clear();
        root_->children.clear();   // Switch 子节点清空 (ref_ptr 释放资产)
        for (const auto& lp : layers) addLayer(lp);
    }
    void setLayerCoverage(int index, float coverage) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        if (index < 0 || index >= static_cast<int>(layers_.size())) return;
        layers_[index].cfg.coverage = std::clamp(coverage, 0.0f, 1.0f);
    }
    void setLayerEnabled(int index, bool on) override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        if (index < 0 || index >= static_cast<int>(layers_.size())) return;
        layers_[index].enabled = on;
        if (layers_[index].holder)
            layers_[index].holder->setAllChildren(on);
    }
    float getCoverage() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_); return coverage_mod_; }
    std::vector<CloudLayerParams> getLayers() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        std::vector<CloudLayerParams> out;
        out.reserve(layers_.size());
        for (const auto& L : layers_) out.push_back(L.cfg);
        return out;
    }

    // ---- ICloudQuery (与画面同源) ----
    bool cloudBase(float& out_base_m) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        float base = 1e9f; bool any = false;
        for (const auto& L : layers_) {
            if (!L.enabled || effectiveCoverage(L) < 0.05f) continue;
            base = std::min(base, L.cfg.altitude);
            any = true;
        }
        if (!any) return false;
        out_base_m = base;
        return true;
    }
    bool cloudTop(float& out_top_m) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        float top = -1e9f; bool any = false;
        for (const auto& L : layers_) {
            if (!L.enabled || effectiveCoverage(L) < 0.05f) continue;
            // 砧状积雨云可达对流层顶
            const float t = L.cfg.thickness;
            top = std::max(top, L.cfg.altitude + t);
            any = true;
        }
        if (!any) return false;
        out_top_m = top;
        return true;
    }
    float shadowFactor(double ex, double ez, double sun_el_rad) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        // 层云沿太阳光线投影: 地面点沿太阳方向上行至云底高度处采样云量图。
        // 与 cloud_sheet.frag 的 density→alpha 同函数 (cloudf2, 同漂移)。
        if (sun_el_rad < 0.05) return 1.0f;   // 太阳在地平线附近: 不投影
        float shade = 1.0f;
        for (const auto& L : layers_) {
            if (!L.enabled) continue;
            if (!L.isSheetType()) {
                if (p_.volume) {
                    const float y=std::max(float(std::sin(sun_el_rad)),.05f);
                    const float horizontal=std::hypot(frame_.sun_dir_enu.x,frame_.sun_dir_enu.z);
                    const float scale=horizontal>1e-6f ? float(std::cos(sun_el_rad))/horizontal : 0.f;
                    shade*=volumeTransmittance(L,float(ex),float(ez),frame_.sun_dir_enu.x*scale,y,frame_.sun_dir_enu.z*scale);
                }
                continue;
            }
            const float cov = effectiveCoverage(L);
            if (cov < 0.05f) continue;
            const auto sd=frame_.sun_dir_enu;
            const float distance=L.cfg.altitude/std::max(sd.y,.05f);
            const float a=sheetOpacity(L,float(ex)+sd.x*distance,float(ez)+sd.z*distance);
            shade*=std::pow(std::max(1.f-a,.001f),1.f/std::max(float(std::sin(sun_el_rad)),.05f));
        }
        return std::clamp(shade, 0.0f, 1.0f);
    }
    float totalCoverage() const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        // 各层覆盖加权 (1-∏(1-coverage·mod)): 视觉近似"总云量"。
        float rem = 1.0f;
        for (const auto& L : layers_) {
            if (!L.enabled) continue;
            rem *= 1.0f - effectiveCoverage(L);
        }
        return std::clamp(1.0f - rem, 0.0f, 1.0f);
    }
    float coverageAt(double ex, double ez) const override {
        std::lock_guard<std::recursive_mutex> stateLock(state_mutex_);
        // 垂直视线云密度: ENU 水平点 (ex,ez) 正上方的层云 alpha (与
        // shadowFactor / cloud_sheet.frag 同一 cloudf2 + 风漂移 + 分位数阈值,
        // 仅去掉太阳几何 —— 观测视线垂直投影到云层底高度处采样)。
        float trans = 1.0f;
        for (const auto& L : layers_) {
            if (!L.enabled) continue;
            if (L.isSheetType()) {
                trans *= 1.f-sheetOpacity(L,float(ex),float(ez));
            } else {
                // P3-4b 体积云: 3D ray-march 密度场 (cloudDensity3D 与 cloud_volume.frag
                // 逐公式同源); 公告板精灵走旧离散块投影 (与 updateBillboards 同块心公式)。
                const float cov = effectiveCoverage(L);
                if (cov < 0.05f || (!p_.volume && L.blocks.empty())) continue;
                if (p_.volume) {
                    trans *= volumeTransmittance(L,float(ex),float(ez),0,1,0);
                } else {
                    // 公告板积云: 离散块投影覆盖 (与 updateBillboards 同一块心公式)。
                    float blockA = 0.0f;
                    for (size_t i = 0; i < L.blocks.size(); ++i) {
                        const auto& b = L.blocks[i];
                        const float cx = std::cos(b.angle) * b.rad;
                        const float cz = std::sin(b.angle) * b.rad;
                        const float dx = static_cast<float>(ex) - cx;
                        const float dz = static_cast<float>(ez) - cz;
                        const float d = std::sqrt(dx * dx + dz * dz);
                        const float ri = b.size * 0.5f;   // 投影半径 (块宽一半)
                        if (d >= ri) continue;
                        // 软边: 0.55R 内全密度 → 1R 渐隐; 幅度与 frag 的 cover/density 门一致
                        const float shape = 1.0f - smoothstepf(0.55f, 1.0f, d / ri);
                        blockA = std::max(blockA, shape);
                    }
                    if (blockA > 0.0f) {
                        const float a = blockA * std::clamp(0.25f + cov, 0.0f, 1.0f);
                        trans *= 1.0f - std::clamp(a, 0.0f, 0.95f);
                    }
                }
            }
        }
        return std::clamp(1.0f - trans, 0.0f, 1.0f);
    }

private:
    float sheetOpacity(const CloudLayerRT& layer,float x,float z) const {
        const float cov=effectiveCoverage(layer);if(cov<.05f)return 0;
        float wx=frame_.wind_dir_enu.x,wz=frame_.wind_dir_enu.z;const float length=std::hypot(wx,wz);
        if(length>1e-6f){wx/=length;wz/=length;}else {wx=1;wz=0;}
        const float u=(x-wx*float(frame_.time_sec)*frame_.wind_speed)/600.f;
        const float v=(z-wz*float(frame_.time_sec)*frame_.wind_speed)/600.f;
        const float threshold=kFbmCoverageThreshold(cov);
        float density,alphaScale=1;
        if(layer.cfg.type==CloudLayerType::Cirrus) {
            density=cloudf2((u*wx+v*wz)*.08f,(-u*wz+v*wx)*.5f);alphaScale=.55f;
        } else if(layer.cfg.type==CloudLayerType::Stratocumulus) {
            density=cloudf2(u*.35f,v*.35f)*.55f+cloudf2(u*1.6f+7.3f,v*1.6f+7.3f)*.45f;
        } else density=cloudf2(u*.35f,v*.35f)*.7f+cloudf2(u*.9f+7.3f,v*.9f+7.3f)*.3f;
        return smoothstepf(threshold-.06f,threshold+.06f,density)*alphaScale*(.5f+.5f*std::clamp(layer.cfg.density,0.f,1.f));
    }
    float volumeTransmittance(const CloudLayerRT& layer,float x,float z,float dx,float dy,float dz) const {
        if(dy<=0)return 1.f;
        const float begin=std::max(0.f,(layer.cfg.altitude-200.f)/dy);
        const float end=(layer.cfg.altitude+layer.cfg.thickness+200.f)/dy;
        if(end<=begin)return 1.f;
        const int count=layer.cfg.type==CloudLayerType::Cumulonimbus ? 32 : 20;
        const float step=(end-begin)/count;
        float opticalDepth=0;
        for(int sample=0;sample<count;++sample) {
            const float distance=begin+(sample+.5f)*step;
            const float d=cloudfield::density(x+dx*distance,dy*distance,z+dz*distance,
                layer.cfg.altitude,layer.cfg.thickness,effectiveCoverage(layer),layer.cfg.density,
                float(frame_.time_sec),frame_.wind_dir_enu.x,frame_.wind_dir_enu.z,frame_.wind_speed);
            if(d>.02f)opticalDepth+=d*.03f*step;
            if(opticalDepth>8.f)break;
        }
        return std::exp(-opticalDepth);
    }
    static float smoothstepf(float e0, float e1, float x) {
        if (e1 <= e0) return x < e0 ? 0.0f : 1.0f;
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    float effectiveCoverage(const CloudLayerRT& L) const {
        return std::clamp(L.cfg.coverage * coverage_mod_ * frame_.cloudEnvelope, 0.0f, 1.0f);
    }

    void addLayer(const CloudLayerParams& lp) {
        CloudLayerRT L;
        L.cfg = lp;
        L.holder = vsg::Switch::create();

        if (L.isSheetType()) {
            // 水平层片: 尺寸按类型 (卷云更大更薄)。
            L.sheet_size_ = (lp.type == CloudLayerType::Cirrus) ? 12000.0f
                            : (lp.type == CloudLayerType::Stratocumulus) ? 8000.0f
                            : p_.stratus_size;
            vsg::ref_ptr<vsg::floatArray> ub;
            L.sg_sheet = buildEnvPipeline("cloud_sheet.vert", "cloud_sheet.frag",
                                          true, true, ub);
            if (L.sg_sheet) {
                L.ub_sheet = ub;
                std::vector<vsg::vec3> v, n;
                std::vector<vsg::vec2> uv;
                std::vector<uint32_t> idx;
                makeSheetGrid(L.sheet_size_, lp.altitude, 12, v, n, uv, idx);
                auto va = vsg::vec3Array::create(v.size());
                auto na = vsg::vec3Array::create(n.size());
                auto ua = vsg::vec2Array::create(uv.size());
                for (size_t i = 0; i < v.size(); ++i) {
                    (*va)[i] = v[i]; (*na)[i] = n[i]; (*ua)[i] = uv[i];
                }
                auto ia = vsg::uintArray::create(idx.size());
                for (size_t i = 0; i < idx.size(); ++i) (*ia)[i] = idx[i];
                auto vid = vsg::VertexIndexDraw::create();
                vid->assignArrays({va, na, ua});
                vid->assignIndices(ia);
                vid->indexCount = static_cast<uint32_t>(idx.size());
                vid->instanceCount = 1;
                L.sg_sheet->addChild(vid);
                L.holder->addChild(true, L.sg_sheet);
            }
        } else {
            // 积云/积雨云: P3-4b 体积云 (ray-march, 相机跟随网格壳体) 或
            // 公告板精灵 (旧行为, 低配回退)。体积云用 cloud_volume.vert 把
            // 网格顶点当稳定水平世界坐标, 每片元 ray-march 共享 3D 体积场;
            // 公告板用 cloud_sprite.vert 逐块面向相机。
            const bool volume = p_.volume;
            vsg::ref_ptr<vsg::floatArray> ub;
            L.sg_spr = buildEnvPipeline(volume ? "cloud_volume.vert" : "cloud_sprite.vert",
                                        volume ? "cloud_volume.frag" : "cloud_sprite.frag",
                                        true, true, ub);
            if (L.sg_spr) {
                L.ub_spr = ub;
                const int N = (lp.type == CloudLayerType::Cumulonimbus)
                    ? std::max(p_.cumulus_count / 2, 6) : p_.cumulus_count;
                L.blocks.resize(N);
                for (int i = 0; i < N; ++i) {
                    auto& b = L.blocks[i];
                    b.angle = hash01(static_cast<int>(layers_.size()) * 97 + i * 3) * 2.0f * static_cast<float>(kPi);
                    b.rad = p_.cumulus_radius *
                            std::sqrt(hash01(static_cast<int>(layers_.size()) * 97 + i * 3 + 1));
                    const float scale = (lp.type == CloudLayerType::Cumulonimbus) ? 2.2f : 1.0f;
                    b.size = (240.0f + 400.0f * hash01(static_cast<int>(layers_.size()) * 97 + i * 3 + 2)) * scale;
                    b.alt = lp.altitude + lp.thickness * hash01(static_cast<int>(layers_.size()) * 97 + i * 7 + 5);
                }
                if (volume) {
                    // 体积云: 相机跟随水平网格 (与 sheet 同模式), y=层底。
                    // 每个片元对应稳定水平坐标 → ray-march 共享 3D 体积场。
                    std::vector<vsg::vec3> v, n;
                    std::vector<vsg::vec2> uv;
                    std::vector<uint32_t> idx;
                    makeSheetGrid(6000.0f, lp.altitude, 1, v, n, uv, idx);
                    auto va = vsg::vec3Array::create(v.size());
                    auto na = vsg::vec3Array::create(n.size());
                    auto ua = vsg::vec2Array::create(uv.size());
                    for (size_t i = 0; i < v.size(); ++i) {
                        (*va)[i] = v[i]; (*na)[i] = n[i]; (*ua)[i] = uv[i];
                    }
                    auto ia = vsg::uintArray::create(idx.size());
                    for (size_t i = 0; i < idx.size(); ++i) (*ia)[i] = idx[i];
                    auto vid = vsg::VertexIndexDraw::create();
                    vid->assignArrays({va, na, ua});
                    vid->assignIndices(ia);
                    vid->indexCount = static_cast<uint32_t>(idx.size());
                    vid->instanceCount = 1;
                    L.sg_spr->addChild(vid);
                    L.holder->addChild(true, L.sg_spr);
                } else {
                const size_t nvert = static_cast<size_t>(N) * 4;
                L.spr_v = vsg::vec3Array::create(nvert);
                L.spr_v->properties.dataVariance = vsg::DYNAMIC_DATA;
                // 创建期即填真实块心位置: 占位 (0,-1e5,0) 会让首次 record 计算的
                // 包围球落在视锥外 → 视锥剔除永久裁掉云块 (bound 只算一次)。
                // 块心 = ENU 锚点为中心散布 (与 updateBillboards 同公式, 恒定)。
                for (int i = 0; i < N; ++i) {
                    const auto& b = L.blocks[i];
                    const float hw = b.size * 0.5f;
                    const float hh = b.size * ((lp.type == CloudLayerType::Cumulonimbus) ? 0.75f : 0.42f);
                    const vsg::vec3 ctr(std::cos(b.angle) * b.rad, b.alt, std::sin(b.angle) * b.rad);
                    vsg::vec3 dir(-ctr.x, 0.0f, -ctr.z);   // 初始朝向: 面向锚点
                    const float dl = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                    if (dl < 1e-3f) dir = vsg::vec3(0, 0, 1);
                    else dir = vsg::vec3(dir.x / dl, 0.0f, dir.z / dl);
                    const vsg::vec3 right(-dir.z, 0.0f, dir.x);
                    const vsg::vec3 up(0.0f, 1.0f, 0.0f);
                    vsg::vec3* q = &(*L.spr_v)[size_t(i) * 4];
                    q[0] = ctr - right * hw;
                    q[1] = ctr + right * hw;
                    q[2] = ctr + right * hw + up * hh;
                    q[3] = ctr - right * hw + up * hh;
                }
                // 法线/UV 借助独立数组 (布局要求 3 属性)。
                auto na = vsg::vec3Array::create(nvert);
                auto ua = vsg::vec2Array::create(nvert);
                for (size_t i = 0; i < nvert; ++i) {
                    (*na)[i] = vsg::vec3(0, 1, 0);
                    const uint32_t q = static_cast<uint32_t>(i % 4);
                    (*ua)[i] = vsg::vec2((q == 1 || q == 2) ? 1.0f : 0.0f,
                                          (q >= 2) ? 1.0f : 0.0f);
                }
                std::vector<uint32_t> idx;
                idx.reserve(static_cast<size_t>(N) * 6);
                for (int i = 0; i < N; ++i) {
                    const uint32_t b0 = i * 4;
                    idx.push_back(b0); idx.push_back(b0 + 2); idx.push_back(b0 + 1);
                    idx.push_back(b0); idx.push_back(b0 + 3); idx.push_back(b0 + 2);
                }
                auto ia = vsg::uintArray::create(idx.size());
                for (size_t i = 0; i < idx.size(); ++i) (*ia)[i] = idx[i];
                auto vid = vsg::VertexIndexDraw::create();
                vid->assignArrays({L.spr_v, na, ua});
                vid->assignIndices(ia);
                vid->indexCount = static_cast<uint32_t>(idx.size());
                vid->instanceCount = 1;
                L.sg_spr->addChild(vid);
                L.holder->addChild(true, L.sg_spr);
                }
            }
        }
        root_->addChild(true, L.holder);
        layers_.push_back(std::move(L));
    }

    // 每帧公告板跟随相机 (与一期同模式)。
    void updateBillboards(CloudLayerRT& L, const EnvFrame& f) {
        const vsg::vec3 cam(static_cast<float>(f.cam_pos_enu.x),
                            static_cast<float>(f.cam_pos_enu.y),
                            static_cast<float>(f.cam_pos_enu.z));
        for (size_t i = 0; i < L.blocks.size(); ++i) {
            const auto& b = L.blocks[i];
            // 块心以 ENU 锚点为中心散布 (不跟随相机): 双视口布局下观察口
            // (2500m 高度) 与传感器 (贴地平视) 共享同一世界散布, 各自都能
            // 在视锥内看到云块; 朝向仍面向查询帧相机 (Y 轴公告板)。
            const vsg::vec3 ctr(std::cos(b.angle) * b.rad,
                                b.alt,
                                std::sin(b.angle) * b.rad);
            vsg::vec3 dir(cam.x - ctr.x, 0.0f, cam.z - ctr.z);
            const float dl = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (dl < 1e-3f) dir = vsg::vec3(0, 0, 1);
            else dir = vsg::vec3(dir.x / dl, 0.0f, dir.z / dl);
            const vsg::vec3 right(-dir.z, 0.0f, dir.x);
            const vsg::vec3 up(0.0f, 1.0f, 0.0f);
            // 积雨云: 纵横比更高 (塔状)。
            const float ar = (L.cfg.type == CloudLayerType::Cumulonimbus) ? 0.75f : 0.42f;
            const float hw = b.size * 0.5f;
            const float hh = b.size * ar;
            vsg::vec3* q = &(*L.spr_v)[i * 4];
            q[0] = ctr - right * hw;
            q[1] = ctr + right * hw;
            q[2] = ctr + right * hw + up * hh;
            q[3] = ctr - right * hw + up * hh;
        }
        L.spr_v->dirty();
    }

    mutable std::recursive_mutex state_mutex_;
    CloudParams p_;
    float coverage_mod_ = 0.5f;
    vsg::ref_ptr<vsg::Switch> root_;
    std::vector<CloudLayerRT> layers_;
    EnvFrame frame_;      // 最近帧 (ICloudQuery 采样)
    bool enabled_ = true;
};

}  // namespace

std::unique_ptr<IEnvModule> createCloudModule(const CloudParams& p)
{
    return std::unique_ptr<IEnvModule>(new CloudModule(p));
}

}  // namespace rockyenv
