#include "MoonTexture.h"
#include <vsg/core/Array2D.h>
namespace rockyenv {
vsg::ref_ptr<vsg::DescriptorImage> moonTexture(uint32_t binding) {
    static const auto image=[] {
        static constexpr uint8_t albedo[]={
#include "../data/MoonAlbedo.inc"
        };
        static constexpr uint8_t elevation[]={
#include "../data/MoonRelief.inc"
        };
        constexpr size_t width=512,height=256;
        static_assert(sizeof(albedo)==width*height && sizeof(elevation)==width*height);
        auto data=vsg::ubvec2Array2D::create(width,height);
        for(size_t pixel=0;pixel<width*height;++pixel)
            data->data()[pixel]=vsg::ubvec2(albedo[pixel],elevation[pixel]);
        data->properties.format=VK_FORMAT_R8G8_UNORM;
        return data;
    }();
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return vsg::DescriptorImage::create(sampler,image,binding);
}
}
