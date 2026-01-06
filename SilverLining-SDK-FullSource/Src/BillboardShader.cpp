// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#include "BillboardShader.h"
#include "ThreadCameraStreamData.h"
#include "BillboardRenderingParameters.h"
#include "BillboardShaderConstantLocations.h"

#include "OverriddenBillboardMatrix.h"
#include "Camera.h"
#include "Renderer.h"
#include "Atmosphere.h"
#include "Configuration.h"

namespace SilverLining
{

BillboardShader::BillboardShader(ThreadCameraStreamData* _tcsData, bool _instanced)
    : locations(0)
    , shaderHandle(0)
    , tcsData(_tcsData)
    , instanced(_instanced)
{
    Renderer* renderer = tcsData->GetRenderer();

    if (instanced) {
        SL_VECTOR(SL_STRING) defines;
        defines.push_back("BILLBOARD_SHADER_INSTANCED");
        shaderHandle = renderer->LoadShaderFromFile("Shaders/Billboard.cg", defines);
    } else {
        shaderHandle = renderer->LoadShaderFromFile("Shaders/Billboard.cg");
    }

    if (renderer->SupportsConstantLocations()) {
        locations = SL_NEW BillboardShaderConstantLocations();
        locations->Init(renderer, shaderHandle);
    }

    float fogColorBoost;
    Configuration::GetFloatValue("billboard-fog-color-boost", fogColorBoost);
    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
    billboardParams->SetFogColorBoost(fogColorBoost);
}
BillboardShader::~BillboardShader()
{
    Renderer* renderer = tcsData->GetRenderer();
    ShaderHandle shaderHandle = GetShaderHandle();
    if (shaderHandle) {
        renderer->DeleteShader(GetShaderHandle());
    }

    if (locations) {
        SL_DELETE locations;
    }
}

ShaderHandle BillboardShader::GetShaderHandle(void) const
{
    return shaderHandle;
}

const BillboardShaderConstantLocations* BillboardShader::GetConstantLocations(void) const
{
    return locations;
}

BillboardShadingUtilities::BillboardShadingUtilities(ShaderHandle _billboardShader, const BillboardShaderConstantLocations* _billboardShaderConstantLocations, ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
    , billboardShader(_billboardShader)
    , billboardShaderConstantLocations(_billboardShaderConstantLocations)
{
    // nothing else to do
}

BillboardShadingUtilities::~BillboardShadingUtilities()
{
    // nothing else to do
}

void BillboardShadingUtilities::SetShaderColor(const Color& color) const
{
    Renderer* renderer = tcsData->GetRenderer();
    if (renderer->SupportsConstantLocations() && billboardShaderConstantLocations) {
        renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_billboardColorLoc, color.ToVector4());
    } else {
        renderer->SetConstantVector4(billboardShader, "sl_billboardColor", color.ToVector4());
    }
}
void BillboardShadingUtilities::UnsetShaderColor() const
{
    Color forceColor;
    bool forceConstantColor = tcsData->GetBillboardRenderingParams()->GetForceConstantColor(forceColor);

    Renderer* renderer = tcsData->GetRenderer();

    if (renderer->SupportsConstantLocations() && billboardShaderConstantLocations) {
        renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_billboardColorLoc, (forceConstantColor) ? forceColor.ToVector4() : Vector4(1.0, 1.0, 1.0, 0.0));
    } else {
        renderer->SetConstantVector4(billboardShader, "sl_billboardColor", (forceConstantColor) ? forceColor.ToVector4() : Vector4(1.0, 1.0, 1.0, 0.0));
    }
}
void BillboardShadingUtilities::BindAndSaveFogAndSetupConstants(double fade, const Vector3& outputScale, bool objectSpace, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, bool& savedFog, bool infiniteDepth) const
{
    if (billboardShader == 0) {
        return;
    }

    Renderer* renderer = tcsData->GetRenderer();

    renderer->BindShader(billboardShader);

    UnsetShaderColor();

    const BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    const bool additive = billboardParams->GetAdditiveBlending();
    const Color fog = billboardParams->GetFogColor();

    const Vector3 fogColor(fog.r, fog.g, fog.b);
    const float t = (float)((float)millis * 0.001f);
    const float fadeT0 = (float)billboardParams->GetFadeStartTime() * 0.001f;

    const double verticalGradient = billboardParams->GetVerticalGradient();
    const float flattening = billboardParams->GetFlattening();
    const float fogColorBoost = billboardParams->GetFogColorBoost();

    Vector4 forceDepth;
    if (infiniteDepth) {
        forceDepth.x = 1.0;
        float zmin, zmax;
        renderer->GetDepthRange(zmin, zmax);
        forceDepth.y = zmax;
    } else {
        forceDepth.x = 0.0;
    }

    if (renderer->SupportsConstantLocations() && billboardShaderConstantLocations) {
        // vs
        renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_modelViewLoc, renderCamera->GetModelViewMatrixFloatArray());
        renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_modelViewProjLoc, renderCamera->GetModelViewProjectionMatrixFloatArray());
        renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_billboardLoc, overriddenBillboardMatrix.GetBillboardMatrix());
        renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_basisLoc, (objectSpace) ? renderCamera->GetBasis4x4() : Matrix4());
        renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_flipTCoordsAndFogDensityLoc, !renderer->GetIsDirectX() ?
                                               Vector4(1, billboardParams->GetFogDensity(), 1, additive ? 1.0 : 0.0) : Vector4(-1, billboardParams->GetFogDensity(), -1, additive ? 1.0 : 0.0));
        renderer->SetConstantVectorAtLocation(billboardShader, billboardShaderConstantLocations->sl_fogColorLoc, fogColor);
        renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_fadeLoc, Vector4(fade, fogColorBoost, fadeT0, t));

        if (renderer->GetIsVulkan()) renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_forceDepthLoc, forceDepth);

        // ps
        renderer->SetConstantVectorAtLocation(billboardShader, billboardShaderConstantLocations->sl_outputScaleLoc, outputScale);

        // common
        renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_noSpinLoc, billboardParams->GetSpinEnabled() ?
                                               Vector4(0, verticalGradient, billboardParams->GetSoftness(), flattening)
                                               : Vector4(1.0, verticalGradient, billboardParams->GetSoftness(), flattening));
    } else {
        // vs
        renderer->SetConstantMatrix(billboardShader, "sl_modelView", renderCamera->GetModelViewMatrixFloatArray());
        renderer->SetConstantMatrix(billboardShader, "sl_modelViewProj", renderCamera->GetModelViewProjectionMatrixFloatArray());
        renderer->SetConstantMatrix(billboardShader, "sl_billboard", overriddenBillboardMatrix.GetBillboardMatrix());
        renderer->SetConstantMatrix(billboardShader, "sl_basis", (objectSpace) ? renderCamera->GetBasis4x4() : Matrix4());
        renderer->SetConstantVector4(billboardShader, "sl_flipTCoordsAndFogDensity", !renderer->GetIsDirectX() ?
                                     Vector4(1, billboardParams->GetFogDensity(), 1, additive ? 1.0 : 0.0) : Vector4(-1, billboardParams->GetFogDensity(), -1, additive ? 1.0 : 0.0));
        renderer->SetConstantVector(billboardShader, "sl_fogColor", fogColor);
        renderer->SetConstantVector4(billboardShader, "sl_fade", Vector4(fade, fogColorBoost, fadeT0, t));

        if (renderer->GetIsVulkan()) renderer->SetConstantVector4(billboardShader, "sl_forceDepth", forceDepth);

        // ps
        renderer->SetConstantVector(billboardShader, "sl_outputScale", outputScale);

        // common
        renderer->SetConstantVector4(billboardShader, "sl_noSpin", billboardParams->GetSpinEnabled() ?
                                     Vector4(0, verticalGradient, billboardParams->GetSoftness(), flattening)
                                     : Vector4(1.0, verticalGradient, billboardParams->GetSoftness(), flattening));

    }

    savedFog = renderer->GetFogEnabled();
    renderer->EnableFog(false);
}
void BillboardShadingUtilities::UnbindAndRestoreFog(bool savedFog) const
{
    Renderer* renderer = tcsData->GetRenderer();
    if (billboardShader) {
        renderer->UnbindShader();
        renderer->EnableFog(savedFog);
    }
}
}