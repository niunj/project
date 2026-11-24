/// @file AtmosphereLUT.cpp —— Bruneton 预计算大气 LUT 生成 (CPU fallback)。
///
/// 实现说明:
///   - 当前版本使用 CPU 生成 LUT (简单可靠)
///   - GPU compute 版本作为优化选项保留 (需要 VSG compute pipeline)
///   - LUT 生成在初始化时一次性完成, 运行时只做查表
#include "AtmosphereLUT.h"
#include <rocky_env/AtmosphereRender.h>

#include <vsg/state/DescriptorImage.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Array3D.h>

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <array>
#include <map>
#include <mutex>
#include <stdexcept>

namespace rockyenv {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRPlanet = AtmosphereLUTConfig::PLANET_RADIUS;
constexpr float kRAtmos = kRPlanet + AtmosphereLUTConfig::ATMOSPHERE_HEIGHT;
constexpr float kLutHeight=AtmosphereLUTConfig::ATMOSPHERE_HEIGHT;
// Quadratic height encoding covers the full atmosphere while preserving LUT
// samples near the surface where density and twilight change fastest.
float lutHeight(float coordinate) {return coordinate*coordinate*kLutHeight;}
float lutCoordinate(float height) {
    return std::sqrt(std::clamp(height/kLutHeight,0.f,1.f));
}
// Pack more scattering samples around the horizon, where optical depth and
// the Earth's shadow change rapidly. This is inverted at each lookup.
float scatteringCosine(float coordinate) {
    const float x=2.f*coordinate-1.f;
    return x*(.35f+.65f*std::abs(x));
}
float scatteringCoordinate(float cosine) {
    const float x=(std::sqrt(.1225f+2.60f*std::abs(cosine))-.35f)/1.30f;
    return std::clamp(.5f+.5f*std::copysign(x,cosine),0.f,1.f);
}

/// Rayleigh 密度
float rayleighDensity(float h) {
    return std::exp(-h / AtmosphereLUTConfig::RAYLEIGH_HEIGHT);
}

/// Mie 密度
float mieDensity(float h) {
    return std::exp(-h / AtmosphereLUTConfig::MIE_HEIGHT);
}

/// Ozone density profile around the stratospheric maximum.
float ozoneDensity(float h) {
    const float d = (h - AtmosphereLUTConfig::OZONE_CENTER) / AtmosphereLUTConfig::OZONE_SIGMA;
    return std::exp(-d * d);
}

/// 射线-球体相交
float raySphereIntersect(const float origin[3], const float dir[3], float radius) {
    float b = origin[0] * dir[0] + origin[1] * dir[1] + origin[2] * dir[2];
    float c = origin[0] * origin[0] + origin[1] * origin[1] + origin[2] * origin[2] - radius * radius;
    float d = b * b - c;
    if (d < 0.0f) return -1.0f;
    d = std::sqrt(d);
    float t0 = -b - d;
    float t1 = -b + d;
    if (t0 > 0.0f) return t0;
    if (t1 > 0.0f) return t1;
    return -1.0f;
}

/// Rayleigh 相函数
float rayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * kPi)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein 相函数 (Mie)
float miePhase(float cosTheta, float g) {
    float g2 = g * g;
    return 3.0f * (1.0f - g2) * (1.0f + cosTheta * cosTheta) /
           ((8.0f * kPi) * (2.0f + g2) * std::pow(1.0f + g2 - 2.0f * g * cosTheta, 1.5f));
}

}  // namespace

AtmosphereLUT::AtmosphereLUT() = default;
AtmosphereLUT::~AtmosphereLUT() = default;

bool AtmosphereLUT::generateReference() {
    constexpr float molecular[3]={5.802e-6f,13.558e-6f,33.1e-6f};
    constexpr float aerosol[3]={3.996e-6f,3.996e-6f,3.996e-6f};
    constexpr float aerosolExtinction[3]={4.444e-6f,4.444e-6f,4.444e-6f};
    constexpr float ozone[3]={.650e-6f,1.881e-6f,.085e-6f};
    return init()&&generate(molecular,aerosol,aerosolExtinction,ozone);
}

vsg::ref_ptr<vsg::Data> referenceAtmosphereTransmittance() {
    static const auto table=[] {
        AtmosphereLUT atmosphere;
        if(!atmosphere.generateReference())throw std::runtime_error("cannot generate reference atmospheric transmission");
        return atmosphere.textures().transmittance->data;
    }();
    return table;
}

