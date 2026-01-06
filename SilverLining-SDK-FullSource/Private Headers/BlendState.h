// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class BlendState
    {
    public:
        bool enabled = false;

        BlendFactor srcColorBlendFactor = BlendFactor::ONE;
        BlendFactor dstColorBlendFactor = BlendFactor::ZERO;

        BlendFactor srcAlphaBlendFactor = BlendFactor::ONE;
        BlendFactor dstAlphaBlendFactor = BlendFactor::ZERO;

        BlendOp colorBlendOp = BlendOp::BLEND_OP_ADD;
        BlendOp alphaBlendOp = BlendOp::BLEND_OP_ADD;
    public:
        bool operator == (const BlendState& rhs) const;
        bool operator != (const BlendState& rhs) const;
    };
}

