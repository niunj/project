// Copyright 2006-2020 Sundog Software. All rights reserved worldwide.

#include "SLAssert.h"
#include "Context.h"
#include "MatrixGL.h"
#include "Shader.h"
#include "OpenGLImmediateStream.h"
#include "OpenGLDeferredStream.h"
#include "OpenGLExtensionManager.h"
#include "OpenGLUtils.h"
#include "SL_Buffer.h"
#include "TextureLoader.h"
#include "Vertex.h"
#include "VAOManagerOpenGL.h"
#include "Mutex.h"

#include <algorithm>
#include <iostream>

namespace SilverLining
{

#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
MutexContainer Context::countMutexContainer;
int Context::totalNumShaders = 0;
int Context::contextCounter = 0;
#endif

Context::Context(bool _useUBOs, bool _useProgramUniforms, bool _avoidStalls, bool _deferred)
    : useUBOs(_useUBOs)
    , useProgramUniforms(_useProgramUniforms)
    , avoidStalls(_avoidStalls)
    , deferred(_deferred)
    , cullClockWise(true)
    , reverseDepth(false)
    , currentShader(0)
    , forceImmediate(false)
{
    mProjection.setIdentity();
    mModelview.setIdentity();
    mTexture.setIdentity();

    if (deferred) {
        stream = SL_NEW OpenGLDeferredStream(this);
    } else {
        stream = SL_NEW OpenGLImmediateStream(this);
    }

    immediateStream = SL_NEW OpenGLImmediateStream(this);

    vaoManager = VAOManagerOpenGL::instance();
    vaoManager->AddRef();

#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    numShaders = 0;
    id = contextCounter++;
    std::cout << "num of contexts: " << contextCounter << std::endl;
#endif
}

Context::~Context()
{
    for (unsigned int i = 0; i < indexBuffersPool.size(); ++i) {
        SL_Buffer* indexBuffer = indexBuffersPool.at(i);
        SL_DELETE indexBuffer;
    }

    SL_DELETE stream;

    SL_DELETE immediateStream;

    vaoManager->RemoveRef();

#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    --contextCounter;
    std::cout << "num of contexts: " << contextCounter << std::endl;
#endif
}

void Context::DeleteShader(ShaderHandle shaderHandle)
{
    Shader* shader = (Shader*)shaderHandle;

    SL_ASSERT(shader->context == this);
    SL_DELETE shader;

#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
    --numShaders;
    --totalNumShaders;
    std::cout << "num shaders for context[" << id << "]: " << numShaders << " , total: " << totalNumShaders << std::endl;
#endif

    shaderList.Get().erase(std::remove(shaderList.Get().begin(), shaderList.Get().end(), shader), shaderList.Get().end());
}


ShaderHandle Context::LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling
        , const SL_VECTOR(SL_STRING)& defines
        , ResourceLoader* resourceLoader)
{
    Shader* shader = SL_NEW Shader(this, useUBOs, useProgramUniforms);
    bool ok = shader->LoadShaderFromFile(fileName, userVertShader, userFragShader, enableObjectLabeling, defines, resourceLoader, userShaderList);
    if (ok) {
        shaderList.Get().push_back(shader);
#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
        ScopedMutex scopedMutex(countMutexContainer.mutex);
        ++numShaders;
        ++totalNumShaders;
        std::cout<<"num shaders for context["<<id<<"]: "<<numShaders<<" , total: "<<totalNumShaders<< std::endl;
#endif
        OpenGLUtils::CheckError(__LINE__);
        return shader;
    } else {
        SL_DELETE shader;
        OpenGLUtils::CheckError(__LINE__);
        return 0;
    }
}

void Context::ShutdownShaderSystem(void)
{
    SL_VECTOR(Shader*) ::iterator it;
    for (it = shaderList.Get().begin(); it != shaderList.Get().end();) {
        Shader *s = (Shader *)(*it);
        SL_ASSERT(s->context == this);
        SL_DELETE s;
#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
        ScopedMutex scopedMutex(countMutexContainer.mutex);
        --numShaders;
        --totalNumShaders;
#endif
        it = shaderList.Get().erase(it);
    }
#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    SL_ASSERT(numShaders == 0 && shaderList.Get().size() == 0);
    std::cout << "num shaders for context[" << id << "]: " << numShaders << " , total: " << totalNumShaders << std::endl;
#endif
    //shaderList.clear();
    OpenGLUtils::CheckError(__LINE__);
}

