#include "Context.h"
#include "OpenGLImmediateStream.h"
#include "OpenGLDeferredStream.h"
#include "OpenGLExtensionManager.h"
#include "OpenGLUtils.h"
#include "Shader.h"
#include "Vertex.h"
#include "TextureLoader.h"
#include "OpenGLStream.h"

#include <algorithm>

namespace SilverLining
{
unsigned int Context::runningContextId = 0;

Context::Context(bool _useUBOs, bool _useProgramUniforms, bool _avoidStalls, bool _deferred)
    : useUBOs(_useUBOs), useProgramUniforms(_useProgramUniforms), avoidStalls(_avoidStalls), deferred(_deferred)
    , hasProjection(false), hasModelview(false), hasTexture(false), inited(false), pboX(-1), pboY(-1)
    , activeShader(0)
{
    immediateStream = SL_NEW OpenGLImmediateStream(this);

    if (deferred) {
        stream = SL_NEW OpenGLDeferredStream(this);
    } else {
        stream = SL_NEW OpenGLImmediateStream(this);
    }


    if (glClampColorARB) {
        glClampColorARB(GL_CLAMP_VERTEX_COLOR_ARB, GL_FIXED_ONLY_ARB);
        glClampColorARB(GL_CLAMP_FRAGMENT_COLOR_ARB, GL_FIXED_ONLY_ARB);
        glClampColorARB(GL_CLAMP_READ_COLOR_ARB, GL_FIXED_ONLY_ARB);
    }

    for (int i = 0; i < NUMR_PBO; i++) {
        m_pbos[i] = 0;
    }

    contextID = runningContextId++;

    cullClockWise = true;
    reverseDepth = false;

    iMaxClipPlanes = 0;
    glGetIntegerv(GL_MAX_CLIP_PLANES, &iMaxClipPlanes);

    iMaxTextures = 0;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &iMaxTextures);

    if (iMaxTextures > 32) iMaxTextures = 32;
}

ShaderHandle Context::LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling
        , const SL_VECTOR(SL_STRING)& defines
        , ResourceLoader* resourceLoader)
{
    if (OpenGLExtensionManager::usingGLSL) {
        Shader* shader = SL_NEW Shader(this, useUBOs, useProgramUniforms);
        bool ok = shader->LoadShaderFromFile(fileName, userVertShader, userFragShader, enableObjectLabeling, defines, resourceLoader, activeShader, userShaderList);
        if (ok == false) {
            SL_DELETE shader;
            return 0;
        }
        shaderList.Get().push_back(shader);
        return shader;
    }

    return 0;
}

Shader* Context::GetActiveShader(void)
{
    return activeShader;
}

bool Context::BindShader(ShaderHandle shader)
{
    if (!shader) return false;

    Shader *s = (Shader *)shader;

    if (OpenGLExtensionManager::usingGLSL) {
        OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

        if (activeShader != s) {
            s->Bind();
            activeShader = s;
            actualStream->checkGlError(__LINE__);
        }
        return true;
    }

    return false;
}

bool Context::UnbindShader(void)
{
    if (OpenGLExtensionManager::usingGLSL) {
        OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

        if (activeShader) {
            activeShader->Unbind();
        } else {
            actualStream->glUseProgramObjectARB(0);
        }
        activeShader = 0;
        actualStream->checkGlError(__LINE__);
        return true;
    }

    return false;
}

void Context::DeleteShader(ShaderHandle shaderHandle)
{
    if (shaderHandle) {
        Shader *s = (Shader *)shaderHandle;
        if (s->GetContext() == this) {
            SL_DELETE s;
            shaderList.Get().erase(std::remove(shaderList.Get().begin(), shaderList.Get().end(), s), shaderList.Get().end());
        }
    }
}

