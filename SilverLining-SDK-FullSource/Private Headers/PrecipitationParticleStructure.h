// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file PrecipitationParticleStructure.h
*/

#pragma once
#include "MemAlloc.h"
#include "ShaderStructures.h"

namespace SilverLining
{
    struct Particle {
        vec3f sl_particlePosition;
        int sl_textureIndex;
        vec4f sl_lightingColor;
    };
}
