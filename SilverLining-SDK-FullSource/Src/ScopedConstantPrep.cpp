// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#include "ScopedConstantPrep.h"
#include "Renderer.h"

namespace SilverLining
{
ScopedConstantPrep::ScopedConstantPrep(Renderer* _renderer, ShaderHandle _shaderHandle, bool _doPost)
    : renderer(_renderer)
    , shaderHandle(_shaderHandle)
    , doPost(_doPost)
{
    renderer->PreConstantsSet(shaderHandle);
}

ScopedConstantPrep::~ScopedConstantPrep()
{
    if (doPost) {
        renderer->PostConstantsSet(shaderHandle);
    }
}
}