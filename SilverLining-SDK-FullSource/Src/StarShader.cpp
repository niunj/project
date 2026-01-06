// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#include "StarShader.h"

#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
StarShader::StarShader(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();
    starShader = renderer->LoadShaderFromFile("Shaders/Stars.cg");
    if (renderer->SupportsConstantLocations()) {
        starShaderConstantLocations.Init(starShader, tcsData);
    }
}

StarShader::~StarShader()
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->DeleteShader(starShader);
}

ShaderHandle StarShader::GetShader(void) const
{
    return starShader;
}

const StarShaderConstantLocations& StarShader::GetConstantLocations(void) const
{
    return starShaderConstantLocations;
}
}