const LineShader& Context::GetLineShader(const char* userVertFileName, const char* userFragFileName, ResourceLoader* resourceLoader)
{
    if (!ls.initialized) { // Setup once on first use
        ls.initialized = true;

        ls.sh = LoadShaderFromFile("Shaders/Line.cg", userVertFileName, userFragFileName, false, SL_VECTOR(SL_STRING)(), resourceLoader);
        OpenGLUtils::CheckError(__LINE__);

        if (!ls.sh)
            return ls;

        Shader *s = (Shader *)ls.sh;
        ls.colorUniformLocation = glGetUniformLocation(s->glProgram, "sl_colorConstant");
        ls.modelViewProjUniformLocation = glGetUniformLocation(s->glProgram, "sl_modelViewProj");
        ls.vertexLocation = s->GetVertexAttribLocations().vertexLocation;

        if (ls.colorUniformLocation == -1 || ls.modelViewProjUniformLocation == -1 || ls.vertexLocation == -1) {
            ls.sh = 0;
        }
        OpenGLUtils::CheckError(__LINE__);
    }
    return ls;
}

void Context::SetDepthRange(float zmin, float zmax)
{
    reverseDepth = (zmin > zmax) ? true : false;

    if (OpenGLExtensionManager::usingDepthRangedNV && (zmin < 0 || zmin > 1.0 || zmax < 0 || zmax > 1.0)) {
        stream->glDepthRangedNV(zmin, zmax);
    } else {
        stream->glDepthRange(zmin, zmax);
    }
    stream->checkGlError(__LINE__);
}

bool Context::GetCullClockWise(void) const
{
    return cullClockWise;
}
void Context::SetCullClockWise(bool _cullClockWise)
{
    cullClockWise = _cullClockWise;
}

bool Context::GetIsReverseDepth(void) const
{
    return reverseDepth;
}
void Context::SetIsReverseDepth(bool reverse)
{
    reverseDepth = reverse;
}

void Context::SetDefaultState(void)
{
    stream->glBindVertexArray(0);
    stream->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (SL_Buffer*)0);
    stream->glBindBuffer(GL_ARRAY_BUFFER, (SL_Buffer*)0);

    stream->glDisable(GL_BLEND);
    stream->glEnable(GL_CULL_FACE);
    stream->glEnable(GL_DEPTH_TEST);
    stream->glDisable(GL_DITHER);
    stream->glDisable(GL_COLOR_LOGIC_OP);
    stream->glDisable(GL_POLYGON_OFFSET_LINE);
    stream->glDisable(GL_POLYGON_OFFSET_FILL);
    stream->glDisable(GL_POLYGON_OFFSET_POINT);
    stream->glDisable(GL_POLYGON_SMOOTH);
    stream->glDisable(GL_SCISSOR_TEST);
    stream->glDisable(GL_STENCIL_TEST);

    stream->glFrontFace(GetCullClockWise() ? GL_CCW : GL_CW);

    if (OpenGLExtensionManager::usingMultisample) {
        stream->glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    stream->glDepthFunc(GetIsReverseDepth() ? GL_GEQUAL : GL_LEQUAL);
    stream->checkGlError(__LINE__);
}