bool AtmosphereLUT::init() {
    using namespace AtmosphereLUTConfig;

    // 纹理格式必须与 CPU 数据 (vsg::vec4Array2D/3D = 4×float32, 16B/纹素) 严格
    // 一致: 若用 RGBA16F (8B/纹素), VSG Context::copy 按 data->dataSize() 原样
    // 拷贝, 2 倍超量的缓冲对 8B/纹素目标是未定义行为 → 上传失败/采样恒 0
    // (天空整片黑)。散射表包含方位/分量打包块，三表统一使用 RGBA32F。
    constexpr VkFormat kLutFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

    // 创建 transmittance 纹理 (2D: 256x64)
    textures_.transmittance = vsg::Image::create();
    // imageType 必须显式声明: VSG vsg::Image 默认 VK_IMAGE_TYPE_2D 恰好兼容,
    // 但依赖默认值会让 3D 纹理静默出错 (见下), 此处统一显式设置。
    textures_.transmittance->imageType = VK_IMAGE_TYPE_2D;
    textures_.transmittance->format = kLutFormat;
    textures_.transmittance->extent.width = TRANSMITTANCE_WIDTH;
    textures_.transmittance->extent.height = TRANSMITTANCE_HEIGHT;
    textures_.transmittance->extent.depth = 1;
    textures_.transmittance->mipLevels = 1;
    textures_.transmittance->arrayLayers = 1;
    textures_.transmittance->samples = VK_SAMPLE_COUNT_1_BIT;
    textures_.transmittance->tiling = VK_IMAGE_TILING_OPTIMAL;
    // 必须含 TRANSFER_DST_BIT: VSG 在 DescriptorImage::compile 里通过
    // vkCmdCopyBufferToImage 把 CPU 数据上传到纹理, 缺少该 usage 时上传会被
    // 跳过/校验失败, 纹理保持未初始化 —— 采样恒为 0 (天空整片纯黑),
    // 但 CPU 侧 LUT 查询一切正常, 极易误判为 LUT 数值错误。
    textures_.transmittance->usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    textures_.transmittance->data = vsg::vec4Array2D::create(TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);
    textures_.transmittance->data->properties.format = kLutFormat;

    // One 3-D image packs four azimuth slices × three spectral integrals.
    textures_.scattering = vsg::Image::create();
    // 关键: imageType 默认是 VK_IMAGE_TYPE_2D —— 2D 类型带 depth=32 创建
    // Vulkan Image 会失败/未定义, 并拖垮整个 set=1 描述符组 (连 2D transmittance
    // 都采样为 0, 天空整片黑)。sampler3D 必须 VK_IMAGE_TYPE_3D。
    textures_.scattering->imageType = VK_IMAGE_TYPE_3D;
    textures_.scattering->format = kLutFormat;
    textures_.scattering->extent.width = SCATTERING_WIDTH;
    textures_.scattering->extent.height = SCATTERING_HEIGHT;
    textures_.scattering->extent.depth = SCATTERING_PACKED_DEPTH;
    textures_.scattering->mipLevels = 1;
    textures_.scattering->arrayLayers = 1;
    textures_.scattering->samples = VK_SAMPLE_COUNT_1_BIT;
    textures_.scattering->tiling = VK_IMAGE_TILING_OPTIMAL;
    textures_.scattering->usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    textures_.scattering->data = vsg::vec4Array3D::create(SCATTERING_WIDTH, SCATTERING_HEIGHT, SCATTERING_PACKED_DEPTH);
    textures_.scattering->data->properties.format = kLutFormat;

    // 创建 irradiance 纹理 (2D: 64x16)
    textures_.irradiance = vsg::Image::create();
    textures_.irradiance->imageType = VK_IMAGE_TYPE_2D;
    textures_.irradiance->format = kLutFormat;
    textures_.irradiance->extent.width = IRRADIANCE_WIDTH;
    textures_.irradiance->extent.height = IRRADIANCE_HEIGHT;
    textures_.irradiance->extent.depth = 1;
    textures_.irradiance->mipLevels = 1;
    textures_.irradiance->arrayLayers = 1;
    textures_.irradiance->samples = VK_SAMPLE_COUNT_1_BIT;
    textures_.irradiance->tiling = VK_IMAGE_TILING_OPTIMAL;
    textures_.irradiance->usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    textures_.irradiance->data = vsg::vec4Array2D::create(IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT);
    textures_.irradiance->data->properties.format = kLutFormat;

    // 创建采样器
    sampler_ = vsg::Sampler::create();
    sampler_->magFilter = VK_FILTER_LINEAR;
    sampler_->minFilter = VK_FILTER_LINEAR;
    sampler_->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_->anisotropyEnable = VK_FALSE;

    valid_ = true;
    return true;
}

bool AtmosphereLUT::generate(const float rayleighScattering[3],
                             const float mieScattering[3],
                             const float mieExtinction[3],
                             const float ozoneAbsorption[3], unsigned scatteringOrders) {
    if(scatteringOrders<1 || scatteringOrders>4) return false;
    scatteringOrders_=scatteringOrders;
    if (!valid_) return false;
    // Share immutable CPU tables across views. Image/Vulkan objects stay local
    // to each LUT, so distinct windows/devices never share compiled handles.
    using Key=std::array<float,13>;
    struct Tables {vsg::ref_ptr<vsg::Data> transmittance,scattering,irradiance;};
    static std::mutex mutex;
    static std::map<Key,Tables> cache;
    Key key;size_t offset=0;
    for(const auto* values:{rayleighScattering,mieScattering,mieExtinction,ozoneAbsorption})
        for(int channel=0;channel<3;++channel) {
            if(!std::isfinite(values[channel]) || values[channel]<0)return false;
            key[offset++]=values[channel];
        }
    std::lock_guard<std::mutex> lock(mutex);
    key[12]=float(scatteringOrders);
    const auto found=cache.find(key);
    if(found!=cache.end()) {
        textures_.transmittance->data=found->second.transmittance;
        textures_.scattering->data=found->second.scattering;
        textures_.irradiance->data=found->second.irradiance;
        return true;
    }
    // Generation is infrequent and serialized; the bounded cache prevents a
    // sequence of edited atmospheres from retaining unbounded CPU tables.
    using namespace AtmosphereLUTConfig;
    textures_.transmittance->data=vsg::vec4Array2D::create(TRANSMITTANCE_WIDTH,TRANSMITTANCE_HEIGHT);
    textures_.scattering->data=vsg::vec4Array3D::create(SCATTERING_WIDTH,SCATTERING_HEIGHT,SCATTERING_PACKED_DEPTH);
    textures_.irradiance->data=vsg::vec4Array2D::create(IRRADIANCE_WIDTH,IRRADIANCE_HEIGHT);
    for(const auto& image:{textures_.transmittance,textures_.scattering,textures_.irradiance})image->data->properties.format=image->format;
    generateCPU(rayleighScattering, mieScattering, mieExtinction, ozoneAbsorption);
    if(cache.size()>=4)cache.erase(cache.begin());
    cache.emplace(key,Tables{textures_.transmittance->data,textures_.scattering->data,textures_.irradiance->data});
    return true;
}

