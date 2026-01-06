// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.
#include "PrecipitationShaders.h"

#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
PrecipitationShaders::PrecipitationShaders(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
    , precipShader(0)
{
    Renderer* renderer = tcsData->GetRenderer();

    InitShaders();
}

PrecipitationShaders::~PrecipitationShaders()
{
    DeleteShaders();
}

void PrecipitationShaders::ReloadShaders()
{
    DeleteShaders();
    InitShaders();
}

void PrecipitationShaders::InitShaders()
{
    Renderer* renderer = tcsData->GetRenderer();

    if (renderer->SupportsInstancedRendering() == false) {
        return;
    }

    if (renderer->GetType() == Atmosphere::OPENGL32CORE || renderer->GetType()==Atmosphere::VULKAN) {
        shaderAndConstantLocationsRain.Init(renderer, renderer->LoadShaderFromFile("Shaders/Particle-instanced.cg"));
        shaderAndConstantLocationsSleet.Init(renderer, renderer->LoadShaderFromFile("Shaders/Particle-instanced-sleet.cg"));
        shaderAndConstantLocationsSnow.Init(renderer, renderer->LoadShaderFromFile("Shaders/Particle-instanced-snow.cg"));
    }
    // load the non-instanced one just in case
    if (renderer->GetType() != Atmosphere::VULKAN) { // vulkan supports only the instanced (fast) path
        precipShader = renderer->LoadShaderFromFile("Shaders/Particle.cg");
    }
}

void PrecipitationShaders::DeleteShaders()
{
    Renderer* renderer = tcsData->GetRenderer();

    renderer->DeleteShader(precipShader);
    precipShader = 0;

    if (renderer->SupportsInstancedRendering() && renderer->GetType() == Atmosphere::OPENGL32CORE) {
        renderer->DeleteShader(shaderAndConstantLocationsRain.shaderHandle);
        renderer->DeleteShader(shaderAndConstantLocationsSleet.shaderHandle);
        renderer->DeleteShader(shaderAndConstantLocationsSnow.shaderHandle);
    }
}

ShaderHandle PrecipitationShaders::GetShaderProgram() const
{
    return precipShader;
}

ShaderHandle PrecipitationShaders::GetShaderProgramInstancedRain() const
{
    return shaderAndConstantLocationsRain.shaderHandle;
}

ShaderHandle PrecipitationShaders::GetShaderProgramInstancedSleet() const
{
    return shaderAndConstantLocationsSleet.shaderHandle;
}

ShaderHandle PrecipitationShaders::GetShaderProgramInstancedSnow() const
{
    return shaderAndConstantLocationsSnow.shaderHandle;
}

const PrecipitationShaderConstantLocations& PrecipitationShaders::GetShaderProgramInstancedRainLocations() const
{
    return shaderAndConstantLocationsRain;
}

const PrecipitationShaderConstantLocations& PrecipitationShaders::GetShaderProgramInstancedSleetLocations() const
{
    return shaderAndConstantLocationsSleet;
}

const PrecipitationShaderConstantLocations& PrecipitationShaders::GetShaderProgramInstancedSnowLocations() const
{
    return shaderAndConstantLocationsSnow;
}

}