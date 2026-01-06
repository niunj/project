// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.
#include "SkyShaders.h"
#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Configuration.h"

namespace SilverLining
{
SkyShaders::SkyShaders(ThreadCameraStreamData* _tcsData, bool _simpleShader)
    : tcsData(_tcsData)
    , simpleShader(_simpleShader)
{
    skyShaderConstantLocations.invalid = true;
    skyShaderConstantLocations.shader = 0;

    skyShader = skyShaderHDR = skyShaderHW = skyShaderHDRHW = 0;

    Renderer* renderer = tcsData->GetRenderer();

    if (!simpleShader) {
        skyShader = renderer->LoadShaderFromFile("Shaders/Sky-complex.cg");
        skyShaderHW = renderer->LoadShaderFromFile("Shaders/Sky-Hosek-Wilkie.cg");
    }

    if (!skyShader || !skyShaderHW) {
        skyShader = renderer->LoadShaderFromFile("Shaders/Sky.cg");
        simpleShader = true;
    }

    skyShaderHDR = renderer->LoadShaderFromFile("Shaders/SkyHDR.cg");
    skyShaderHDRHW = renderer->LoadShaderFromFile("Shaders/Sky-Hosek-Wilkie-HDR.cg");

    skyShaderHDRHWConstantLocations.Init(skyShaderHDRHW, tcsData);
    skyShaderHWConstantLocations.Init(skyShaderHW, tcsData);
    skyShaderHDRConstantLocations.Init(skyShaderHDR, tcsData);
    skyShaderConstantLocations.Init(skyShader, tcsData);
}

SkyShaders::~SkyShaders()
{
    DeleteShaders();
}

ShaderHandle SkyShaders::GetSkyShader(void) const
{
    return skyShader;
}
ShaderHandle SkyShaders::GetSkyShaderHdr(void) const
{
    return skyShaderHDR;
}

ShaderHandle SkyShaders::GetSkyShaderHosenWilkie(void) const
{
    return skyShaderHW;
}
ShaderHandle SkyShaders::GetSkyShaderHosenWilkieHdr(void) const
{
    return skyShaderHDRHW;
}

void SkyShaders::DeleteShaders(void)
{
    Renderer* renderer = tcsData->GetRenderer();

    renderer->DeleteShader(skyShader);
    renderer->DeleteShader(skyShaderHW);
    renderer->DeleteShader(skyShaderHDR);
    renderer->DeleteShader(skyShaderHDRHW);
}

ShaderHandle SkyShaders::GetShaderFor(SkyModel skyModel, bool disableToneMapping) const
{
    ShaderHandle currentShader = 0;
    if (skyModel == HOSEK_WILKIE) {
        currentShader = disableToneMapping ? skyShaderHDRHW : skyShaderHW;
    } else {
        currentShader = disableToneMapping ? skyShaderHDR : skyShader;
    }

    return currentShader;
}

const SkyShaderConstantLocations& SkyShaders::GetSkyShaderConstantLocations(SkyModel skyModel, bool disableToneMapping) const
{
    const SkyShaderConstantLocations* constantLocations = 0;
    if (skyModel == HOSEK_WILKIE) {
        constantLocations = disableToneMapping ? &skyShaderHDRHWConstantLocations : &skyShaderHWConstantLocations;
    } else {
        constantLocations = disableToneMapping ? &skyShaderHDRConstantLocations : &skyShaderConstantLocations;
    }
    return *constantLocations;
}
}