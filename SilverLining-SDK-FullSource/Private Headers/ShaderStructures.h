// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

/** \file ShaderStructures.h
*/

#pragma once
#include "MemAlloc.h"

namespace SilverLining
{
    // structures that match the equivalent structures/types in shaders
    struct vec4f {
        float x, y, z, w;
    };
    struct vec3f {
        float x, y, z;
    };

    struct mat4 {
        float mat[4][4];
    };
}