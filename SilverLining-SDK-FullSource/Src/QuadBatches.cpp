// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "QuadBatches.h"
#include "QuadBatch.h"
#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Matrix4.h"
#include "OverriddenBillboardMatrix.h"
#include "BillboardRenderingParameters.h"
#include "BillboardShader.h"
#include "BillboardShaderConstantLocations.h"
#include "BillboardRenderer.h"
#include "Camera.h"
#include "Configuration.h"
#include "Billboard.h"
#include "Atmosphere.h"
#include "Renderable.h"
#include "SLAssert.h"

namespace SilverLining
{
QuadBatches::QuadBatches()
    : dirty(false)
    , compiledForIndirect(false)
{
    spinEnabledOverrideDisabled = false;
    Configuration::GetBoolValue("billboard-spin-enabled-override-disabled", spinEnabledOverrideDisabled);
}

QuadBatches::~QuadBatches()
{
    Clear();
}

void QuadBatches::Clear()
{
    for (SL_VECTOR(QuadBatch*)::iterator it = batches.begin(); it != batches.end(); it++) {
        SL_DELETE(*it);
    }

    batches.clear();
    dirty = false;
    compiledForIndirect = false;
}

static void CopyMatrix(mat4& lhs, const Matrix4& rhs)
{
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            lhs.mat[i][j] = (float)rhs.elem[j][i];
        }
    }
}

static void CopyVector(vec4f& lhs, const Vector4& rhs)
{
    lhs.x = (float)(rhs.x);
    lhs.y = (float)(rhs.y);
    lhs.z = (float)(rhs.z);
    lhs.w = (float)(rhs.w);
}

static void CopyVector(vec4f& lhs, const Vector4f& rhs)
{
    memcpy(&lhs.x, &rhs.x, sizeof(float)* 4);
}

