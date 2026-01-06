// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#include "StratocumulusShaders.h"
#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
StratocumulusShaders::StratocumulusShaders(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{
    shader = hdrShader = 0;
}

StratocumulusShaders::~StratocumulusShaders()
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->DeleteShader(shader);
    renderer->DeleteShader(hdrShader);
}

ShaderHandle StratocumulusShaders::GetShader(void) const
{
    if (!shader) {
        Renderer* renderer = tcsData->GetRenderer();
        shader = renderer->LoadShaderFromFile("Shaders/Stratocumulus.cg");
        if (renderer->SupportsConstantLocations()) {
            shaderConstantLocations.Init(shader, tcsData);
        }
    }

    return shader;
}
ShaderHandle StratocumulusShaders::GetShaderHdr(void) const
{
    if (!hdrShader) {
        Renderer* renderer = tcsData->GetRenderer();
        hdrShader = renderer->LoadShaderFromFile("Shaders/StratocumulusHDR.cg");
        if (renderer->SupportsConstantLocations()) {
            shaderConstantLocationsHDR.Init(hdrShader, tcsData);
        }
    }

    return hdrShader;
}

const StratocumulusShaderConstantLocations& StratocumulusShaders::GetShaderConstantLocations(void) const
{
    return shaderConstantLocations;
}
const StratocumulusShaderConstantLocations& StratocumulusShaders::GetShaderConstantLocationsHdr(void) const
{
    return shaderConstantLocationsHDR;
}
}