void Context::ShutdownShaderSystem(void)
{
    SL_VECTOR(Shader*) ::iterator it;

    for (it = shaderList.Get().begin(); it != shaderList.Get().end();) {
        Shader *s = (Shader *)(*it);
        if (s->GetContext() == this) {
            SL_DELETE s;
            it = shaderList.Get().erase(it);
        } else {
            it++;
        }
    }
    //shaderList.clear();
    activeShader = 0;

    if (inited) {
        glDeleteBuffersARB(NUMR_PBO, m_pbos);
    }
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
    activeShader = 0;

    stream->glDisable(GL_ALPHA_TEST);
    stream->glDisable(GL_AUTO_NORMAL);
    stream->glDisable(GL_BLEND);
    stream->glDisable(GL_COLOR_MATERIAL);
    stream->glEnable(GL_CULL_FACE);
    stream->glEnable(GL_DEPTH_TEST);
    stream->glDisable(GL_DITHER);
    stream->glDisable(GL_FOG);
    stream->glDisable(GL_LIGHT0);
    stream->glDisable(GL_LIGHT1);
    stream->glDisable(GL_LIGHT2);
    stream->glDisable(GL_LIGHT3);
    stream->glEnable(GL_LIGHTING);
    stream->glEnable(GL_LINE_SMOOTH);
    stream->glDisable(GL_LINE_STIPPLE);
    stream->glDisable(GL_COLOR_LOGIC_OP);
    stream->glDisable(GL_INDEX_LOGIC_OP);
    stream->glDisable(GL_NORMALIZE);
    stream->glDisable(GL_POINT_SMOOTH);
    stream->glDisable(GL_POLYGON_OFFSET_LINE);
    stream->glDisable(GL_POLYGON_OFFSET_FILL);
    stream->glDisable(GL_POLYGON_OFFSET_POINT);
    stream->glDisable(GL_POLYGON_SMOOTH);
    stream->glDisable(GL_POLYGON_STIPPLE);
    stream->glDisable(GL_SCISSOR_TEST);
    stream->glDisable(GL_STENCIL_TEST);
    stream->glDisable(GL_TEXTURE_1D);
    stream->glDisable(GL_TEXTURE_2D);
    stream->glDisable(GL_TEXTURE_GEN_S);
    stream->glDisable(GL_TEXTURE_GEN_T);
    stream->glDisable(GL_TEXTURE_GEN_R);
    stream->glDisable(GL_TEXTURE_GEN_Q);


    for (int i = 0; i < iMaxClipPlanes; i++) {
        stream->glDisable(GL_CLIP_PLANE0 + i);
    }

    stream->glFrontFace(cullClockWise ? GL_CCW : GL_CW);

    if (OpenGLExtensionManager::usingMultisample) {
        stream->glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE_ARB);
    }

    stream->glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, (SL_Buffer*)0);
    stream->glBindBufferARB(GL_ARRAY_BUFFER_ARB, (SL_Buffer*)0);

    for (int stage = 0; stage < iMaxTextures; stage++) {
        DisableTexture(stage);
    }

    stream->glClientActiveTexture(GL_TEXTURE0_ARB);

    stream->glDisableClientState(GL_NORMAL_ARRAY);
    stream->glDisableClientState(GL_FOG_COORD_ARRAY);
    stream->glDisableClientState(GL_INDEX_ARRAY);
    stream->glDisableClientState(GL_EDGE_FLAG_ARRAY);
    stream->glDisableClientState(GL_SECONDARY_COLOR_ARRAY);

    if (OpenGLExtensionManager::hasBindlessGraphics) {
        stream->glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    }

    if (OpenGLExtensionManager::usingBindlessGraphics) {
#ifdef FLOATING_POINT_COLOR
        stream->glColorFormatNV(4, GL_FLOAT, sizeof(Vertex));
#else
        stream->glColorFormatNV(4, GL_UNSIGNED_BYTE, sizeof(Vertex));
#endif
        stream->glVertexFormatNV(4, GL_FLOAT, sizeof(Vertex));

#ifdef HALF_FLOAT_TEXCOORDS
        stream->glTexCoordFormatNV(4, GL_HALF_FLOAT, sizeof(Vertex));
#else
        stream->glTexCoordFormatNV(4, GL_FLOAT, sizeof(Vertex));
#endif
    }

    stream->glDepthFunc(reverseDepth ? GL_GEQUAL : GL_LEQUAL);
}

