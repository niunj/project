/// @file EnvStack.cpp —— 环境聚合栈 (sky + cloud + ocean) 实现。
///
/// 把三个模块的创建/挂载/每帧喂数据与"单一真值源"传播 (sky 的星历太阳与
/// AtmosphereState 先算, 再覆写 EnvFrame 喂给 cloud/ocean) 固化在模块内部,
/// 宿主 (常规 VSG 平面应用 / rocky 地球工作流) 只需 worldRoot/contentRoot
/// 两个挂载点 + 一个 onFrame 调用。
#include <rocky_env/EnvApi.h>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

#include <cstdio>
#include <cmath>

namespace rockyenv {

namespace {

class EnvStack final : public IEnvStack {
public:
    explicit EnvStack(const EnvironmentStackParams& p)
        : sky_drives_sun_(p.sky_drives_sun),
          sky_drives_lighting_(p.sky_drives_lighting) {
        world_root_   = vsg::Group::create();
        content_root_ = vsg::Group::create();

        if (p.enable_sky) {
            sky_ = createSkyModule(p.sky);
            mount(sky_.get());
        }
        if (p.enable_cloud) {
            cloud_ = createCloudModule(p.cloud);
            mount(cloud_.get());
        }
        if (p.enable_ocean) {
            ocean_ = createOceanModule(p.ocean);
            mount(ocean_.get());
        }
        std::fprintf(stderr,
                     "[rocky_env] EnvStack: sky=%d cloud=%d ocean=%d "
                     "(sun=%d lighting=%d propagation)\n",
                     sky_ ? 1 : 0, cloud_ ? 1 : 0, ocean_ ? 1 : 0,
                     p.sky_drives_sun ? 1 : 0, p.sky_drives_lighting ? 1 : 0);
    }

    vsg::Node* worldRoot() override { return world_root_.get(); }
    vsg::Node* contentRoot() override { return content_root_.get(); }

    void onFrame(EnvFrame& f) override {
        // ---- 1. sky 先跑: 产出星历太阳与 AtmosphereState ----
        if (sky_) {
            sky_->onFrame(f);

            if (sky_drives_sun_) {
                if (auto* sp = dynamic_cast<ISunProvider*>(sky_.get())) {
                    EnvSunInfo si;
                    if (sp->sunInfo(f.model, si)) {
                        f.sun_dir_enu = si.dirENU;
                        f.sun_el_deg  = si.elevationDeg;
                    }
                }
            }
            if (sky_drives_lighting_) {
                if (auto* lp = dynamic_cast<ILightingProvider*>(sky_.get())) {
                    AtmosphereState st;
                    if (lp->getAtmosphereState(st)) {
                        f.sun_light     = st.sun_light;
                        f.is_day        = st.is_day;
                        f.ambient_light = st.ambient_light;
                        f.horizon_color = st.horizon_color;
                        f.zenith_color  = st.zenith_color;
                        f.extinction_coeff = st.extinction_coeff;
                        // 月亮光照真值 (海面月光反射带/云月面照明消费);
                        // 月亮在地平线下 → 方向指下, shader 侧无月光。
                        f.moon_dir_enu = st.moon_el_deg > 0.0f
                                       ? st.moon_dir_enu : vsg::vec3(0.0f, -1.0f, 0.0f);
                        f.moon_phase   = st.moon_phase;
                    }
                }
            }
        }

        // ---- 2. cloud / ocean 消费覆写后的真值 ----
        if (cloud_) cloud_->onFrame(f);
        if (ocean_) ocean_->onFrame(f);
    }

    IEnvModule* module(const char* type) override {
        if (!type) return nullptr;
        if (std::string(type) == "sky")   return sky_.get();
        if (std::string(type) == "cloud") return cloud_.get();
        if (std::string(type) == "ocean") return ocean_.get();
        return nullptr;
    }

    // setWindow 由 IEnvStack 默认实现经 modules() 转发到各模块
    // (ocean wave_mode=2 消费以构建 GPU FFT; sky/cloud 基类空实现忽略)。

protected:
    std::vector<IEnvModule*> modules() override {
        std::vector<IEnvModule*> out;
        if (sky_)   out.push_back(sky_.get());
        if (cloud_) out.push_back(cloud_.get());
        if (ocean_) out.push_back(ocean_.get());
        return out;
    }

private:
    // 按 mountInWorldRoot 分流挂载; 模块所有权归栈。
    void mount(IEnvModule* m) {
        if (!m) return;
        auto* root = m->rootNode();
        if (!root) return;
        if (m->mountInWorldRoot()) world_root_->addChild(vsg::ref_ptr<vsg::Node>(root));
        else                        content_root_->addChild(vsg::ref_ptr<vsg::Node>(root));
    }

    std::unique_ptr<IEnvModule> sky_;
    std::unique_ptr<IEnvModule> cloud_;
    std::unique_ptr<IEnvModule> ocean_;
    bool sky_drives_sun_ = true;
    bool sky_drives_lighting_ = true;
    vsg::ref_ptr<vsg::Group> world_root_;
    vsg::ref_ptr<vsg::Group> content_root_;
};

}  // namespace

std::unique_ptr<IEnvStack> createEnvironmentStack(const EnvironmentStackParams& p)
{
    return std::unique_ptr<IEnvStack>(new EnvStack(p));
}

}  // namespace rockyenv
