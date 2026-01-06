// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "StratocumulusShaderConstantLocations.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
StratocumulusShaderConstantLocations::StratocumulusShaderConstantLocations()
{
    Reset();
}

void StratocumulusShaderConstantLocations::Init(ShaderHandle shader, ThreadCameraStreamData* tcsData)
{
    Reset();

    const Renderer* renderer = tcsData->GetRenderer();
    sl_projectionMatrix = renderer->GetConstantLocation(shader, "sl_projectionMatrix");

    sl_lightColor = renderer->GetConstantLocation(shader, "sl_lightColor");
    sl_lightObjectDirAndConstTerm = renderer->GetConstantLocation(shader, "sl_lightObjectDirAndConstTerm");
    sl_lightTexCoords = renderer->GetConstantLocation(shader, "sl_lightTexCoords");
    sl_viewSampleDimensions = renderer->GetConstantLocation(shader, "sl_viewSampleDimensions");
    sl_lightSampleDimensions = renderer->GetConstantLocation(shader, "sl_lightSampleDimensions");
    sl_voxelDimensions = renderer->GetConstantLocation(shader, "sl_voxelDimensions");
    sl_lightWorldDirAndExtinction = renderer->GetConstantLocation(shader, "sl_lightWorldDirAndExtinction");
    sl_cameraTexCoords = renderer->GetConstantLocation(shader, "sl_cameraTexCoords");
    sl_skyLightColor = renderer->GetConstantLocation(shader, "sl_skyLightColor");
    sl_originTexCoords = renderer->GetConstantLocation(shader, "sl_originTexCoords");
    sl_fadeFlag = renderer->GetConstantLocation(shader, "sl_fadeFlag");
    sl_fogColorAndDensity = renderer->GetConstantLocation(shader, "sl_fogColorAndDensity");
    sl_multipleScatteringTerm = renderer->GetConstantLocation(shader, "sl_multipleScatteringTerm");
    sl_noiseOffset = renderer->GetConstantLocation(shader, "sl_noiseOffset");
    sl_extinctionCoefficient = renderer->GetConstantLocation(shader, "sl_extinctionCoefficient");
    sl_jitter = renderer->GetConstantLocation(shader, "sl_jitter");
    sl_unitScale = renderer->GetConstantLocation(shader, "sl_unitScale");
    sl_outputScale = renderer->GetConstantLocation(shader, "sl_outputScale");
    sl_roundCenter = renderer->GetConstantLocation(shader, "sl_roundCenter");
}
void StratocumulusShaderConstantLocations::Reset(void)
{
    sl_projectionMatrix = -1;

    sl_lightColor = -1;
    sl_lightObjectDirAndConstTerm = -1;
    sl_lightTexCoords = -1;
    sl_viewSampleDimensions = -1;
    sl_lightSampleDimensions = -1;
    sl_voxelDimensions = -1;
    sl_lightWorldDirAndExtinction = -1;
    sl_cameraTexCoords = -1;
    sl_skyLightColor = -1;
    sl_originTexCoords = -1;
    sl_fadeFlag = -1;
    sl_fogColorAndDensity = -1;
    sl_multipleScatteringTerm = -1;
    sl_noiseOffset = -1;
    sl_extinctionCoefficient = -1;
    sl_jitter = -1;
    sl_unitScale = -1;
    sl_outputScale = -1;
    sl_roundCenter = -1;
}
}
