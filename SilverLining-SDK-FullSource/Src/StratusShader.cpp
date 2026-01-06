// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#include "StratusShader.h"

#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
StratusShader::StratusShader(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();

    stratusShader = renderer->LoadShaderFromFile("Shaders/Stratiform.cg");
    if (renderer->SupportsConstantLocations()) {
        constantLocations.Init(stratusShader, tcsData);
    }
}

ShaderHandle StratusShader::GetShader(void) const
{
    return stratusShader;
}

const StratusShaderConstants& StratusShader::GetConstantLocations(void) const
{
    return constantLocations;
}

StratusShader::~StratusShader()
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->DeleteShader(stratusShader);
}
}