void QuadBatches::Draw(TextureHandle tex, double fade, const Atmosphere* atmosphere, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
                       , bool indirect, ThreadCameraStreamData* tcsData) const
{
    Renderer* renderer = tcsData->GetRenderer();

    renderer->EnableBackfaceCulling(false);
    renderer->EnableTexture(tex, 0);

    const BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    // calculate sl_flipTCoordsAndFogDensity
    const bool additive = billboardParams->GetAdditiveBlending();
    const Vector4f sl_flipTCoordsAndFogDensityVal = !renderer->GetIsDirectX() ?
            Vector4f(1.0f, (float)billboardParams->GetFogDensity(), 1.0f, additive ? 1.0f : 0.0f) : Vector4f(-1.0f, (float)billboardParams->GetFogDensity(), -1.0f, additive ? 1.0f : 0.0f);

    // calculate sl_fogColor
    const Color fog = billboardParams->GetFogColor();
    const Vector3 sl_fogColorVal(fog.r, fog.g, fog.b);

    // calculate sl_billboardColor
    Color forceColor;
    bool forceConstantColor = tcsData->GetBillboardRenderingParams()->GetForceConstantColor(forceColor);
    const Vector4f sl_billboardColorVal = forceConstantColor ? forceColor.ToVector4f() : Vector4f(1.0, 1.0, 1.0, 0.0);

    // calculate sl_fade
    const float t = (float)millis * 0.001f;
    const float fadeT0 = (float)billboardParams->GetFadeStartTime() * 0.001f;
    const Vector4f sl_fadeVal((float)fade, (float)billboardParams->GetFogColorBoost(), fadeT0, t);

    // calculate sl_noSpin
    const Vector4f sl_noSpinVal = billboardParams->GetSpinEnabled() && !spinEnabledOverrideDisabled ?
                                  Vector4f(0.0f, (float)billboardParams->GetVerticalGradient(), billboardParams->GetSoftness(), billboardParams->GetFlattening())
                                  : Vector4f(1.0f, (float)billboardParams->GetVerticalGradient(), billboardParams->GetSoftness(), billboardParams->GetFlattening());


    const Matrix4& billboardMatrix = overriddenBillboardMatrix.GetBillboardMatrix();

    const Vector4f sl_forceDepth(0.0, 0.0, 0.0, 0.0);

    if (indirect==false) {
        const BillboardShader* billboardShaderContainer = tcsData->GetBillboardShader();
        if (billboardShaderContainer == 0) {
            return;
        }
        const ShaderHandle billboardShader = billboardShaderContainer->GetShaderHandle();
        const BillboardShaderConstantLocations* billboardShaderConstantLocations = billboardShaderContainer->GetConstantLocations();

        renderer->BindShader(billboardShader);

        BillboardShadingUtilities shadingUtils(billboardShader, billboardShaderConstantLocations, tcsData);
        shadingUtils.UnsetShaderColor();

        if (renderer->SupportsConstantLocations() && billboardShaderConstantLocations) {
            // vs
            renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_modelViewLoc, renderCamera->GetModelViewMatrixFloatArray());
            renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_modelViewProjLoc, renderCamera->GetModelViewProjectionMatrixFloatArray());
            renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_billboardLoc, billboardMatrix);
            renderer->SetConstantMatrix4AtLocation(billboardShader, billboardShaderConstantLocations->sl_basisLoc, renderCamera->GetBasis4x4FloatArray());

            renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_flipTCoordsAndFogDensityLoc, sl_flipTCoordsAndFogDensityVal);
            renderer->SetConstantVectorAtLocation(billboardShader, billboardShaderConstantLocations->sl_fogColorLoc, sl_fogColorVal);
            renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_fadeLoc, sl_fadeVal);

            if (renderer->GetIsVulkan()) renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_forceDepthLoc, sl_forceDepth);

            // ps
            renderer->SetConstantVectorAtLocation(billboardShader, billboardShaderConstantLocations->sl_outputScaleLoc, atmosphere->GetOutputScale());

            // common
            renderer->SetConstantVector4AtLocation(billboardShader, billboardShaderConstantLocations->sl_noSpinLoc, sl_noSpinVal);
        } else {
            // vs
            renderer->SetConstantMatrix(billboardShader, "sl_modelView", renderCamera->GetModelViewMatrixFloatArray());
            renderer->SetConstantMatrix(billboardShader, "sl_modelViewProj", renderCamera->GetModelViewProjectionMatrixFloatArray());
            renderer->SetConstantMatrix(billboardShader, "sl_billboard", billboardMatrix);
            renderer->SetConstantMatrix(billboardShader, "sl_basis", renderCamera->GetBasis4x4FloatArray());

            renderer->SetConstantVector4(billboardShader, "sl_flipTCoordsAndFogDensity", sl_flipTCoordsAndFogDensityVal);
            renderer->SetConstantVector(billboardShader, "sl_fogColor", sl_fogColorVal);
            renderer->SetConstantVector4(billboardShader, "sl_fade", sl_fadeVal);

            if (renderer->GetIsVulkan()) renderer->SetConstantVector4(billboardShader, "sl_forceDepth", sl_forceDepth);

            // ps
            renderer->SetConstantVector(billboardShader, "sl_outputScale", atmosphere->GetOutputScale());

            // common
            renderer->SetConstantVector4(billboardShader, "sl_noSpin", sl_noSpinVal);
        }
    }

    bool savedFog = renderer->GetFogEnabled();
    renderer->EnableFog(false);

    if (indirect && IsCompiledForIndirect()) {
        BillboardRenderer* billboardRenderer = tcsData->GetBillboardRenderer();

        BillboardParams params;
        CopyMatrix(params.sl_modelView, renderCamera->GetModelViewMatrix());
        CopyMatrix(params.sl_modelViewProj, renderCamera->GetModelViewProjectionMatrix());
        CopyMatrix(params.sl_billboard, billboardMatrix);
        CopyMatrix(params.sl_basis, renderCamera->GetBasis4x4());

        CopyVector(params.sl_flipTCoordsAndFogDensity, sl_flipTCoordsAndFogDensityVal);
        CopyVector(params.sl_fogColor, sl_fogColorVal);
        CopyVector(params.sl_billboardColor, sl_billboardColorVal);
        CopyVector(params.sl_fade, sl_fadeVal);

        Vector4 sl_outputScaleVal(atmosphere->GetOutputScale().x, atmosphere->GetOutputScale().y, atmosphere->GetOutputScale().z, 0);
        CopyVector(params.sl_outputScale, sl_outputScaleVal);
        CopyVector(params.sl_noSpin, sl_noSpinVal);

        TextureManager* textureManager = (atmosphere->TexturesAreShared()) ? atmosphere->GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();
        billboardRenderer->UpdateTexturesBufferIfNecessary(textureManager);
        float textureIndex = (float)billboardRenderer->textureIndex(tex);

        CopyVector(params.sl_padding0, Vector4f(textureIndex, 0, 0, 0));
        CopyVector(params.sl_forceDepth, Vector4f(0, 0, 0, 0));

        const SL_VECTOR(Renderable*)& blendedObjects = renderer->GetBlendedObjects();
        const int currentBlendedObjectBeingDrawn = renderer->GetCurrentBlendedObjectBeingDrawnIndex();

        size_t indexOfNext = currentBlendedObjectBeingDrawn + 1;

        // check next
        bool nextInstanced = false;
        if (indexOfNext < blendedObjects.size()) {
            // if next is also instanced, continue to accumulate
            if (blendedObjects[indexOfNext]->GetInstanced()) {
                nextInstanced = true;
            }
        }

        std::vector<DrawElementsIndirectBindlessCommandNV>& billboardCommandBuffer = billboardRenderer->GetBillboardCommandBuffer();
        std::vector<BillboardParams>& billboardParamsVector = billboardRenderer->GetBillboardParamsVector();

        for (size_t i = 0; i < commandBufferForIndirect.size(); ++i) {
            billboardParamsVector.push_back(params);
            billboardCommandBuffer.push_back(commandBufferForIndirect[i]);
        }

        // If next is *not* instanced OR we reached the end
        //  -> draw so far
        if (nextInstanced == false || currentBlendedObjectBeingDrawn + 1 == blendedObjects.size()) {
            const int sizeToCopy = (int)billboardParamsVector.size()*sizeof(BillboardParams);

            Buffer* paramsBuffer = billboardRenderer->GetBillboardParamsBuffer();

            paramsBuffer->SyncToGpu(0, sizeToCopy, billboardParamsVector.data(), renderer->GetContext());

            paramsBuffer->Bind(0, renderer->GetContext());

            Buffer* texturesBuffer = billboardRenderer->GetTexturesBuffer();

            texturesBuffer->Bind(1, renderer->GetContext());

            renderer->BindShader(tcsData->GetBillboardShaderInstanced()->GetShaderHandle());

            renderer->DrawMultiStrips(&billboardCommandBuffer);

            texturesBuffer->Bind(1, renderer->GetContext());

            paramsBuffer->UnBind(0, renderer->GetContext());


            billboardParamsVector.clear();
            billboardCommandBuffer.clear();
        }
    } else {
        for (SL_VECTOR(QuadBatch *)::const_iterator it = batches.begin(); it != batches.end(); it++) {
            (*it)->Draw();
        }
    }

    renderer->EnableFog(savedFog);

    renderer->UnbindShader();
}
void QuadBatches::CompileForIndirect(ThreadCameraStreamData* tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();
    commandBufferForIndirect.resize(batches.size());

    int i = 0;
    for (SL_VECTOR(QuadBatch*)::const_iterator it = batches.begin(); it != batches.end(); it++) {
        const QuadBatch* quadBatch = *it;

        const VertexBuffer* vertexBuffer = quadBatch->GetVertexBuffer();
        const IndexBuffer* indexBuffer = quadBatch->GetIndexBuffer();

        const GPUAddressType vboAddress = renderer->GetBufferAddress(vertexBuffer->GetHandle());
        const GPUAddressType iboAddress = renderer->GetBufferAddress(quadBatch->GetIndexBuffer()->GetHandle());

        renderer->MakeBufferResident(quadBatch->GetVertexBuffer()->GetHandle(), true);
        renderer->MakeBufferResident(quadBatch->GetIndexBuffer()->GetHandle(), true);

        const int numIndices = quadBatch->GetIndexBuffer()->GetNumIndices();

        DrawElementsIndirectCommand& command = commandBufferForIndirect[i].cmd;
        command.count = numIndices;
        command.instanceCount = 1;
        command.firstIndex = 0;
        command.baseVertex = 0;
        command.baseInstance = 0;

        BindlessPtrNV& indexBufferBindless = commandBufferForIndirect[i].indexBuffer;
        indexBufferBindless.reserved = 0;
        indexBufferBindless.index = 0;
        indexBufferBindless.address = iboAddress;
        indexBufferBindless.length = numIndices*sizeof(Index);

        commandBufferForIndirect[i].vertexBuffers[0].reserved = 0;
        commandBufferForIndirect[i].vertexBuffers[0].index = 0;
        commandBufferForIndirect[i].vertexBuffers[0].address = vboAddress;
        commandBufferForIndirect[i].vertexBuffers[0].length = vertexBuffer->GetNumVertices()*sizeof(Vertex);

        ++i;
    }

    BillboardRenderer* billboardRenderer = tcsData->GetBillboardRenderer();
    billboardRenderer->incrementMaxInstancedBillboardsBy((int)commandBufferForIndirect.size());
    compiledForIndirect = true;
}

bool QuadBatches::IsCompiledForIndirect(void) const
{
    return compiledForIndirect;
}

}