void Context::SetViewport(int x, int y, int w, int h)
{
    stream->glViewport(x, y, w, h);
    stream->checkGlError(__LINE__);
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

    glTexture *texture = (glTexture *)tex;
    if (!texture) return;

    actualStream->glActiveTexture(GL_TEXTURE0 + stage);

    actualStream->glEnable(GL_TEXTURE_2D);
    actualStream->glBindTexture(GL_TEXTURE_2D, texture->TextureID);
    actualStream->glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    actualStream->checkGlError(__LINE__);
}

void Context::Enable3DTexture(TextureHandle tex, int stage)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    glTexture *texture = (glTexture *)tex;
    if (!texture) return;

    actualStream->glActiveTexture(GL_TEXTURE0 + stage);

    actualStream->glEnable(GL_TEXTURE_3D);
    actualStream->glBindTexture(GL_TEXTURE_3D, texture->TextureID);
    actualStream->glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    actualStream->checkGlError(__LINE__);
}

void Context::DisableTexture(int stage)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    actualStream->glActiveTexture(GL_TEXTURE0 + stage);

    actualStream->glBindTexture(GL_TEXTURE_2D, 0);
    actualStream->glBindTexture(GL_TEXTURE_3D, 0);
    actualStream->glDisable(GL_TEXTURE_RECTANGLE_NV);
    actualStream->glDisable(GL_TEXTURE_2D);
    actualStream->glDisable(GL_TEXTURE_3D);

    actualStream->glActiveTexture(GL_TEXTURE0);

    actualStream->checkGlError(__LINE__);
}

void Context::InitReadback()
{
    if (!inited) {
        inited = true;

        if (m_pbos[0] == 0)
            glGenBuffersARB(NUMR_PBO, m_pbos);

        for (int i = 0; i < NUMR_PBO; i++) {
            glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, m_pbos[i]);
            glBufferDataARB(GL_PIXEL_PACK_BUFFER_ARB, 4, NULL, GL_STATIC_READ);
        }

        glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, 0);
        vram2sys = 0;
        gpu2vram = NUMR_PBO - 1;
    }
}

void Context::SetDepthRange(float zmin, float zmax)
{
    if (zmin > zmax) {
        reverseDepth = true;
    } else {
        reverseDepth = false;
    }

    if (OpenGLExtensionManager::usingDepthRangedNV && (zmin < 0 || zmin > 1.0 || zmax < 0 || zmax > 1.0)) {
        stream->glDepthRangedNV(zmin, zmax);
    } else {
        stream->glDepthRange(zmin, zmax);
    }
    stream->checkGlError(__LINE__);
}

void Context::SetForceImmediate(bool val)
{
    forceImmediate = val;
}
bool Context::GetForceImmediate(void) const
{
    return forceImmediate;
}

bool Context::SetVertexBuffer(VertexBufferHandle vbh, bool vertexColors)
{
    // Vertex format is xyzwrgbuvst

    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    char *verts;
    SL_Buffer* vb = (SL_Buffer*)vbh;

    if (OpenGLExtensionManager::usingVB) {
        //SL_ASSERT(glIsBufferARB(buffNum) == GL_TRUE);
        actualStream->glBindBufferARB(GL_ARRAY_BUFFER_ARB, vb);
        verts = NULL;
    } else {
        verts = (char *)vbh;
    }

    actualStream->glEnableClientState(GL_VERTEX_ARRAY);

    if (vertexColors) {
        actualStream->glEnableClientState(GL_COLOR_ARRAY);
    } else {
        actualStream->glDisableClientState(GL_COLOR_ARRAY);
    }

    actualStream->glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    GLint texcoordSize;

#ifdef HALF_FLOAT_TEXCOORDS
    texcoordSize = GL_HALF_FLOAT;
#else
    texcoordSize = GL_FLOAT;
#endif

    if (OpenGLExtensionManager::usingBindlessGraphics) {
        actualStream->glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);

        actualStream->glBufferAddressRangeNV(GL_VERTEX_ARRAY_ADDRESS_NV, 0, vb->address, vb->GetSizeInBytes());
        actualStream->glBufferAddressRangeNV(GL_COLOR_ARRAY_ADDRESS_NV, 0, vb->address + 4 * sizeof(float),
                                             vb->GetSizeInBytes() - (4 * sizeof(float)));

#ifdef FLOATING_POINT_COLOR
        actualStream->glBufferAddressRangeNV(GL_TEXTURE_COORD_ARRAY_ADDRESS_NV, 0,
                                             vb->address + 4 * sizeof(float) + 4 * sizeof(float),
                                             vb->GetSizeInBytes() - (4 * sizeof(float) + 4 * sizeof(float)));
#else
        actualStream->glBufferAddressRangeNV(GL_TEXTURE_COORD_ARRAY_ADDRESS_NV, 0,
                                             vb->address + 4 * sizeof(float) + 4 * sizeof(char),
                                             vb->GetSizeInBytes() - (4 * sizeof(float) + 4 * sizeof(char)));
#endif
    } else {
        actualStream->glVertexPointer(4, GL_FLOAT, sizeof(Vertex), verts);
#ifdef FLOATING_POINT_COLOR
        actualStream->glColorPointer(4, GL_FLOAT, sizeof(Vertex), verts + 4 * sizeof(float));
        actualStream->glTexCoordPointer(4, texcoordSize, sizeof(Vertex), verts + 4 * sizeof(float) + 4 * sizeof(float));
#else
        actualStream->glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), verts + 4 * sizeof(float));
        actualStream->glTexCoordPointer(4, texcoordSize, sizeof(Vertex), verts + 4 * sizeof(float) + 4 * sizeof(char));
