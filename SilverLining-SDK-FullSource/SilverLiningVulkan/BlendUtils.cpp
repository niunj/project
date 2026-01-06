#include "BlendUtils.h"
#include "Device.h"
namespace SilverLining
{
namespace Vulkan
{
VkBlendFactor BlendUtils::ToVulkanBlendFactor(BlendFactor incomingValue, Device* device)
{
    VkBlendFactor factor = VK_BLEND_FACTOR_ZERO;

    switch (incomingValue) {
    case ZERO:
        factor = VK_BLEND_FACTOR_ZERO;
        break;

    case ONE:
        factor = VK_BLEND_FACTOR_ONE;
        break;

    case SRCCOLOR:
        factor = VK_BLEND_FACTOR_SRC_COLOR;
        break;

    case INVSRCCOLOR:
        factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        break;

    case SRCALPHA:
        factor = VK_BLEND_FACTOR_SRC_ALPHA;
        break;

    case INVSRCALPHA:
        factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;

    case DSTCOLOR:
        factor = VK_BLEND_FACTOR_DST_COLOR;
        break;

    case INVDSTCOLOR:
        factor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        break;

    case DSTALPHA:
        factor = VK_BLEND_FACTOR_DST_ALPHA;
        break;

    case INVDSTALPHA:
        factor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        break;

    case SRCALPHASAT:
        factor = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        break;
    }

    return factor;
}

VkBlendOp BlendUtils::ToVulkanBlendOp(BlendOp op, Device* device)
{
    if (op == BLEND_OP_ADD) return VK_BLEND_OP_ADD;
    if (op == BLEND_OP_SUBTRACT) return VK_BLEND_OP_SUBTRACT;
    if (op == BLEND_OP_REVERSE_SUBTRACT) return VK_BLEND_OP_REVERSE_SUBTRACT;
    if (op == BLEND_OP_MIN)  return  VK_BLEND_OP_MIN;
    if (op == BLEND_OP_MAX)  return  VK_BLEND_OP_MAX;

    device->log(LogLevel::SL_LOG_LEVEL_WARN) << "Unknown blend op" << std::endl;
    return VK_BLEND_OP_ADD;
}
}
}
