#include "BlendState.h"

namespace SilverLining
{
bool BlendState::operator==(const BlendState& rhs) const
{
    const BlendState& lhs = *this;

    return
        lhs.enabled == rhs.enabled

        && lhs.srcColorBlendFactor == rhs.srcColorBlendFactor
        && lhs.dstColorBlendFactor == rhs.dstColorBlendFactor

        && lhs.srcAlphaBlendFactor == rhs.srcAlphaBlendFactor
        && lhs.dstAlphaBlendFactor == rhs.dstAlphaBlendFactor

        && lhs.colorBlendOp == rhs.colorBlendOp
        && lhs.alphaBlendOp == rhs.alphaBlendOp;
}
bool BlendState::operator!=(const BlendState& rhs) const
{
    return !(this->operator==(rhs));
}
}