void AtmosphereLUT::generateCPU(const float rayleighScattering[3],
                                const float mieScattering[3],
                                const float mieExtinction[3],
                                const float ozoneAbsorption[3]) {
    using namespace AtmosphereLUTConfig;

    // 生成顺序: transmittance → irradiance → scattering。
    // scattering 的多次散射近似 (T1) 需要查 irradiance LUT, 因此 irradiance
    // 必须先于 scattering 生成。
    // 生成 transmittance LUT
    std::vector<float> transmittanceData(TRANSMITTANCE_WIDTH * TRANSMITTANCE_HEIGHT * 4);
    computeTransmittance(transmittanceData.data(), TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT,
                         rayleighScattering, mieExtinction, ozoneAbsorption);

    // 复制到纹理数据数组
    auto transmittanceArray = textures_.transmittance->data.cast<vsg::vec4Array2D>();
    for (uint32_t y = 0; y < TRANSMITTANCE_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TRANSMITTANCE_WIDTH; ++x) {
            const uint32_t i = y * TRANSMITTANCE_WIDTH + x;
            transmittanceArray->at(x, y) = vsg::vec4(
                transmittanceData[i * 4 + 0],
                transmittanceData[i * 4 + 1],
                transmittanceData[i * 4 + 2],
                1.0f
            );
        }
    }
    transmittanceArray->dirty();

    // 生成 irradiance LUT (先于 scattering, 供多次散射项查询)
    std::vector<float> irradianceData(IRRADIANCE_WIDTH * IRRADIANCE_HEIGHT * 4);
    computeIrradiance(irradianceData.data(), IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT,
                      rayleighScattering, mieExtinction, ozoneAbsorption);

    std::vector<float> multipleData(IRRADIANCE_WIDTH*IRRADIANCE_HEIGHT*4);
    computeMultipleScattering(multipleData.data(),rayleighScattering,mieScattering,mieExtinction,ozoneAbsorption);
    for(size_t i=0;i<irradianceData.size();i+=4)
        for(int c=0;c<3;++c) irradianceData[i+c]+=3.14159265f*multipleData[i+c];
    // 复制到纹理数据数组
    auto irradianceArray = textures_.irradiance->data.cast<vsg::vec4Array2D>();
    for (uint32_t y = 0; y < IRRADIANCE_HEIGHT; ++y) {
        for (uint32_t x = 0; x < IRRADIANCE_WIDTH; ++x) {
            const uint32_t i = y * IRRADIANCE_WIDTH + x;
            irradianceArray->at(x, y) = vsg::vec4(
                irradianceData[i * 4 + 0],
                irradianceData[i * 4 + 1],
                irradianceData[i * 4 + 2],
                1.0f
            );
        }
    }
    irradianceArray->dirty();

    // 生成 scattering LUT (含多次散射近似)
    std::vector<float> scatteringData(SCATTERING_WIDTH * SCATTERING_HEIGHT * SCATTERING_PACKED_DEPTH * 4);
    computeScattering(scatteringData.data(), SCATTERING_WIDTH, SCATTERING_HEIGHT, SCATTERING_DEPTH,
                      rayleighScattering, mieScattering, mieExtinction, ozoneAbsorption,
                      multipleData.data());

    // 复制到纹理数据数组
    auto scatteringArray = textures_.scattering->data.cast<vsg::vec4Array3D>();
    for (uint32_t z = 0; z < SCATTERING_PACKED_DEPTH; ++z) {
        for (uint32_t y = 0; y < SCATTERING_HEIGHT; ++y) {
            for (uint32_t x = 0; x < SCATTERING_WIDTH; ++x) {
                const uint32_t i = (z * SCATTERING_HEIGHT + y) * SCATTERING_WIDTH + x;
                scatteringArray->at(x, y, z) = vsg::vec4(
                    scatteringData[i * 4 + 0],
                    scatteringData[i * 4 + 1],
                    scatteringData[i * 4 + 2],
                    scatteringData[i * 4 + 3]
                );
            }
        }
    }
    scatteringArray->dirty();
}