void Context::PushAllState(void)
{
    if (!avoidStalls) {
        glState state;

        state.blend = glIsEnabled(GL_BLEND);
        state.depthTest = glIsEnabled(GL_DEPTH_TEST);
        state.cullFace = glIsEnabled(GL_CULL_FACE);
        state.dither = glIsEnabled(GL_DITHER);
        state.colorLogicOp = glIsEnabled(GL_COLOR_LOGIC_OP);
        state.polygonOffsetLine = glIsEnabled(GL_POLYGON_OFFSET_LINE);
        state.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        state.polygonOffsetPoint = glIsEnabled(GL_POLYGON_OFFSET_POINT);
        state.polygonSmooth = glIsEnabled(GL_POLYGON_SMOOTH);
        state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
        state.stencilTest = glIsEnabled(GL_STENCIL_TEST);

        stateStack.push(state);

        OpenGLUtils::CheckError(__LINE__);
    }
}
void Context::PopAllState(void)
{
    if (!avoidStalls) {
        if (!stateStack.empty()) {
            glState state = stateStack.top();

            if (state.blend) glEnable(GL_BLEND);
            else glDisable(GL_BLEND);
            if (state.depthTest) glEnable(GL_DEPTH_TEST);
            else glDisable(GL_DEPTH_TEST);
            if (state.cullFace) glEnable(GL_CULL_FACE);
            else glDisable(GL_CULL_FACE);
            if (state.dither) glEnable(GL_DITHER);
            else glDisable(GL_DITHER);
            if (state.colorLogicOp) glEnable(GL_COLOR_LOGIC_OP);
            else glDisable(GL_COLOR_LOGIC_OP);
            if (state.polygonOffsetFill) glEnable(GL_POLYGON_OFFSET_FILL);
            else glDisable(GL_POLYGON_OFFSET_FILL);
            if (state.polygonOffsetPoint) glEnable(GL_POLYGON_OFFSET_POINT);
            else glDisable(GL_POLYGON_OFFSET_POINT);
            if (state.polygonSmooth) glEnable(GL_POLYGON_SMOOTH);
            else glDisable(GL_POLYGON_SMOOTH);
            if (state.scissorTest) glEnable(GL_SCISSOR_TEST);
            else glDisable(GL_SCISSOR_TEST);
            if (state.stencilTest) glEnable(GL_STENCIL_TEST);
            else glDisable(GL_STENCIL_TEST);

            stateStack.pop();
        }

        OpenGLUtils::CheckError(__LINE__);

        OpenGLUtils::ClearErrors();
    }

    OpenGLUtils::CheckError(__LINE__);
}

void Context::SetViewport(int x, int y, int w, int h)
{
    stream->glViewport(x, y, w, h);
    stream->checkGlError(__LINE__);
}

bool Context::SetIndexBuffer(IndexBufferHandle ibh)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    SL_ASSERT(ibh);
    SL_Buffer* slBuffer = (SL_Buffer*)ibh;
    actualStream->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slBuffer);

    actualStream->checkGlError(__LINE__);

    return true;
}
bool Context::UnsetIndexBuffer()
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    actualStream->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (SL_Buffer*)0);

    actualStream->checkGlError(__LINE__);
    return true;
}

bool Context::SetVertexBuffer(VertexBufferHandle vbh)
{
    if (currentShader == 0) {
        return false;
    }

    SL_Buffer *vb = (SL_Buffer*)vbh;
    GLuint buffNum = (GLuint)(vb->handle);

    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    actualStream->glBindVertexArrayFor(buffNum, currentShader);

    actualStream->checkGlError(__LINE__);

    return true;
}
bool Context::UnsetVertexBuffer()
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();
    actualStream->glBindBuffer(GL_ARRAY_BUFFER, (SL_Buffer*)0);
    actualStream->glBindVertexArray(0);

    actualStream->checkGlError(__LINE__);
    return true;
}

bool Context::BindShader(ShaderHandle shader)
{
    if (!shader) return false;


    Shader* s = (Shader *)shader;
    s->Bind();

    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();
    actualStream->checkGlError(__LINE__);

    currentShader = s;

    return true;
}
bool Context::UnbindShader(void)
{
    if (currentShader) {
        currentShader->Unbind();
    }

    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();
    actualStream->checkGlError(__LINE__);

    currentShader = 0;

    return true;
}
bool Context::DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, bool preDraw)
{
#ifdef WIREFRAME
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

    if (preDraw && currentShader) {
        currentShader->PreDraw();
    }

    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    actualStream->glDrawElements(GL_TRIANGLE_STRIP, nIndices,
                                 sizeof(Index) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                 (GLvoid *)(startIdx * sizeof(Index)));

    actualStream->checkGlError(__LINE__);
    return true;
}

bool Context::DrawMultiStrips(const std::vector<DrawElementsIndirectBindlessCommandNV>& commandBuffer)
{
    SL_ASSERT(currentShader);

    const GLuint vao = vaoManager->GetOrCreateVAOForContext(currentShader->GetVertexAttribLocations());
    currentShader->PreDraw();

    SL_ASSERT(sizeof(unsigned long long) == sizeof(GLuint64));

    glBindVertexArray(vao);

    if (OpenGLExtensionManager::usingCoreProfile) {
        if (drawIndirectBuffer==0) {
            glGenBuffers(1, &drawIndirectBuffer);
        }
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawIndirectBuffer);
        glBufferData(GL_DRAW_INDIRECT_BUFFER, commandBuffer.size() * sizeof(DrawElementsIndirectBindlessCommandNV), commandBuffer.data(), GL_DYNAMIC_DRAW);
        glMultiDrawElementsIndirectBindlessNV(GL_TRIANGLE_STRIP, GL_UNSIGNED_INT, 0, commandBuffer.size(), sizeof(DrawElementsIndirectBindlessCommandNV), 1);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    } else {
        glMultiDrawElementsIndirectBindlessNV(GL_TRIANGLE_STRIP, GL_UNSIGNED_INT, commandBuffer.data(), commandBuffer.size(), sizeof(DrawElementsIndirectBindlessCommandNV), 1);
    }

    glBindVertexArray(0);

    return true;
}