#endif
    }

    actualStream->checkGlError(__LINE__);
    return true;
}
bool Context::UnsetVertexBuffer()
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    // Vertex format is xyzrgbuvst

    if (OpenGLExtensionManager::usingVB) {
        actualStream->glBindBufferARB(GL_ARRAY_BUFFER_ARB, (SL_Buffer*)0);
    }

    actualStream->glDisableClientState(GL_VERTEX_ARRAY);
    actualStream->glDisableClientState(GL_COLOR_ARRAY);
    actualStream->glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    if (OpenGLExtensionManager::usingBindlessGraphics) {
        actualStream->glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    }

    actualStream->checkGlError(__LINE__);
    return true;
}

bool Context::DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, bool preDraw)
{
    SL_ASSERT(ibh);

    char* indices = (OpenGLExtensionManager::usingVB) ? 0 : (char*)(ibh);

#ifdef WIREFRAME
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

    if (preDraw && GetActiveShader()) {
        GetActiveShader()->PreDraw();
    }

    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    actualStream->glDrawElements(GL_TRIANGLE_STRIP, nIndices,
                                 sizeof(Index) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                 indices + startIdx * sizeof(Index));

    actualStream->checkGlError(__LINE__);
    return true;
}

bool Context::DrawMultiStrips(const std::vector<DrawElementsIndirectBindlessCommandNV>& commandBuffer)
{
    SL_ASSERT(GetActiveShader());
    if (GetActiveShader() == 0) {
        std::cout << "no shader for drawing multi-strips" << std::endl;
        return false;
    }

    SL_ASSERT(sizeof(unsigned long long) == sizeof(GLuint64));

    glBindVertexBuffer(0, 0, 0, sizeof(Vertex));

    glVertexAttribFormat(0, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, x));
#ifdef FLOATING_POINT_COLOR
    glVertexAttribFormat(1, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, r));
#else
    glVertexAttribFormat(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, offsetof(Vertex, color));
#endif

#ifdef HALF_FLOAT_TEXCOORDS
    glVertexAttribFormat(2, 4, GL_HALF_FLOAT, GL_FALSE, offsetof(Vertex, u));
#else
    glVertexAttribFormat(2, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, u));