void AtmosphereLUT::computeTransmittance(float* out, uint32_t width, uint32_t height,
                                         const float rayleighScattering[3],
                                         const float mieExtinction[3],
                                         const float ozoneAbsorption[3]) {
    using namespace AtmosphereLUTConfig;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            // Height uses the same quadratic 0..100 km mapping as the shader.
            float viewHeight = lutHeight(static_cast<float>(y) / static_cast<float>(height - 1));
            float sunCosZenith = static_cast<float>(x) / static_cast<float>(width - 1) * 2.0f - 1.0f;

            // 相机位置
            float cameraPos[3] = {0.0f, kRPlanet + viewHeight, 0.0f};
            float sinSunZenith = std::sqrt(std::max(0.0f, 1.0f - sunCosZenith * sunCosZenith));
            float sunDir[3] = {0.0f, sunCosZenith, sinSunZenith};

            // 射线-大气相交
            float b = cameraPos[1] * sunDir[1];
            float c = cameraPos[1] * cameraPos[1] - kRAtmos * kRAtmos;
            float d = b * b - c;
            if (d < 0.0f) {
                // 不穿过大气
                out[(y * width + x) * 4 + 0] = 0.0f;
                out[(y * width + x) * 4 + 1] = 0.0f;
                out[(y * width + x) * 4 + 2] = 0.0f;
                out[(y * width + x) * 4 + 3] = 1.0f;
                continue;
            }
            d = std::sqrt(d);
            float tFar = -b + d;

            // 检查是否碰到地面
            float groundHit = raySphereIntersect(cameraPos, sunDir, kRPlanet);
            if (groundHit > 0.0f) {
                out[(y * width + x) * 4 + 0] = 0.0f;
                out[(y * width + x) * 4 + 1] = 0.0f;
                out[(y * width + x) * 4 + 2] = 0.0f;
                out[(y * width + x) * 4 + 3] = 1.0f;
                continue;
            }

            // 数值积分
            const int SAMPLES = 64;
            float stepLen = tFar / static_cast<float>(SAMPLES);
            float opticalDepth[3] = {0.0f, 0.0f, 0.0f};

            for (int i = 0; i < SAMPLES; ++i) {
                float t = (static_cast<float>(i) + 0.5f) * stepLen;
                float h = std::sqrt(cameraPos[0] * cameraPos[0] +
                                    (cameraPos[1] + sunDir[1] * t) * (cameraPos[1] + sunDir[1] * t) +
                                    (sunDir[2] * t) * (sunDir[2] * t)) - kRPlanet;

                float rhoR = rayleighDensity(h);
                float rhoM = mieDensity(h);
                float rhoO3 = ozoneDensity(h);

                opticalDepth[0] += (rayleighScattering[0] * rhoR + mieExtinction[0] * rhoM + ozoneAbsorption[0] * rhoO3) * stepLen;
                opticalDepth[1] += (rayleighScattering[1] * rhoR + mieExtinction[1] * rhoM + ozoneAbsorption[1] * rhoO3) * stepLen;
                opticalDepth[2] += (rayleighScattering[2] * rhoR + mieExtinction[2] * rhoM + ozoneAbsorption[2] * rhoO3) * stepLen;
            }

            // 透过率 = exp(-光学深度)
            out[(y * width + x) * 4 + 0] = std::exp(-opticalDepth[0]);
            out[(y * width + x) * 4 + 1] = std::exp(-opticalDepth[1]);
            out[(y * width + x) * 4 + 2] = std::exp(-opticalDepth[2]);
            out[(y * width + x) * 4 + 3] = 1.0f;
        }
    }
}

// Successive isotropic angular moments. The first source is direct solar
// scattering; each subsequent source transports the previous order's mean
// radiance through the spherical atmosphere. Store orders 1..3 for the final
// view integration, producing scattering orders 2..4 without a fitted gain.
void AtmosphereLUT::computeMultipleScattering(float* out,const float betaR[3],
    const float betaM[3],const float extinctionM[3],const float ozone[3]) {
    using namespace AtmosphereLUTConfig;
    constexpr int W=IRRADIANCE_WIDTH,H=IRRADIANCE_HEIGHT;
    constexpr int zeniths=8,azimuths=8,steps=16;
    constexpr double pi=3.141592653589793;
    std::vector<vsg::vec3> previous(W*H),next(W*H),sum(W*H);
    const auto sample=[&](const vsg::dvec3& point,const vsg::dvec3& sun) {
        const double h=std::max(0.,vsg::length(point)-kRPlanet);
        double x=std::clamp((vsg::dot(vsg::normalize(point),sun)+1.)*.5,0.,1.)*(W-1);
        double y=lutCoordinate(float(h))*(H-1);
        int i=int(x),j=int(y);
        vsg::vec3 result;
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx)
            result+=previous[std::min(j+dy,H-1)*W+std::min(i+dx,W-1)]*
                float((dx?x-i:1-(x-i))*(dy?y-j:1-(y-j)));
        return result;
    };
    for(unsigned order=1;order<scatteringOrders_;++order) {
        std::fill(next.begin(),next.end(),vsg::vec3{});
        for(int j=0;j<H;++j) for(int i=0;i<W;++i) {
            double h=lutHeight(float(j)/float(H-1)),muSun=2.*i/(W-1)-1.;
            vsg::dvec3 origin(0,kRPlanet+h,0),sun(0,muSun,std::sqrt(std::max(0.,1-muSun*muSun)));
            for(int z=0;z<zeniths;++z) for(int az=0;az<azimuths;++az) {
                const double mu=2.*(z+.5)/zeniths-1.,phi=2*pi*(az+.5)/azimuths;
                vsg::dvec3 ray(std::sqrt(1-mu*mu)*std::cos(phi),mu,std::sqrt(1-mu*mu)*std::sin(phi));
                if(h==0 && mu<0) continue;
                const double b=vsg::dot(origin,ray);
                double distance=-b+std::sqrt(b*b+kRAtmos*kRAtmos-vsg::dot(origin,origin));
                const double ground=b*b+kRPlanet*kRPlanet-vsg::dot(origin,origin);
                if(ground>=0 && -b-std::sqrt(ground)>0) distance=std::min(distance,-b-std::sqrt(ground));
                double ds=distance/steps;
                vsg::vec3 trans(1,1,1),radiance;
                for(int k=0;k<steps;++k) {
                    const auto point=origin+ray*((k+.5)*ds);
                    const double radius=vsg::length(point);
                    const float height=std::max(0.f,float(radius-kRPlanet));
                    float r=rayleighDensity(height),m=mieDensity(height),o=ozoneDensity(height);
                    vsg::vec3 sigmaS(betaR[0]*r+betaM[0]*m,betaR[1]*r+betaM[1]*m,betaR[2]*r+betaM[2]*m);
                    vsg::vec3 ext(betaR[0]*r+extinctionM[0]*m+ozone[0]*o,
                                  betaR[1]*r+extinctionM[1]*m+ozone[1]*o,
                                  betaR[2]*r+extinctionM[2]*m+ozone[2]*o);
                    vsg::vec3 source;
                    if(order==1) {
                        const double cosine=vsg::dot(vsg::normalize(point),sun);
                        const double sunB=vsg::dot(point,sun);
                        if(!(sunB<0 && sunB*sunB+kRPlanet*kRPlanet-radius*radius>0)) {
                            const auto solar=sampleTransmittanceCPU(height,float(cosine));
                            const float nu=vsg::dot(ray,sun),g=.76f;
                            const float pr=3.f*(1+nu*nu)/float(16*pi);
                            const float pm=(1-g*g)/(float(4*pi)*std::pow(1+g*g-2*g*nu,1.5f));
                            source=vsg::vec3(betaR[0]*r*pr+betaM[0]*m*pm,
                                             betaR[1]*r*pr+betaM[1]*m*pm,
                                             betaR[2]*r*pr+betaM[2]*m*pm)*solar;
                        }
                    } else source=sigmaS*sample(point,sun);
                    for(int c=0;c<3;++c) {
                        const float segment=std::exp(-ext[c]*ds);
                        radiance[c]+=trans[c]*source[c]*(ext[c]>0 ? (1-segment)/ext[c] : float(ds));
                        trans[c]*=segment;
                    }
                }
                next[j*W+i]+=radiance/float(zeniths*azimuths);
            }
        }
        for(size_t i=0;i<sum.size();++i) sum[i]+=next[i];
        previous.swap(next);
    }
    for(size_t i=0;i<sum.size();++i) {
        out[4*i]=sum[i].x;out[4*i+1]=sum[i].y;out[4*i+2]=sum[i].z;out[4*i+3]=1;
    }
}

