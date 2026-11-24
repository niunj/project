#include "StarTexture.h"
#include <vsg/core/Array2D.h>
#include <algorithm>
#include <cmath>

namespace rockyenv {
namespace {
vsg::vec3 stellarTint(float colorIndex) {
    // Ballesteros B-V estimate. These spectral samples are divided by the
    // equivalent daylight white before the unresolved points are rendered.
    const float bv=.92f*std::clamp(colorIndex,-.32f,2.20f);
    const float temperature=4600.f*(1.f/(bv+1.70f)+1.f/(bv+.62f));
    const auto channel=[=](float wavelengthNm) {
        constexpr float planckNmKelvin=1.4387769e7f;
        return std::expm1(planckNmKelvin/(wavelengthNm*6500.f))/
               std::expm1(planckNmKelvin/(wavelengthNm*temperature));
    };
    const vsg::vec3 color(channel(610.f),channel(550.f),channel(455.f));
    return vsg::vec3(.57f,.57f,.57f)+color*(.43f/std::max({color.r,color.g,color.b}));
}
}
vsg::ref_ptr<vsg::DescriptorImage> starTexture(uint32_t binding) {
    static const auto atlas=[] {
        struct Star { uint16_t ra; int16_t dec, magnitude, color; };
        static constexpr Star stars[] = {
#include "../data/BrightStars.inc"
        };
        constexpr int width=2048,height=1024;
        constexpr float pi=3.14159265358979323846f;
        auto image=vsg::ubvec4Array2D::create(width,height,vsg::ubvec4(0,0,0,255));
        image->properties.format=VK_FORMAT_R8G8B8A8_UNORM;
        for(const auto& star:stars) {
            const float x=star.ra*(width/36000.f)-.5f;
            const float y=(9000-star.dec)*(height/18000.f)-.5f;
            const float intensity=std::min(1.f,.66f*std::pow(10.f,-.29f*(star.magnitude*.01f-1.f)));
            const auto tint=stellarTint(star.color*.01f);
            // Rasterise a compact angular PSF. Stretch its longitude support
            // to account for the equirectangular map near the celestial poles.
            const float cosDec=std::max(.014f,std::cos(star.dec*pi/18000.f));
            constexpr float support=1.55f;
            const float radius=support/cosDec;
            for(int iy=std::max(0,int(std::floor(y-support)));iy<=std::min(height-1,int(std::ceil(y+support)));++iy)
                for(int ix=int(std::floor(x-radius));ix<=int(std::ceil(x+radius));++ix) {
                    const float dx=(ix-x)*cosDec,dy=iy-y;
                    const float energy=intensity*std::exp(-1.87f*(dx*dx+dy*dy));
                    auto& pixel=image->at((ix+width)%width,iy);
                    for(int c=0;c<3;++c)pixel[c]=uint8_t(std::min(255,int(pixel[c])+int(std::lround(255.f*energy*tint[c]))));
                }
        }
        return image;
    }();
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod=4.f;
    return vsg::DescriptorImage::create(sampler,atlas,binding);
}
}