#endif
    glVertexAttribBinding(0, 0);
    glVertexAttribBinding(1, 0);
    glVertexAttribBinding(2, 0);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glEnableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);

    GetActiveShader()->PreDraw();

    glMultiDrawElementsIndirectBindlessNV(GL_TRIANGLE_STRIP, GL_UNSIGNED_INT, commandBuffer.data(), (GLsizei)commandBuffer.size(), sizeof(DrawElementsIndirectBindlessCommandNV), 1);

    glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glDisableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);

    return true;
}
bool Context::DrawStripInstanced(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int count)
{
    if (GetActiveShader()) {
        GetActiveShader()->PreDraw();
    }

    stream->glDrawElementsInstanced(GL_TRIANGLE_STRIP, nIndices,
                                    sizeof(Index) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                    (GLvoid *)(startIdx * sizeof(Index)), count);

    stream->checkGlError(__LINE__);
    return true;
}
bool Context::DrawPoints(double pointSize, int nPoints, int start)
{
    stream->glEnable(GL_POINT_SMOOTH);
    stream->glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

    if (OpenGLExtensionManager::usingNVPointSprite) {
        stream->glDisable(GL_POINT_SPRITE_NV);
    }

    stream->glPointSize((float)pointSize);

    if (GetActiveShader()) {
        GetActiveShader()->PreDraw();
    }
    stream->glDrawArrays(GL_POINTS, start, nPoints);

    stream->checkGlError(__LINE__);
    return true;
}
bool Context::DrawQuads(int nPoints, int start)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    if (GetActiveShader()) {
        GetActiveShader()->PreDraw();
    }
    actualStream->glDrawArrays(GL_QUADS, start, nPoints);

    actualStream->checkGlError(__LINE__);
    return true;
}

void Context::PushAllState(void)
{
    if (!avoidStalls) {
        OpenGLUtils::ClearErrors();
        glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        glPushAttrib(GL_ALL_ATTRIB_BITS);

        if (glGetHandleARB) {
            GLhandleARB currentProgram = glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
            shaderStack.push(currentProgram);
        }

        if (OpenGLExtensionManager::usingFramebuffers) {
            GLint iFbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &iFbo);
            fboStack.push(iFbo);
        }
        OpenGLUtils::CheckError(__LINE__);
    }
}
void Context::PopAllState(void)
{
    if (!avoidStalls) {
        if (OpenGLExtensionManager::usingFramebuffers && !fboStack.empty()) {
            GLuint savedFbo = fboStack.top();

            glBindFramebuffer(GL_FRAMEBUFFER, savedFbo);

            fboStack.pop();
        }

        if (!shaderStack.empty()) {
            GLhandleARB savedProgram = shaderStack.top();
            if (savedProgram) {
                glUseProgramObjectARB(savedProgram);
            }
            shaderStack.pop();
        }

        glPopAttrib();
        glPopClientAttrib();
        OpenGLUtils::CheckError(__LINE__);
        OpenGLUtils::ClearErrors();
    }
}

bool Context::SetIndexBuffer(IndexBufferHandle ibh)
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    SL_ASSERT(ibh);
    SL_Buffer* slBuffer = (SL_Buffer*)ibh;

    if (OpenGLExtensionManager::usingVB) {
        SL_ASSERT(ibh);
        SL_Buffer* slBuffer = (SL_Buffer*)ibh;
        actualStream->glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, slBuffer);
    }

    actualStream->checkGlError(__LINE__);

    return true;
}
bool Context::UnsetIndexBuffer()
{
    OpenGlStream* actualStream = GetForceImmediate() ? GetImmediateStream() : GetStream();

    if (OpenGLExtensionManager::usingVB) {
        actualStream->glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, (SL_Buffer*)0);
    }

    actualStream->checkGlError(__LINE__);
    return true;
}

SL_Buffer* Context::GetAnIndexBuffer(int nIndices, const char* name, const BufferProperties& bufferProperties)
{
    if (indexBuffersPool.empty()) {
        SL_Buffer* ib = SL_NEW SL_Buffer(GL_ELEMENT_ARRAY_BUFFER_ARB, nIndices * sizeof(Index), GL_DYNAMIC_DRAW_ARB, name, bufferProperties, OpenGLExtensionManager::usingBindlessGraphics);
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
        SL_Buffer* vb = SL_NEW SL_Buffer(GL_ARRAY_BUFFER_ARB, numVertices * sizeof(Vertex), GL_DYNAMIC_DRAW_ARB, name, bufferProperties, OpenGLExtensionManager::usingBindlessGraphics);
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
    indexBuffersPool.push_back(slBuffer);
}
}