bool Context::DrawStripInstanced(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int count)
{
#ifdef WIREFRAME
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

    if (currentShader) {
        currentShader->PreDraw();
    }

    stream->glDrawElementsInstanced(GL_TRIANGLE_STRIP, nIndices,
                                    sizeof(Index) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                    (GLvoid *)(startIdx * sizeof(Index)), count);

    stream->checkGlError(__LINE__);
    return true;
}
bool Context::DrawPoints(double pointSize, int nPoints, int start)
{
    if (currentShader) {
        currentShader->PreDraw();
    }

    stream->glPointSize((float)pointSize);
    stream->glDrawArrays(GL_POINTS, start, nPoints);

    stream->checkGlError(__LINE__);
    return true;
}

void Context::EnableBackfaceCulling(bool enable)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    if (enable)
        actualStream->glEnable(GL_CULL_FACE);
    else
        actualStream->glDisable(GL_CULL_FACE);

    actualStream->checkGlError(__LINE__);
}
void Context::EnableTexture(TextureHandle tex, int stage)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    const glTexture* texture = (glTexture*)tex;

    actualStream->glActiveTexture(GL_TEXTURE0 + stage);
    actualStream->glBindTexture(GL_TEXTURE_2D, texture->TextureID);

    actualStream->checkGlError(__LINE__);
}

void Context::Enable3DTexture(TextureHandle tex, int stage)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    const glTexture* texture = (glTexture*)tex;

    if (texture) {
        actualStream->glActiveTexture(GL_TEXTURE0 + stage);
        actualStream->glBindTexture(GL_TEXTURE_3D, texture->TextureID);
    }
    actualStream->checkGlError(__LINE__);
}
void Context::DisableTexture(int stage)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    actualStream->glActiveTexture(GL_TEXTURE0);

    actualStream->checkGlError(__LINE__);
}

void Context::SetForceImmediate(bool val)
{
    forceImmediate = val;
}
bool Context::GetForceImmediate(void) const
{
    return forceImmediate;
}

SL_Buffer* Context::GetAnIndexBuffer(int nIndices, const char* name, const BufferProperties& bufferProperties)
{
    if (indexBuffersPool.empty()) {
        SL_Buffer* ib = SL_NEW SL_Buffer(GL_ELEMENT_ARRAY_BUFFER, nIndices * sizeof(Index), GL_DYNAMIC_DRAW, name, bufferProperties);
        return ib;
    }
    SL_Buffer* indexBuffer = indexBuffersPool.front();
    indexBuffer->Respec(nIndices * sizeof(Index));
    indexBuffersPool.pop_front();

    return indexBuffer;
}
void Context::ReturnAnIndexBuffer(SL_Buffer* slBuffer)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();
    slBuffer->DeInitBuffers(actualStream);
    indexBuffersPool.push_back(slBuffer);
}

SL_Buffer* Context::GetAVertexBuffer(int numVertices, const char* name, const BufferProperties& bufferProperties)
{
    if (vertexBuffersPool.empty()) {
        SL_Buffer* vb = SL_NEW SL_Buffer(GL_ARRAY_BUFFER, numVertices * sizeof(Vertex), GL_DYNAMIC_DRAW, name, bufferProperties);
        return vb;
    }
    SL_Buffer* vertexBuffer = vertexBuffersPool.front();
    vertexBuffer->Respec(numVertices * sizeof(Vertex));
    vertexBuffersPool.pop_front();

    return vertexBuffer;
}
void Context::ReturnAVertexBuffer(SL_Buffer* slBuffer)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();
    slBuffer->DeInitBuffers(actualStream);
    vertexBuffersPool.push_back(slBuffer);
}
}