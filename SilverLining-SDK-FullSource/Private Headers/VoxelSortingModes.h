// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file VoxelSortingModes.h
\brief Enumeration of ways to sort voxels
*/

#pragma once

#include "MemAlloc.h"

namespace SilverLining
{
    /** Enumeration of ways to sort voxels; either front-to-back or
    back-to-front. */
    enum VoxelSortingModes
    {
        FRONT_TO_BACK,
        BACK_TO_FRONT
    };
}