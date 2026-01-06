// Copyright 2006-2020 Sundog Software. All rights reserved worldwide.
#include "LineShader.h"

namespace SilverLining
{
LineShader::LineShader()
    : sh(0)
    , modelViewProjUniformLocation(-1)
    , colorUniformLocation(-1)
    , vertexLocation(-1)
    , initialized(false)
{
}
}