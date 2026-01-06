// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.
#include "FlareShader.h"

#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
FlareShader::FlareShader(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();

    flareShader = renderer->LoadShaderFromFile("Shaders/Flare.cg");
    if (renderer->GetType() == Atmosphere::OPENGL32CORE || renderer->GetType() == Atmosphere::OPENGL) {
        constantLocations.Init(flareShader, tcsData);
    }
}

ShaderHandle FlareShader::GetShader(void) const
{
    return flareShader;
}
const FlareShaderConstantLocations& FlareShader::GetConstantLocations(void) const
{
    return constantLocations;
}

FlareShader::~FlareShader()
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->DeleteShader(flareShader);
}
}