void AtmosphereLUT::computeScattering(float* out, uint32_t width, uint32_t height, uint32_t depth,
                                      const float rayleighScattering[3],
                                      const float mieScattering[3],
                                      const float mieExtinction[3],
                                      const float ozoneAbsorption[3],
                                      const float* irradianceData) {
    using namespace AtmosphereLUTConfig;

    // The source table contains the sum of angular-mean radiance from
    // previous scattering orders, not the direct solar irradiance.
    constexpr float kMultipleScatteringFactor = 1.0f;
    auto sampleAngularMean = [&](float h, float sunCos) {
        const float fy = lutCoordinate(h) * (IRRADIANCE_HEIGHT - 1);
        const float fx = std::min(std::max((sunCos + 1.0f) * 0.5f, 0.0f), 1.0f) * (IRRADIANCE_WIDTH - 1);
        const uint32_t y0 = static_cast<uint32_t>(fy), y1 = std::min(y0 + 1, IRRADIANCE_HEIGHT - 1);
        const uint32_t x0 = static_cast<uint32_t>(fx), x1 = std::min(x0 + 1, IRRADIANCE_WIDTH - 1);
        const float ty = fy - static_cast<float>(y0), tx = fx - static_cast<float>(x0);
        float e[3] = {0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 3; ++k) {
            const float a = irradianceData[(y0 * IRRADIANCE_WIDTH + x0) * 4 + k];
            const float b = irradianceData[(y0 * IRRADIANCE_WIDTH + x1) * 4 + k];
            const float c = irradianceData[(y1 * IRRADIANCE_WIDTH + x0) * 4 + k];
            const float d = irradianceData[(y1 * IRRADIANCE_WIDTH + x1) * 4 + k];
            const float top = a + (b - a) * tx;
            const float bot = c + (d - c) * tx;
            e[k] = top + (bot - top) * ty;
        }
        return vsg::vec3(e[0], e[1], e[2]);
    };

    // Four geometrical azimuth slices bound CPU startup cost. The narrow Mie
    // lobe is NOT discretized: phase functions are evaluated at lookup time.
    // Every slice stores three RGB integrals in distinct altitude blocks;
    // lookups clamp within the block to avoid filtering between components.
    for(uint32_t azimuth=0;azimuth<SCATTERING_AZIMUTHS;++azimuth) {
    const float azimuthCos=2.f*float(azimuth)/float(SCATTERING_AZIMUTHS-1)-1.f;
    auto index=[&](uint32_t component,uint32_t x,uint32_t y,uint32_t z){return (((azimuth*SCATTERING_COMPONENTS+component)*depth+z)*height*width+y*width+x)*4;};
    for (uint32_t z = 0; z < depth; ++z) {
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                // 参数映射
                float viewHeight = lutHeight(static_cast<float>(z) / static_cast<float>(depth - 1));
                float viewCosZenith = scatteringCosine(float(y)/float(height-1));
                float sunCosZenith = scatteringCosine(float(x)/float(width-1));

                // 相机位置
                float cameraPos[3] = {0.0f, kRPlanet + viewHeight, 0.0f};
                float sinViewZenith = std::sqrt(std::max(0.0f, 1.0f - viewCosZenith * viewCosZenith));
                float viewDir[3] = {0.0f, viewCosZenith, sinViewZenith};

                // 太阳方向
                float sinSunZenith = std::sqrt(std::max(0.0f, 1.0f - sunCosZenith * sunCosZenith));
                float sunDir[3] = {sinSunZenith*std::sqrt(std::max(0.f,1.f-azimuthCos*azimuthCos)), sunCosZenith, sinSunZenith*azimuthCos};

                // h=0 切片向下视线特判: 相机位于行星表面, 射线立即入地, 大气
                // 路径长度为 0, 散射贡献为 0。不特判时 raySphereIntersect 因
                // c=0、近交点 t0=0 非正而退回对跖点远交点 (t1≈2R), 积分穿入
                // 行星内部 → 密度 exp(+h/8.5km) 溢出 → NaN 纹素, 并经 GPU
                // 三线性过滤 (w 轴混合 z=0 切片) 毒化地面附近全部采样。
                if (viewHeight <= 0.0f && viewCosZenith < 0.0f) {
                    for(uint32_t component=0;component<SCATTERING_COMPONENTS;++component)for(int channel=0;channel<4;++channel)out[index(component,x,y,z)+channel]=0.f;
                    continue;
                }

                // 射线-大气相交
                float b = cameraPos[1] * viewDir[1];
                float c = cameraPos[1] * cameraPos[1] - kRAtmos * kRAtmos;
                float disc = b * b - c;
                if (disc < 0.0f) {
                    // 不穿过大气
                    for(uint32_t component=0;component<SCATTERING_COMPONENTS;++component)for(int channel=0;channel<4;++channel)out[index(component,x,y,z)+channel]=0.f;
                    continue;
                }
                disc = std::sqrt(disc);
                float tNear = std::max(-b - disc, 0.0f);
                float tFar = -b + disc;

                // 检查是否碰到地面
                float groundHit = raySphereIntersect(cameraPos, viewDir, kRPlanet);
                if (groundHit > 0.0f) tFar = groundHit;

                // 数值积分
                // Long horizon paths still contain a dense 1.2 km Mie layer
                // next to the observer. Uniform 8-step integration sampled
                // above it entirely. Cosine spacing resolves both atmospheric
                // entry and ground-adjacent ends without changing the physics.
                const int VIEW_SAMPLES = 24;

                float rayleighSum[3] = {0.0f, 0.0f, 0.0f};
                float mieSum[3] = {0.0f, 0.0f, 0.0f};
                float msSum[3] = {0.0f, 0.0f, 0.0f};
                float viewOpticalDepth[3] = {0.0f, 0.0f, 0.0f};

                for (int i = 0; i < VIEW_SAMPLES; ++i) {
                    float s0=.5f*(1.f-std::cos(kPi*float(i)/float(VIEW_SAMPLES)));
                    float s1=.5f*(1.f-std::cos(kPi*float(i+1)/float(VIEW_SAMPLES)));
                    float viewStepLen=(s1-s0)*(tFar-tNear);
                    float t=tNear+.5f*(s0+s1)*(tFar-tNear);
                    float h = std::sqrt(cameraPos[0] * cameraPos[0] +
                                        (cameraPos[1] + viewDir[1] * t) * (cameraPos[1] + viewDir[1] * t) +
                                        (viewDir[2] * t) * (viewDir[2] * t)) - kRPlanet;

                    float rhoR = rayleighDensity(h);
                    float rhoM = mieDensity(h);
                    float rhoO3 = ozoneDensity(h);

                    float localExtR = rayleighScattering[0] * rhoR + mieExtinction[0] * rhoM + ozoneAbsorption[0] * rhoO3;
                    float localExtG = rayleighScattering[1] * rhoR + mieExtinction[1] * rhoM + ozoneAbsorption[1] * rhoO3;
                    float localExtB = rayleighScattering[2] * rhoR + mieExtinction[2] * rhoM + ozoneAbsorption[2] * rhoO3;

                    float viewTransR = std::exp(-viewOpticalDepth[0]-.5f*localExtR*viewStepLen);
                    float viewTransG = std::exp(-viewOpticalDepth[1]-.5f*localExtG*viewStepLen);
                    float viewTransB = std::exp(-viewOpticalDepth[2]-.5f*localExtB*viewStepLen);
                    viewOpticalDepth[0] += localExtR * viewStepLen;
                    viewOpticalDepth[1] += localExtG * viewStepLen;
                    viewOpticalDepth[2] += localExtB * viewStepLen;

                    // 光源方向光学深度
                    float p[3] = {
                        cameraPos[0] + viewDir[0] * t,
                        cameraPos[1] + viewDir[1] * t,
                        cameraPos[2] + viewDir[2] * t
                    };

                    float sunB = p[1]*sunDir[1]+p[2]*sunDir[2];
                    float pointRadius2=p[0]*p[0]+p[1]*p[1]+p[2]*p[2];
                    bool blocked=sunB<0 && sunB*sunB+kRPlanet*kRPlanet-pointRadius2>0;
                    // This very same optical table drives the sensor queries.
                    // Reuse it instead of a separate four-point solar integral
                    // which skipped most near-ground Mie extinction as well.
                    const auto sunTrans=blocked?vsg::vec3{}:sampleTransmittanceCPU(h,sunB/std::sqrt(pointRadius2));
                    const float sunTransR=sunTrans.x,sunTransG=sunTrans.y,sunTransB=sunTrans.z;

                    rayleighSum[0] += viewTransR * sunTransR * rhoR * viewStepLen;
                    rayleighSum[1] += viewTransG * sunTransG * rhoR * viewStepLen;
                    rayleighSum[2] += viewTransB * sunTransB * rhoR * viewStepLen;

                    mieSum[0] += viewTransR * sunTransR * rhoM * viewStepLen;
                    mieSum[1] += viewTransG * sunTransG * rhoM * viewStepLen;
                    mieSum[2] += viewTransB * sunTransB * rhoM * viewStepLen;

                    // Isotropic higher-order source, scattering coefficient times angular mean radiance.
                    const vsg::vec3 eMs = sampleAngularMean(h, (p[1]*sunDir[1]+p[2]*sunDir[2])/std::sqrt(pointRadius2));
                    msSum[0] += viewTransR * (rayleighScattering[0] * rhoR + mieScattering[0] * rhoM)
                                * eMs.x * viewStepLen;
                    msSum[1] += viewTransG * (rayleighScattering[1] * rhoR + mieScattering[1] * rhoM)
                                * eMs.y * viewStepLen;
                    msSum[2] += viewTransB * (rayleighScattering[2] * rhoR + mieScattering[2] * rhoM)
                                * eMs.z * viewStepLen;
                }

                // Separate angular-free RGB integrals. Retain spectral
                // extinction in each, instead of estimating a grey Mie ratio.
                for(int channel=0;channel<3;++channel) {
                    out[index(0,x,y,z)+channel]=rayleighScattering[channel]*rayleighSum[channel];
                    out[index(1,x,y,z)+channel]=mieScattering[channel]*mieSum[channel];
                    out[index(2,x,y,z)+channel]=kMultipleScatteringFactor*msSum[channel];
                }
                for(uint32_t component=0;component<SCATTERING_COMPONENTS;++component)out[index(component,x,y,z)+3]=0.f;
            }
        }
    }
    }
}

