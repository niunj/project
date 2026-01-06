// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#include "CirrusShader.h"

#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
CirrusShader::CirrusShader(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();

    hdrShader = renderer->LoadShaderFromFile("Shaders/CirrusHDR.cg");
    if (renderer->SupportsConstantLocations()) {
        constantLocations.Init(hdrShader, tcsData);
    }
}

ShaderHandle CirrusShader::GetShader(void) const
{
    return hdrShader;
}
const CirrusShaderConstantLocations& CirrusShader::GetConstantLocations(void) const
{
    return constantLocations;
}

CirrusShader::~CirrusShader()
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->DeleteShader(hdrShader);
}
}