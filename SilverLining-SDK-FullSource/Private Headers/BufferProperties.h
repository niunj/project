// Copyright (c) 2004-2023  Sundog Software, LLC All rights reserved worldwide.

/**
    \file BufferProperties.h
 */

#pragma once

namespace SilverLining
{
    /*
    \param genBuffer whether the buffer is actually generated on the GPU(OpenGL / OpenGL32 only)
    \param dynamic whether the buffer pointer is mappable on the GPU(Vulkan only)
    */
    struct BufferProperties
    {
        // These are used for vertex/index buffers
        bool genBuffer = false;
        bool dynamic = false;

        // These are used for other buffers (Renderer::CreateBuffer path)
        bool useStagingBuffer = false;
        bool useMapUnmap = false;

        int numOfPossibleOffsets = 1;
        bool frameIndexed = false;
    };
}