void AtmosphereLUT::computeIrradiance(float* out, uint32_t width, uint32_t height,
                                      const float rayleighScattering[3],
                                      const float mieExtinction[3],
                                      const float ozoneAbsorption[3]) {
    using namespace AtmosphereLUTConfig;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            // 参数映射
            float viewHeight = lutHeight(static_cast<float>(y) / static_cast<float>(height - 1));
            float sunCosZenith = static_cast<float>(x) / static_cast<float>(width - 1) * 2.0f - 1.0f;

            // 相机位置
            float cameraPos[3] = {0.0f, kRPlanet + viewHeight, 0.0f};
            float sinSunZenith = std::sqrt(std::max(0.0f, 1.0f - sunCosZenith * sunCosZenith));
            float sunDir[3] = {0.0f, sunCosZenith, sinSunZenith};

            float irradiance[3] = {0.0f, 0.0f, 0.0f};

            // 上半球积分 (θ ∈ [0, π/2]: 天顶→地平线; 下半球射线不入射水平面,
            // 且夜间会因 cosThetaSun>0 虚增 irradiance —— 夜空亮空 bug 根因)。
            const int SAMPLES = 32;
            const float kHalfPi = kPi * 0.5f;
            for (int i = 0; i < SAMPLES; ++i) {
                float theta = (static_cast<float>(i) + 0.5f) * kHalfPi / static_cast<float>(SAMPLES);
                float dTheta = kHalfPi / static_cast<float>(SAMPLES);

                float sampleDir[3] = {0.0f, std::cos(theta), std::sin(theta)};
                float cosThetaSun = sampleDir[1] * sunDir[1] + sampleDir[2] * sunDir[2];

                // 射线-大气相交
                float b = cameraPos[1] * sampleDir[1];
                float c = cameraPos[1] * cameraPos[1] - kRAtmos * kRAtmos;
                float disc = b * b - c;
                if (disc < 0.0f) continue;
                disc = std::sqrt(disc);
                float tNear = std::max(-b - disc, 0.0f);
                float tFar = -b + disc;

                // 检查是否碰到地面: 撞地射线的天空贡献置零 (与 computeTransmittance
                // 的撞地 T=0 语义一致; 只截断距离会让掠射/向下载荷带 T≈1 混入)。
                float groundHit = raySphereIntersect(cameraPos, sampleDir, kRPlanet);
                if (groundHit > 0.0f) continue;

                // 计算 transmittance
                const int TRANS_SAMPLES = 16;
                float transStepLen = (tFar - tNear) / static_cast<float>(TRANS_SAMPLES);
                float transOD[3] = {0.0f, 0.0f, 0.0f};

                for (int j = 0; j < TRANS_SAMPLES; ++j) {
                    float t = tNear + (static_cast<float>(j) + 0.5f) * transStepLen;
                    float h = std::sqrt(cameraPos[0] * cameraPos[0] +
                                        (cameraPos[1] + sampleDir[1] * t) * (cameraPos[1] + sampleDir[1] * t) +
                                        (sampleDir[2] * t) * (sampleDir[2] * t)) - kRPlanet;

                    float rhoR = rayleighDensity(h);
                    float rhoM = mieDensity(h);
                    float rhoO3 = ozoneDensity(h);

                    transOD[0] += (rayleighScattering[0] * rhoR + mieExtinction[0] * rhoM + ozoneAbsorption[0] * rhoO3) * transStepLen;
                    transOD[1] += (rayleighScattering[1] * rhoR + mieExtinction[1] * rhoM + ozoneAbsorption[1] * rhoO3) * transStepLen;
                    transOD[2] += (rayleighScattering[2] * rhoR + mieExtinction[2] * rhoM + ozoneAbsorption[2] * rhoO3) * transStepLen;
                }

                float transR = std::exp(-transOD[0]);
                float transG = std::exp(-transOD[1]);
                float transB = std::exp(-transOD[2]);

                // 添加贡献 (Lambert cosine)
                float cosTheta = std::max(cosThetaSun, 0.0f);
                irradiance[0] += transR * cosTheta * std::sin(theta) * dTheta;
                irradiance[1] += transG * cosTheta * std::sin(theta) * dTheta;
                irradiance[2] += transB * cosTheta * std::sin(theta) * dTheta;
            }

            // 写入结果
            uint32_t idx = (y * width + x) * 4;
            out[idx + 0] = irradiance[0];
            out[idx + 1] = irradiance[1];
            out[idx + 2] = irradiance[2];
            out[idx + 3] = 1.0f;
        }
    }
}

// ---- CPU 采样 (IAtmosphereQuery / 仿真侧) ----
// 归一化与 sky_dome_lut.frag 中 sampleTransmittance/sampleIrradiance 一致:
//   u = (sunCosZenith + 1) / 2,  v = sqrt(height / 100000).
vsg::vec3 AtmosphereLUT::sampleTransmittanceCPU(float heightM, float sunCosZenith) const
{
    using namespace AtmosphereLUTConfig;
    if (!valid_ || !textures_.transmittance) return vsg::vec3(1.0f, 1.0f, 1.0f);
    auto arr = textures_.transmittance->data.cast<vsg::vec4Array2D>();
    if (!arr) return vsg::vec3(1.0f, 1.0f, 1.0f);

    // Same quadratic height coordinate as sky_dome_lut.frag.
    auto clamp01 = [](float t) { return std::min(std::max(t, 0.0f), 1.0f); };
    const float fy = lutCoordinate(heightM) * (TRANSMITTANCE_HEIGHT - 1);
    const float fx = clamp01((sunCosZenith + 1.0f) * 0.5f) * (TRANSMITTANCE_WIDTH - 1);
    const uint32_t y0 = static_cast<uint32_t>(fy), y1 = std::min(y0 + 1, TRANSMITTANCE_HEIGHT - 1);
    const uint32_t x0 = static_cast<uint32_t>(fx), x1 = std::min(x0 + 1, TRANSMITTANCE_WIDTH - 1);
    const float ty = fy - static_cast<float>(y0), tx = fx - static_cast<float>(x0);

    const vsg::vec4 a = arr->at(x0, y0);
    const vsg::vec4 b = arr->at(x1, y0);
    const vsg::vec4 c = arr->at(x0, y1);
    const vsg::vec4 d = arr->at(x1, y1);
    vsg::vec3 out;
    for (int k = 0; k < 3; ++k) {
        const float top = a[k] + (b[k] - a[k]) * tx;
        const float bot = c[k] + (d[k] - c[k]) * tx;
        out[k] = top + (bot - top) * ty;
    }
    return out;
}

vsg::vec3 AtmosphereLUT::sampleIrradianceCPU(float heightM, float sunCosZenith) const
{
    using namespace AtmosphereLUTConfig;
    if (!valid_ || !textures_.irradiance) return vsg::vec3(0.0f, 0.0f, 0.0f);
    auto arr = textures_.irradiance->data.cast<vsg::vec4Array2D>();
    if (!arr) return vsg::vec3(0.0f, 0.0f, 0.0f);

    auto clamp01 = [](float t) { return std::min(std::max(t, 0.0f), 1.0f); };
    const float fy = lutCoordinate(heightM) * (IRRADIANCE_HEIGHT - 1);
    const float fx = clamp01((sunCosZenith + 1.0f) * 0.5f) * (IRRADIANCE_WIDTH - 1);
    const uint32_t y0 = static_cast<uint32_t>(fy), y1 = std::min(y0 + 1, IRRADIANCE_HEIGHT - 1);
    const uint32_t x0 = static_cast<uint32_t>(fx), x1 = std::min(x0 + 1, IRRADIANCE_WIDTH - 1);
    const float ty = fy - static_cast<float>(y0), tx = fx - static_cast<float>(x0);

    const vsg::vec4 a = arr->at(x0, y0);
    const vsg::vec4 b = arr->at(x1, y0);
    const vsg::vec4 c = arr->at(x0, y1);
    const vsg::vec4 d = arr->at(x1, y1);
    vsg::vec3 out;
    for (int k = 0; k < 3; ++k) {
        const float top = a[k] + (b[k] - a[k]) * tx;
        const float bot = c[k] + (d[k] - c[k]) * tx;
        out[k] = top + (bot - top) * ty;
    }
    return out;
}

}  // namespace rockyenv

namespace rockyenv {
vsg::vec3 AtmosphereLUT::sampleScatteringCPU(float heightM,float viewCos,float sunCos,float viewSunCos)const {
    using namespace AtmosphereLUTConfig;
    if(!valid_||!std::isfinite(heightM)||!std::isfinite(viewCos)||!std::isfinite(sunCos)||!std::isfinite(viewSunCos))return {};
    const auto data=textures_.scattering->data.cast<vsg::vec4Array3D>();if(!data)return {};
    viewCos=std::clamp(viewCos,-1.f,1.f);sunCos=std::clamp(sunCos,-1.f,1.f);
    const float horizontal=std::sqrt(std::max(0.f,(1-viewCos*viewCos)*(1-sunCos*sunCos)));
    const float relative=horizontal>1e-6f?std::clamp((viewSunCos-viewCos*sunCos)/horizontal,-1.f,1.f):0.f;
    const float nu=viewCos*sunCos+horizontal*relative;
    const float az=(relative+1)*.5f*(SCATTERING_AZIMUTHS-1);
    const uint32_t a0=uint32_t(az),a1=std::min(a0+1,SCATTERING_AZIMUTHS-1);const float at=az-a0;
    const float fx=scatteringCoordinate(sunCos)*(SCATTERING_WIDTH-1),fy=scatteringCoordinate(viewCos)*(SCATTERING_HEIGHT-1),fz=lutCoordinate(heightM)*(SCATTERING_DEPTH-1);
    const uint32_t x0=uint32_t(fx),y0=uint32_t(fy),z0=uint32_t(fz);
    auto sample=[&](uint32_t a,uint32_t component){vsg::vec3 result{};for(uint32_t z=0;z<2;++z)for(uint32_t y=0;y<2;++y)for(uint32_t x=0;x<2;++x){const auto value=data->at(std::min(x0+x,SCATTERING_WIDTH-1),std::min(y0+y,SCATTERING_HEIGHT-1),(a*SCATTERING_COMPONENTS+component)*SCATTERING_DEPTH+std::min(z0+z,SCATTERING_DEPTH-1));const float weight=(x?fx-x0:1-(fx-x0))*(y?fy-y0:1-(fy-y0))*(z?fz-z0:1-(fz-z0));result+=vsg::vec3(value.x,value.y,value.z)*weight;}return result;};
    auto component=[&](uint32_t c){return sample(a0,c)*(1-at)+sample(a1,c)*at;};
    return component(0)*rayleighPhase(nu)+component(1)*miePhase(nu,.76f)+component(2);
}
}
