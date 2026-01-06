// Copyright (c) 2004-2023  Sundog Software, LLC All rights reserved.

#define SILVERLININGDLL_API extern "C"
#include "Renderer.h"
#include "Atmosphere.h"
#include "Color.h"
#include "SilverLiningDLLCommon.h"
#include "Configuration.h"
#include "Vector4.h"
#include "Profiler.h"
#include "Renderable.h"
#include "MatrixUtils.h"
#include "Camera.h"
#include "MathUtils.h"
#include "OverriddenCameraPos.h"
#include "Mutex.h"
#include "ThreadCameraStreamData.h"
#include "SLAssert.h"

#include <string>
#include <stack>
#include <vector>
#include <algorithm>
#include <math.h>

#if defined(WIN32) || defined(WIN64)
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#endif

using namespace std;


/*
#define FN_TIMER \
char buf[256]; \
sprintf_s(buf, 256, "%d", __LINE__); \
Timer(SL_STRING(buf));
*/

#define FN_TIMER

#if defined(WIN32)
#pragma comment(lib, "winmm.lib")
#endif

#ifdef SILVERLINING_STATIC_RENDERER_DX9
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#endif

#ifdef SILVERLINING_STATIC_RENDERER_DX10
#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "d3dx10.lib")
#endif

#ifdef SILVERLINING_STATIC_RENDERER_DX11
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dx11.lib")
#pragma comment(lib, "d3dx11effects.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3dx10.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#endif

#ifdef SILVERLINING_STATIC_RENDERER_DX11_1
#pragma comment(lib, "d3d11.lib")
#endif

#if (defined(SILVERLINING_STATIC_RENDERER_OPENGL) || defined(SILVERLINING_STATIC_RENDERER_OPENGL32)) && (defined(WIN32) || defined(WIN64))
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#endif

#if (defined(SILVERLINING_STATIC_RENDERER_VULKAN))
// add Vulkan libs
#endif

#if defined(SILVERLINING_STATIC_RENDERER_CUSTOM) || defined(SILVERLINING_STATIC_RENDERER_DX9) \
    || defined(SILVERLINING_STATIC_RENDERER_DX10) || defined(SILVERLINING_STATIC_RENDERER_OPENGL) \
    || defined(SILVERLINING_STATIC_RENDERER_DX11) || defined(SILVERLINING_STATIC_RENDERER_OPENGL32) || defined(ANDROID) || defined(OPENGLES) \
    || defined(SILVERLINING_STATIC_RENDERER_DX11_1) || defined(SILVERLINING_STATIC_RENDERER_VULKAN)
static SET_CONTEXT_PROC SetContextProc = ::SetContext;

static HAS_INSTANCING_PROC HasInstancingProc = ::HasInstancing;
static HAS_BINDLESS_TEXTURES_PROC HasBindlessTexturesProc = ::HasBindlessTextures;
static HAS_UBOS_PROC HasUBOsProc = ::HasUBOs;
static HAS_SSBOS_PROC HasSSBOsProc = ::HasSSBOs;
static HAS_64BIT_SUPPORT Has64BitSupportProc = ::Has64BitSupport;
static HAS_BINDLESS_INDIRECT_RENDERING HasBindlessIndirectRenderingProc = ::HasBindlessIndirectRendering;

static CREATE_BUFFER_PROC CreateBufferProc = ::CreateBuffer;
static DESTROY_BUFFER_PROC DestroyBufferProc = ::DestroyBuffer;
static GET_TEXTURE_ADDRESS_PROC GetTextureAddressProc = GetTextureAddress;
static MAKE_TEXTURE_ADDRESS_RESIDENT_PROC MakeTextureAddressResidentProc = MakeTextureAddressResident;

static GET_BUFFER_ADDRESS_PROC GetBufferAddressProc = ::GetBufferAddress;
static MAKE_BUFFER_RESIDENT_PROC MakeBufferResidentProc = ::MakeBufferResident;
static IS_BUFFER_RESIDENT_PROC IsBufferResidentProc = ::IsBufferResident;

static CLEAR_SCREEN_PROC ClearScreenProc = ::ClearScreen;
static CLEAR_DEPTH_PROC ClearDepthProc = ::ClearDepth;
static LOAD_SHADER_FROM_FILE_PROC LoadShaderFromFileProc = ::LoadShaderFromFile;
static LOAD_SHADER_FROM_SOURCE_PROC LoadShaderFromSourceProc = ::LoadShaderFromSource;
static GET_CONSTANT_LOCATION_PROC GetConstantLocationProc = ::GetConstantLocation;
static SET_CONSTANT_VECTOR4_AT_LOCATION_PROC SetConstantVector4AtLocationProc = ::SetConstantVector4AtLocation;
static SET_CONSTANT_MATRIX4_AT_LOCATION_PROC SetConstantMatrix4AtLocationProc = ::SetConstantMatrix4AtLocation;
static SET_CONSTANT_VECTOR4_PROC SetConstantVector4Proc = ::SetConstantVector4;
static SET_CONSTANT_INT_PROC SetConstantIntProc = ::SetConstantInt;
static SET_CONSTANT_MATRIX4_PROC SetConstantMatrix4Proc = ::SetConstantMatrix4;
static PRE_CONSTANTS_SET_PROC PreConstantsSetProc = ::PreConstantsSet;
static POST_CONSTANTS_SET_PROC PostConstantsSetProc = ::PostConstantsSet;
static BIND_SHADER_PROC BindShaderProc = ::BindShader;
static UNBIND_SHADER_PROC UnbindShaderProc = ::UnbindShader;
static INCREMENTCONSTANTSBUFFEROFFSET_PROC IncrementConstantBuffersOffsetProc = ::IncrementConstantBuffersOffset;
static SHUTDOWN_SHADER_SYSTEM_PROC ShutdownShaderSystemProc = ::ShutdownShaderSystem;
static ALLOCATE_VERTEX_BUFFER_PROC AllocateVertexBufferProc = ::AllocateVertexBuffer;
static LOCK_VERTEX_BUFFER_PROC LockVertexBufferProc = ::LockVertexBuffer;
static GET_VERTICES_PROC GetVerticesProc = ::GetVertices;
static UNLOCK_VERTEX_BUFFER_PROC UnlockVertexBufferProc = ::UnlockVertexBuffer;
static RELEASE_VERTEX_BUFFER_PROC ReleaseVertexBufferProc = ::ReleaseVertexBuffer;
static UPDATE_VERTEX_BUFFER_PROC UpdateVertexBufferProc = ::UpdateVertexBuffer;
static GET_VERTEX_BUFFER_PROC GetVertexBufferProc = ::GetVertexBuffer;
static SET_VERTEX_BUFFER_PROC SetVertexBufferProc = ::SetVertexBuffer;
static UNSET_VERTEX_BUFFER_PROC UnsetVertexBufferProc = ::UnsetVertexBuffer;
static INCREMENT_OFFSET_INDEX_PROC IncrementOffsetIndexProc = ::IncrementOffsetIndex;
static ALLOCATE_INDEX_BUFFER_PROC AllocateIndexBufferProc = ::AllocateIndexBuffer;
static LOCK_INDEX_BUFFER_PROC LockIndexBufferProc = ::LockIndexBuffer;
static GET_INDICES_PROC GetIndicesProc = ::GetIndices;
static UNLOCK_INDEX_BUFFER_PROC UnlockIndexBufferProc = ::UnlockIndexBuffer;
static RELEASE_INDEX_BUFFER_PROC ReleaseIndexBufferProc = ::ReleaseIndexBuffer;
static SET_INDEX_BUFFER_PROC SetIndexBufferProc = ::SetIndexBuffer;
static UNSET_INDEX_BUFFER_PROC UnsetIndexBufferProc = ::UnsetIndexBuffer;
static DRAW_LINE_STRIP_PROC DrawLineStripProc = ::DrawLineStrip;
static DRAW_STRIP_PROC DrawStripProc = ::DrawStrip;
static DRAW_MULTI_STRIPS_PROC DrawMultiStripsProc = ::DrawMultiStrips;
static DRAW_STRIP_INSTANCED_PROC DrawStripInstancedProc = ::DrawStripInstanced;
static DRAW_POINTS_PROC DrawPointsProc = ::DrawPoints;
static DRAW_QUADS_PROC DrawQuadsProc = ::DrawQuads;
static ENABLE_DEPTH_WRITES_PROC EnableDepthWritesProc = ::EnableDepthWrites;
static ENABLE_DEPTH_READS_PROC EnableDepthReadsProc = ::EnableDepthReads;
static ENABLE_TEXTURE_2D_PROC EnableTexture2DProc = ::EnableTexture2D;
static ENABLE_TEXTURE_3D_PROC EnableTexture3DProc = ::EnableTexture3D;
static ENABLE_BACKFACE_CULLING_PROC EnableBackfaceCullingProc = ::EnableBackfaceCulling;
static ENABLE_FOG_PROC EnableFogProc = ::EnableFog;
static ENABLE_LIGHTING_PROC EnableLightingProc = ::EnableLighting;
static GET_TEXTURE_MATRIX_PROC GetTextureMatrixProc = ::GetTextureMatrix;
static SET_TEXTURE_MATRIX_PROC SetTextureMatrixProc = ::SetTextureMatrix;
static SET_PROJECTION_MATRIX_PROC SetProjectionMatrixProc = ::SetProjectionMatrix;
static GET_MODELVIEW_MATRIX_PROC GetModelviewMatrixProc = ::GetModelviewMatrix;
static SET_MODELVIEW_MATRIX_PROC SetModelviewMatrixProc = ::SetModelviewMatrix;
static PUSH_ALL_STATE_PROC PushAllStateProc = ::PushAllState;
static POP_ALL_STATE_PROC PopAllStateProc = ::PopAllState;
static ENABLE_BLENDING_PROC EnableBlendingProc = ::EnableBlending;
static DISABLE_BLENDING_PROC DisableBlendingProc = ::DisableBlending;
static LOAD_TEXTURE_FROM_FILE_PROC LoadTextureFromFileProc = ::LoadTextureFromFile;
static ENABLE_TEXTURE_PROC EnableTextureProc = ::EnableTexture;
static ENABLE_3D_TEXTURE_PROC Enable3DTextureProc = ::Enable3DTexture;
static DISABLE_TEXTURE_PROC DisableTextureProc = ::DisableTexture;
static RELEASE_TEXTURE_PROC ReleaseTextureProc = ::ReleaseTexture;
static HAS_POINT_SPRITES_PROC HasPointSpritesProc = ::HasPointSprites;
static ENABLE_POINT_SPRITES_PROC EnablePointSpritesProc = ::EnablePointSprites;
static DISABLE_POINT_SPRITES_PROC DisablePointSpritesProc = ::DisablePointSprites;
static GET_VIEWPORT_PROC GetViewportProc = ::GetViewport;
static GET_FOV_PROC GetFOVProc = ::GetFOV;
static LOAD_FLOAT_TEXTURE_RGB_PROC LoadFloatTextureRGBProc = ::LoadFloatTextureRGB;
static HAS_FLOAT_TEXTURES_PROC HasFloatTexturesProc = ::HasFloatTextures;
static LOAD_FLOAT_TEXTURE_PROC LoadFloatTextureProc = ::LoadFloatTexture;
static LOAD_TEXTURE_PROC LoadTextureProc = ::LoadTexture;
static LOAD_3D_TEXTURE_PROC Load3DTextureProc = ::Load3DTexture;
static LOAD_3D_TEXTURE_RGB_PROC Load3DTextureRGBProc = ::Load3DTextureRGB;
static LOAD_3D_TEXTURE_LA_PROC Load3DTextureLAProc = ::Load3DTextureLA;
static LOAD_3D_TEXTURE_FLOAT_PROC Load3DTextureFloatProc = ::Load3DTextureFloat;
static SUBLOAD_3D_TEXTURE_LA_PROC SubLoad3DTextureLAProc = ::SubLoad3DTextureLA;
static INIT_RENDER_TARGET_PROC InitRenderTargetProc = ::InitRenderTarget;
static INIT_RENDER_TEXTURE_PROC InitRenderTextureProc = ::InitRenderTexture;
static MAKE_RENDER_TARGET_CURRENT_PROC MakeRenderTargetCurrentProc = ::MakeRenderTargetCurrent;
static MAKE_RENDER_TEXTURE_CURRENT_PROC MakeRenderTextureCurrentProc = ::MakeRenderTextureCurrent;
static RESTORE_RENDER_TARGET_PROC RestoreRenderTargetProc = ::RestoreRenderTarget;
static BIND_RENDER_TEXTURE_PROC BindRenderTextureProc = ::BindRenderTexture;
static GET_RENDER_TEXTURE_TEXTURE_HANDLE_PROC GetRenderTextureTextureHandleProc = ::GetRenderTextureTextureHandle;
static RELEASE_RENDER_TARGET_PROC ReleaseRenderTargetProc = ::ReleaseRenderTarget;
static RELEASE_RENDER_TEXTURE_PROC ReleaseRenderTextureProc = ::ReleaseRenderTexture;
static INIT_RENDER_TEXTURE_CUBE_PROC InitRenderTextureCubeProc = ::InitRenderTextureCube;
static MAKE_RENDER_TEXTURE_CUBE_CURRENT_PROC MakeRenderTextureCubeCurrentProc = ::MakeRenderTextureCubeCurrent;
static BIND_RENDER_TEXTURE_CUBE_PROC BindRenderTextureCubeProc = ::BindRenderTextureCube;
static GET_RENDER_TEXTURE_CUBE_TEXTURE_HANDLE_PROC GetRenderTextureCubeTextureHandleProc = ::GetRenderTextureCubeTextureHandle;
static RELEASE_RENDER_TEXTURE_CUBE_PROC ReleaseRenderTextureCubeProc = ::ReleaseRenderTextureCube;
static GET_PIXELS_PROC GetPixelsProc = ::GetPixels;
static SET_PIXELS_PROC SetPixelsProc = ::SetPixels;
static DRAW_AA_LINE_PROC DrawAALineProc = ::DrawAALine;
static DRAW_AA_LINES_PROC DrawAALinesProc = ::DrawAALines;
static HAS_QUADS_PROC HasQuadsProc = ::HasQuads;
static SET_CURRENT_COLOR_PROC SetCurrentColorProc = ::SetCurrentColor;
static CONFIGURE_FOG_PROC ConfigureFogProc = ::ConfigureFog;
static SET_VIEWPORT_PROC SetViewportProc = ::SetViewport;
static COPY_LUMINANCE_FROM_SCREEN_PROC CopyLuminanceFromScreenProc = ::CopyLuminanceFromScreen;
static CREATE_LUMINANCE_TEXTURE_PROC CreateLuminanceTextureProc = ::CreateLuminanceTexture;
static COPY_LUMINANCE_INTO_TEXTURE_PROC CopyLuminanceIntoTextureProc = ::CopyLuminanceIntoTexture;
static SET_DEFAULT_STATE_PROC SetDefaultStateProc = ::SetDefaultState;
static HEART_BEAT_PROC HeartBeatProc = ::HeartBeat;
static CONTEXT_BEING_DELETED_PROC ContextBeingDeletedProc = ::ContextBeingDeleted;
static START_OCCLUSION_QUERY_PROC StartOcclusionQueryProc = ::StartOcclusionQuery;
static END_OCCLUSION_QUERY_PROC EndOcclusionQueryProc = ::EndOcclusionQuery;
static GET_OCCLUSION_QUERY_RESULT_PROC GetOcclusionQueryResultProc = ::GetOcclusionQueryResult;
static SET_DEPTH_RANGE_PROC SetDepthRangeProc = ::SetDepthRange;
static GET_DEPTH_RANGE_PROC GetDepthRangeProc = ::GetDepthRange;
static DEVICE_LOST_PROC DeviceLostProc = ::DeviceLost;
static DEVICE_RESET_PROC DeviceResetProc = ::DeviceReset;
static GET_NATIVE_TEXTURE_PROC GetNativeTextureProc = ::GetNativeTexture;
static SET_USER_SHADERS_PROC SetUserShadersProc = ::SetUserShaders;
static GET_SHADER_PROGRAM_OBJECT_PROC GetShaderProgramObjectProc = ::GetShaderProgramObject;
static DELETE_SHADER_PROC DeleteShaderProc = ::DeleteShader;
static BACKFACE_CULL_CLOCKWISE_PROC BackfaceCullClockwiseProc = ::BackfaceCullClockwise;
static SET_REVERSE_Z_PROC SetReverseZProc = ::SetReverseZ;
static GET_LINE_SHADER_PROC GetLineShaderProc = ::GetLineShader;
static SET_FORCE_IMMEDIATE_PROC SetForceImmediateProc = ::SetForceImmediate;
static GET_FORCE_IMMEDIATE_PROC GetForceImmediateProc = ::GetForceImmediate;
static EXECUTE_STREAM_PROC ExecuteStreamProc = ::ExecuteStream;
static SET_STREAM_PROC SetStreamProc = ::SetStream;
static SETTINGS_CHANGED_PROC SettingsChangedProc = ::SettingsChanged;

#else

static HINSTANCE dll = 0;

static SET_CONTEXT_PROC SetContextProc = 0;

static HAS_INSTANCING_PROC HasInstancingProc = 0;
static HAS_BINDLESS_TEXTURES_PROC HasBindlessTexturesProc = 0;
static HAS_UBOS_PROC HasUBOsProc = 0;
static HAS_SSBOS_PROC HasSSBOsProc = 0;
static HAS_64BIT_SUPPORT Has64BitSupportProc = 0;
static HAS_BINDLESS_INDIRECT_RENDERING HasBindlessIndirectRenderingProc = 0;

static CREATE_BUFFER_PROC CreateBufferProc = 0;
static DESTROY_BUFFER_PROC DestroyBufferProc = 0;
static GET_TEXTURE_ADDRESS_PROC GetTextureAddressProc = 0;
static MAKE_TEXTURE_ADDRESS_RESIDENT_PROC MakeTextureAddressResidentProc = 0;

static GET_BUFFER_ADDRESS_PROC GetBufferAddressProc = 0;
static MAKE_BUFFER_RESIDENT_PROC MakeBufferResidentProc = 0;
static IS_BUFFER_RESIDENT_PROC IsBufferResidentProc = 0;

static CLEAR_SCREEN_PROC ClearScreenProc = 0;
static CLEAR_DEPTH_PROC ClearDepthProc = 0;
static SET_ENV_PROC SetEnvProc = 0;
static LOAD_SHADER_FROM_FILE_PROC LoadShaderFromFileProc = 0;
static LOAD_SHADER_FROM_SOURCE_PROC LoadShaderFromSourceProc = 0;
static GET_CONSTANT_LOCATION_PROC GetConstantLocationProc = 0;
static SET_CONSTANT_VECTOR4_AT_LOCATION_PROC SetConstantVector4AtLocationProc = 0;
static SET_CONSTANT_MATRIX4_AT_LOCATION_PROC SetConstantMatrix4AtLocationProc = 0;
static SET_CONSTANT_VECTOR4_PROC SetConstantVector4Proc = 0;
static SET_CONSTANT_INT_PROC SetConstantIntProc = 0;
static SET_CONSTANT_MATRIX4_PROC SetConstantMatrix4Proc = 0;
static PRE_CONSTANTS_SET_PROC PreConstantsSetProc = 0;
static POST_CONSTANTS_SET_PROC PostConstantsSetProc = 0;
static BIND_SHADER_PROC BindShaderProc = 0;
static UNBIND_SHADER_PROC UnbindShaderProc = 0;
static INCREMENTCONSTANTSBUFFEROFFSET_PROC IncrementConstantBuffersOffsetProc = 0;
static SHUTDOWN_SHADER_SYSTEM_PROC ShutdownShaderSystemProc = 0;
static ALLOCATE_VERTEX_BUFFER_PROC AllocateVertexBufferProc = 0;
static LOCK_VERTEX_BUFFER_PROC LockVertexBufferProc = 0;
static GET_VERTICES_PROC GetVerticesProc = 0;
static UNLOCK_VERTEX_BUFFER_PROC UnlockVertexBufferProc = 0;
static RELEASE_VERTEX_BUFFER_PROC ReleaseVertexBufferProc = 0;
static UPDATE_VERTEX_BUFFER_PROC UpdateVertexBufferProc = 0;
static GET_VERTEX_BUFFER_PROC GetVertexBufferProc = 0;
static SET_VERTEX_BUFFER_PROC SetVertexBufferProc = 0;
static UNSET_VERTEX_BUFFER_PROC UnsetVertexBufferProc = 0;
static INCREMENT_OFFSET_INDEX_PROC IncrementOffsetIndexProc = 0;
static ALLOCATE_INDEX_BUFFER_PROC AllocateIndexBufferProc = 0;
static LOCK_INDEX_BUFFER_PROC LockIndexBufferProc = 0;
static GET_INDICES_PROC GetIndicesProc = 0;
static UNLOCK_INDEX_BUFFER_PROC UnlockIndexBufferProc = 0;
static RELEASE_INDEX_BUFFER_PROC ReleaseIndexBufferProc = 0;
static SET_INDEX_BUFFER_PROC SetIndexBufferProc = 0;
static UNSET_INDEX_BUFFER_PROC UnsetIndexBufferProc = 0;
static DRAW_LINE_STRIP_PROC DrawLineStripProc = 0;
static DRAW_STRIP_PROC DrawStripProc = 0;
static DRAW_MULTI_STRIPS_PROC DrawMultiStripsProc = 0;
static DRAW_STRIP_INSTANCED_PROC DrawStripInstancedProc = 0;
static DRAW_POINTS_PROC DrawPointsProc = 0;
static DRAW_QUADS_PROC DrawQuadsProc = 0;
static ENABLE_DEPTH_WRITES_PROC EnableDepthWritesProc = 0;
static ENABLE_DEPTH_READS_PROC EnableDepthReadsProc = 0;
static ENABLE_TEXTURE_2D_PROC EnableTexture2DProc = 0;
static ENABLE_TEXTURE_3D_PROC EnableTexture3DProc = 0;
static ENABLE_BACKFACE_CULLING_PROC EnableBackfaceCullingProc = 0;
static ENABLE_FOG_PROC EnableFogProc = 0;
static ENABLE_LIGHTING_PROC EnableLightingProc = 0;
static GET_TEXTURE_MATRIX_PROC GetTextureMatrixProc = 0;
static SET_TEXTURE_MATRIX_PROC SetTextureMatrixProc = 0;
static SET_PROJECTION_MATRIX_PROC SetProjectionMatrixProc = 0;
static GET_MODELVIEW_MATRIX_PROC GetModelviewMatrixProc = 0;
static SET_MODELVIEW_MATRIX_PROC SetModelviewMatrixProc = 0;
static PUSH_ALL_STATE_PROC PushAllStateProc = 0;
static POP_ALL_STATE_PROC PopAllStateProc = 0;
static ENABLE_BLENDING_PROC EnableBlendingProc = 0;
static DISABLE_BLENDING_PROC DisableBlendingProc = 0;
static LOAD_TEXTURE_FROM_FILE_PROC LoadTextureFromFileProc = 0;
static ENABLE_TEXTURE_PROC EnableTextureProc = 0;
static ENABLE_3D_TEXTURE_PROC Enable3DTextureProc = 0;
static DISABLE_TEXTURE_PROC DisableTextureProc = 0;
static RELEASE_TEXTURE_PROC ReleaseTextureProc = 0;
static HAS_POINT_SPRITES_PROC HasPointSpritesProc = 0;
static ENABLE_POINT_SPRITES_PROC EnablePointSpritesProc = 0;
static DISABLE_POINT_SPRITES_PROC DisablePointSpritesProc = 0;
static GET_VIEWPORT_PROC GetViewportProc = 0;
static GET_FOV_PROC GetFOVProc = 0;
static LOAD_FLOAT_TEXTURE_RGB_PROC LoadFloatTextureRGBProc = 0;
static HAS_FLOAT_TEXTURES_PROC HasFloatTexturesProc = 0;
static LOAD_FLOAT_TEXTURE_PROC LoadFloatTextureProc = 0;
static LOAD_TEXTURE_PROC LoadTextureProc = 0;
static LOAD_3D_TEXTURE_PROC Load3DTextureProc = 0;
static LOAD_3D_TEXTURE_RGB_PROC Load3DTextureRGBProc = 0;
static LOAD_3D_TEXTURE_LA_PROC Load3DTextureLAProc = 0;
static SUBLOAD_3D_TEXTURE_LA_PROC SubLoad3DTextureLAProc = 0;
static LOAD_3D_TEXTURE_FLOAT_PROC Load3DTextureFloatProc = 0;
static INIT_RENDER_TARGET_PROC InitRenderTargetProc = 0;
static INIT_RENDER_TEXTURE_PROC InitRenderTextureProc = 0;
static MAKE_RENDER_TARGET_CURRENT_PROC MakeRenderTargetCurrentProc = 0;
static MAKE_RENDER_TEXTURE_CURRENT_PROC MakeRenderTextureCurrentProc = 0;
static RESTORE_RENDER_TARGET_PROC RestoreRenderTargetProc = 0;
static BIND_RENDER_TEXTURE_PROC BindRenderTextureProc = 0;
static GET_RENDER_TEXTURE_TEXTURE_HANDLE_PROC GetRenderTextureTextureHandleProc = 0;
static RELEASE_RENDER_TARGET_PROC ReleaseRenderTargetProc = 0;
static RELEASE_RENDER_TEXTURE_PROC ReleaseRenderTextureProc = 0;
static INIT_RENDER_TEXTURE_CUBE_PROC InitRenderTextureCubeProc = 0;
static MAKE_RENDER_TEXTURE_CUBE_CURRENT_PROC MakeRenderTextureCubeCurrentProc = 0;
static BIND_RENDER_TEXTURE_CUBE_PROC BindRenderTextureCubeProc = 0;
static GET_RENDER_TEXTURE_CUBE_TEXTURE_HANDLE_PROC GetRenderTextureCubeTextureHandleProc = 0;
static RELEASE_RENDER_TEXTURE_CUBE_PROC ReleaseRenderTextureCubeProc = 0;
static GET_PIXELS_PROC GetPixelsProc = 0;
static SET_PIXELS_PROC SetPixelsProc = 0;
static DRAW_AA_LINE_PROC DrawAALineProc = 0;
static DRAW_AA_LINES_PROC DrawAALinesProc = 0;
static HAS_QUADS_PROC HasQuadsProc = 0;
static SET_CURRENT_COLOR_PROC SetCurrentColorProc = 0;
static CONFIGURE_FOG_PROC ConfigureFogProc = 0;
static SET_VIEWPORT_PROC SetViewportProc = 0;
static COPY_LUMINANCE_FROM_SCREEN_PROC CopyLuminanceFromScreenProc = 0;
static CREATE_LUMINANCE_TEXTURE_PROC CreateLuminanceTextureProc = 0;
static COPY_LUMINANCE_INTO_TEXTURE_PROC CopyLuminanceIntoTextureProc = 0;
static SET_DEFAULT_STATE_PROC SetDefaultStateProc = 0;
static HEART_BEAT_PROC HeartBeatProc = 0;
static CONTEXT_BEING_DELETED_PROC ContextBeingDeletedProc = 0;
static START_OCCLUSION_QUERY_PROC StartOcclusionQueryProc = 0;
static END_OCCLUSION_QUERY_PROC EndOcclusionQueryProc = 0;
static GET_OCCLUSION_QUERY_RESULT_PROC GetOcclusionQueryResultProc = 0;
static SET_DEPTH_RANGE_PROC SetDepthRangeProc = 0;
static GET_DEPTH_RANGE_PROC GetDepthRangeProc = 0;
static DEVICE_LOST_PROC DeviceLostProc = 0;
static DEVICE_RESET_PROC DeviceResetProc = 0;
static GET_NATIVE_TEXTURE_PROC GetNativeTextureProc = 0;
static SET_USER_SHADERS_PROC SetUserShadersProc = 0;
static GET_SHADER_PROGRAM_OBJECT_PROC GetShaderProgramObjectProc = 0;
static DELETE_SHADER_PROC DeleteShaderProc = 0;
static BACKFACE_CULL_CLOCKWISE_PROC BackfaceCullClockwiseProc = 0;
static SET_REVERSE_Z_PROC SetReverseZProc = 0;
static GET_LINE_SHADER_PROC GetLineShaderProc = 0;
static SET_FORCE_IMMEDIATE_PROC SetForceImmediateProc = 0;
static GET_FORCE_IMMEDIATE_PROC GetForceImmediateProc = 0;
static EXECUTE_STREAM_PROC ExecuteStreamProc = 0;
static SET_STREAM_PROC SetStreamProc = 0;
static SETTINGS_CHANGED_PROC SettingsChangedProc = 0;

#endif

namespace SilverLining
{
class RendererRenderableListener : public RenderableListener
{
public:
    RendererRenderableListener(Renderer* _renderer)
        : renderer(_renderer)
    {

    }
    virtual void renderableDeleted(Renderable* obj)
    {
        renderer->RemoveBlendedObject(obj);
    }
    Renderer* renderer;
};

MutexContainer Renderer::internalMutexContainer;
int Renderer::refCount = 0;
bool Renderer::triedToLoadDll = false;
bool Renderer::procsInitialized = false;

Renderer::Renderer(const ThreadCameraStreamData& _tcsData) : blendingEnabled(false), lastSrc(-1), lastDst(-1), lastTex(0), lastShader(0),
    fogEnabled(false), rightHanded(true), disableDepthReads(false), disableDepthWrites(false),
    hasExplicitMatrices(false), hasExplicitViewport(false), cullingUndefined(true), cullingEnabled(false),
    fogDensity(1E-9), fogStart(0), fogEnd(1E9), hasExplicitDepthRange(false), minZ(0.0f), maxZ(1.0f),
    alwaysApplyState(false), type(Atmosphere::OPENGL32CORE)
    , tcsData(_tcsData)
    , avoidStalls(false)
{
    listener = SL_NEW RendererRenderableListener(this);

    ScopedMutex scopedMutex(internalMutexContainer.mutex);
    ++refCount;

}

Renderer::~Renderer()
{
    ScopedMutex scopedMutex(internalMutexContainer.mutex);
    --refCount;

    SL_DELETE listener;
#if !(defined SILVERLINING_STATIC_RENDERER_CUSTOM || defined SILVERLINING_STATIC_RENDERER_DX9 || \
    defined SILVERLINING_STATIC_RENDERER_DX10 || defined SILVERLINING_STATIC_RENDERER_OPENGL || \
    defined SILVERLINING_STATIC_RENDERER_DX11 || defined SILVERLINING_STATIC_RENDERER_OPENGL32 || \
    defined ANDROID || defined OPENGLES || defined SILVERLINING_STATIC_RENDERER_DX11_1 || \
    defined SILVERLINING_STATIC_RENDERER_VULKAN)
    if (refCount == 0 && dll) {
        FreeLibrary(dll);
        dll = 0;

        triedToLoadDll = false;
        procsInitialized = false;
    }
#endif
}

int Renderer::Initialize(int renderer, bool rightHanded, void *environment, void **contextOut, bool _avoidStalls, bool deferred, const SL_VECTOR(unsigned int)& userShaders)
{
    type = renderer;
    this->rightHanded = rightHanded;

    avoidStalls = _avoidStalls;

    alwaysApplyState = false;
    Configuration::GetBoolValue("always-apply-state", alwaysApplyState);

    bool useUBOs = false;
    Configuration::GetBoolValue("use-ubos", useUBOs);

#if (defined SILVERLINING_STATIC_RENDERER_DX9 || defined SILVERLINING_STATIC_RENDERER_DX10 || \
    defined SILVERLINING_STATIC_RENDERER_DX11 || defined SILVERLINING_STATIC_RENDERER_OPENGL || \
    defined SILVERLINING_STATIC_RENDERER_CUSTOM || defined SILVERLINING_STATIC_RENDERER_OPENGL32 || \
    defined ANDROID || defined OPENGLES || defined SILVERLINING_STATIC_RENDERER_DX11_1 || defined SILVERLINING_STATIC_RENDERER_VULKAN)

    const char *userVert = 0, *userFrag = 0;

    if (type == Atmosphere::OPENGL) {
        Configuration::GetStringValue("user-vert-shader-file-opengl", userVert);
        Configuration::GetStringValue("user-frag-shader-file-opengl", userFrag);
    }

    if (type == Atmosphere::OPENGL32CORE) {
        Configuration::GetStringValue("user-vert-shader-file-opengl32", userVert);
        Configuration::GetStringValue("user-frag-shader-file-opengl32", userFrag);
    }

    if (type == Atmosphere::VULKAN) {
        // TBD: Do user shaders even make sense in Vulkan, given they must be precompiled?
        // We may have to execute the compiler at runtime
        Configuration::GetStringValue("user-vert-shader-file-vulkan", userVert);
        Configuration::GetStringValue("user-frag-shader-file-vulkan", userFrag);
    }

    *contextOut = SetEnvironment(rightHanded, environment, Atmosphere::GetResourceLoader(), Allocator::GetAllocator(),
                                 Atmosphere::GetUserDefinedVertString(), Atmosphere::GetUserDefinedFragString(), avoidStalls,
                                 userVert, userFrag, useUBOs, deferred, userShaders);

    context = *contextOut;

    if (!*contextOut) {
        return Atmosphere::E_CANT_INITIALIZE_RENDERER_SUBSYSTEM;
    }

    FrameStarted(*contextOut);

    return Atmosphere::E_NOERROR;

#else
    ScopedMutex scopedMutex(internalMutexContainer.mutex);
    if (dll == 0 && triedToLoadDll == false) {
        //#ifdef _DEBUG
        //    SL_STRING dllName = "c:\\sl\\dll\\Debug\\";
        //#else
        SL_STRING dllName = Atmosphere::GetResourceLoader()->GetRendererDLLDirPath();

        //#endif

        switch (renderer) {
#ifdef SINGLE_THREADED
        case Atmosphere::OPENGL:
            dllName += "SilverLiningOpenGL-ST.dll";
            break;

        case Atmosphere::OPENGL32CORE:
            dllName += "SilverLiningOpenGL32-ST.dll";
            break;

        case Atmosphere::DIRECTX9:
            dllName += "SilverLiningDirectX9-ST.dll";
            break;

        case Atmosphere::DIRECTX11:
            dllName += "SilverLiningDirectX11-ST.dll";
            break;

        case Atmosphere::DIRECTX11_1:
            dllName += "SilverLiningDirectX11_1-ST.dll";
            break;

        case Atmosphere::VULKAN:
            dllName += "SilverLiningVulkan-ST.dll";
            break;

#endif

#ifdef MULTI_THREADED
        case Atmosphere::OPENGL:
            dllName += "SilverLiningOpenGL-MT.dll";
            break;

        case Atmosphere::OPENGL32CORE:
            dllName += "SilverLiningOpenGL32-MT.dll";
            break;

        case Atmosphere::DIRECTX9:
            dllName += "SilverLiningDirectX9-MT.dll";
            break;

        case Atmosphere::DIRECTX11:
            dllName += "SilverLiningDirectX11-MT.dll";
            break;

        case Atmosphere::DIRECTX11_1:
            dllName += "SilverLiningDirectX11_1-MT.dll";
            break;

        case Atmosphere::VULKAN:
            dllName += "SilverLiningVulkan-MT.dll";
            break;
#endif

#ifdef SINGLE_THREADED_DEBUG
        case Atmosphere::OPENGL:
            dllName += "SilverLiningOpenGL-STD.dll";
            break;

        case Atmosphere::OPENGL32CORE:
            dllName += "SilverLiningOpenGL32-STD.dll";
            break;

        case Atmosphere::DIRECTX9:
            dllName += "SilverLiningDirectX9-STD.dll";
            break;

        case Atmosphere::DIRECTX11:
            dllName += "SilverLiningDirectX11-STD.dll";
            break;

        case Atmosphere::DIRECTX11_1:
            dllName += "SilverLiningDirectX11_1-STD.dll";
            break;

        case Atmosphere::VULKAN:
            dllName += "SilverLiningVulkan-STD.dll";
            break;
#endif

#ifdef MULTI_THREADED_DEBUG
        case Atmosphere::OPENGL:
            dllName += "SilverLiningOpenGL-MTD.dll";
            break;

        case Atmosphere::OPENGL32CORE:
            dllName += "SilverLiningOpenGL32-MTD.dll";
            break;

        case Atmosphere::DIRECTX9:
            dllName += "SilverLiningDirectX9-MTD.dll";
            break;

        case Atmosphere::DIRECTX11:
            dllName += "SilverLiningDirectX11-MTD.dll";
            break;

        case Atmosphere::DIRECTX11_1:
            dllName += "SilverLiningDirectX11_1-MTD.dll";
            break;

        case Atmosphere::VULKAN:
            dllName += "SilverLiningVulkan-MTD.dll";
            break;
#endif

#ifdef MULTI_THREADED_DEBUG_DLL
        case Atmosphere::OPENGL:
            dllName += "SilverLiningOpenGL-MTD-DLL.dll";
            break;

        case Atmosphere::OPENGL32CORE:
            dllName += "SilverLiningOpenGL32-MTD-DLL.dll";
            break;

        case Atmosphere::DIRECTX9:
            dllName += "SilverLiningDirectX9-MTD-DLL.dll";
            break;

        case Atmosphere::DIRECTX11:
            dllName += "SilverLiningDirectX11-MTD-DLL.dll";
            break;

        case Atmosphere::DIRECTX11_1:
            dllName += "SilverLiningDirectX11_1-MTD-DLL.dll";
            break;

        case Atmosphere::VULKAN:
            dllName += "SilverLiningVulkan-MTD-DLL.dll";
            break;
#endif

#ifdef MULTI_THREADED_DLL
        case Atmosphere::OPENGL:
            dllName += "SilverLiningOpenGL-MT-DLL.dll";
            break;

        case Atmosphere::OPENGL32CORE:
            dllName += "SilverLiningOpenGL32-MT-DLL.dll";
            break;

        case Atmosphere::DIRECTX9:
            dllName += "SilverLiningDirectX9-MT-DLL.dll";
            break;

        case Atmosphere::DIRECTX11:
            dllName += "SilverLiningDirectX11-MT-DLL.dll";
            break;

        case Atmosphere::DIRECTX11_1:
            dllName += "SilverLiningDirectX11_1-MT-DLL.dll";
            break;

        case Atmosphere::VULKAN:
            dllName += "SilverLiningVulkan-MT-DLL.dll";
            break;
#endif

        default:
            return Atmosphere::E_CANT_LOAD_RENDERER_DLL;
        }

        dll = LoadLibraryA(dllName.c_str());
        triedToLoadDll = true;
    }

    if (dll) {
        if (procsInitialized == false) {
            SetContextProc = (SET_CONTEXT_PROC)GetProcAddress(dll, "SetContext");

            HasInstancingProc = (HAS_INSTANCING_PROC)GetProcAddress(dll, "HasInstancing");
            HasBindlessTexturesProc = (HAS_BINDLESS_TEXTURES_PROC)GetProcAddress(dll, "HasBindlessTextures");
            HasUBOsProc = (HAS_UBOS_PROC)GetProcAddress(dll, "HasUBOs");
            HasSSBOsProc = (HAS_SSBOS_PROC)GetProcAddress(dll, "HasSSBOs");
            Has64BitSupportProc = (HAS_64BIT_SUPPORT)GetProcAddress(dll, "Has64BitSupport");
            HasBindlessIndirectRenderingProc = (HAS_BINDLESS_INDIRECT_RENDERING)GetProcAddress(dll, "HasBindlessIndirectRendering");

            CreateBufferProc = (CREATE_BUFFER_PROC)GetProcAddress(dll, "CreateBuffer");
            DestroyBufferProc = (DESTROY_BUFFER_PROC)GetProcAddress(dll, "DestroyBuffer");
            GetTextureAddressProc = (GET_TEXTURE_ADDRESS_PROC)GetProcAddress(dll, "GetTextureAddress");
            MakeTextureAddressResidentProc = (MAKE_TEXTURE_ADDRESS_RESIDENT_PROC)GetProcAddress(dll, "MakeTextureAddressResident");

            GetBufferAddressProc = (GET_BUFFER_ADDRESS_PROC)GetProcAddress(dll, "GetBufferAddress");
            MakeBufferResidentProc = (MAKE_BUFFER_RESIDENT_PROC)GetProcAddress(dll, "MakeBufferResident");
            IsBufferResidentProc = (IS_BUFFER_RESIDENT_PROC)GetProcAddress(dll, "IsBufferResident");

            ClearScreenProc = (CLEAR_SCREEN_PROC)GetProcAddress(dll, "ClearScreen");
            ClearDepthProc = (CLEAR_DEPTH_PROC)GetProcAddress(dll, "ClearDepth");
            SetEnvProc = (SET_ENV_PROC)GetProcAddress(dll, "SetEnvironment");
            LoadShaderFromFileProc = (LOAD_SHADER_FROM_FILE_PROC)GetProcAddress(dll, "LoadShaderFromFile");
            LoadShaderFromSourceProc = (LOAD_SHADER_FROM_SOURCE_PROC)GetProcAddress(dll, "LoadShaderFromSource");
            GetConstantLocationProc = (GET_CONSTANT_LOCATION_PROC)GetProcAddress(dll, "GetConstantLocation");
            SetConstantVector4AtLocationProc = (SET_CONSTANT_VECTOR4_AT_LOCATION_PROC)GetProcAddress(dll, "SetConstantVector4AtLocation");
            SetConstantMatrix4AtLocationProc = (SET_CONSTANT_MATRIX4_AT_LOCATION_PROC)GetProcAddress(dll, "SetConstantMatrix4AtLocation");
            SetConstantVector4Proc = (SET_CONSTANT_VECTOR4_PROC)GetProcAddress(dll, "SetConstantVector4");
            SetConstantIntProc = (SET_CONSTANT_INT_PROC)GetProcAddress(dll, "SetConstantInt");
            SetConstantMatrix4Proc = (SET_CONSTANT_MATRIX4_PROC)GetProcAddress(dll, "SetConstantMatrix4");
            PreConstantsSetProc = (PRE_CONSTANTS_SET_PROC)GetProcAddress(dll, "PreConstantsSet");
            PostConstantsSetProc = (POST_CONSTANTS_SET_PROC)GetProcAddress(dll, "PostConstantsSet");
            BindShaderProc = (BIND_SHADER_PROC)GetProcAddress(dll, "BindShader");
            UnbindShaderProc = (UNBIND_SHADER_PROC)GetProcAddress(dll, "UnbindShader");
            IncrementConstantBuffersOffsetProc = (INCREMENTCONSTANTSBUFFEROFFSET_PROC)GetProcAddress(dll, "IncrementConstantBuffersOffset");
            ShutdownShaderSystemProc = (SHUTDOWN_SHADER_SYSTEM_PROC)GetProcAddress(dll, "ShutdownShaderSystem");
            AllocateVertexBufferProc = (ALLOCATE_VERTEX_BUFFER_PROC)GetProcAddress(dll, "AllocateVertexBuffer");
            LockVertexBufferProc = (LOCK_VERTEX_BUFFER_PROC)GetProcAddress(dll, "LockVertexBuffer");
            GetVerticesProc = (GET_VERTICES_PROC)GetProcAddress(dll, "GetVertices");
            UnlockVertexBufferProc = (UNLOCK_VERTEX_BUFFER_PROC)GetProcAddress(dll, "UnlockVertexBuffer");
            GetVertexBufferProc = (GET_VERTEX_BUFFER_PROC)GetProcAddress(dll, "GetVertexBuffer");
            UpdateVertexBufferProc = (UPDATE_VERTEX_BUFFER_PROC)GetProcAddress(dll, "UpdateVertexBuffer");
            ReleaseVertexBufferProc = (RELEASE_VERTEX_BUFFER_PROC)GetProcAddress(dll, "ReleaseVertexBuffer");
            SetVertexBufferProc = (SET_VERTEX_BUFFER_PROC)GetProcAddress(dll, "SetVertexBuffer");
            UnsetVertexBufferProc = (UNSET_VERTEX_BUFFER_PROC)GetProcAddress(dll, "UnsetVertexBuffer");
            IncrementOffsetIndexProc = (INCREMENT_OFFSET_INDEX_PROC)GetProcAddress(dll, "IncrementOffsetIndex");
            AllocateIndexBufferProc = (ALLOCATE_INDEX_BUFFER_PROC)GetProcAddress(dll, "AllocateIndexBuffer");
            LockIndexBufferProc = (LOCK_INDEX_BUFFER_PROC)GetProcAddress(dll, "LockIndexBuffer");
            GetIndicesProc = (GET_INDICES_PROC)GetProcAddress(dll, "GetIndices");
            UnlockIndexBufferProc = (UNLOCK_INDEX_BUFFER_PROC)GetProcAddress(dll, "UnlockIndexBuffer");
            ReleaseIndexBufferProc = (RELEASE_INDEX_BUFFER_PROC)GetProcAddress(dll, "ReleaseIndexBuffer");
            SetIndexBufferProc = (SET_INDEX_BUFFER_PROC)GetProcAddress(dll, "SetIndexBuffer");
            UnsetIndexBufferProc = (UNSET_INDEX_BUFFER_PROC)GetProcAddress(dll, "UnsetIndexBuffer");
            DrawLineStripProc = (DRAW_LINE_STRIP_PROC)GetProcAddress(dll, "DrawLineStrip");
            DrawStripProc = (DRAW_STRIP_PROC)GetProcAddress(dll, "DrawStrip");
            DrawMultiStripsProc = (DRAW_MULTI_STRIPS_PROC)GetProcAddress(dll, "DrawMultiStrips");
            DrawStripInstancedProc = (DRAW_STRIP_INSTANCED_PROC)GetProcAddress(dll, "DrawStripInstanced");
            DrawPointsProc = (DRAW_POINTS_PROC)GetProcAddress(dll, "DrawPoints");
            EnableDepthWritesProc = (ENABLE_DEPTH_WRITES_PROC)GetProcAddress(dll, "EnableDepthWrites");
            EnableDepthReadsProc = (ENABLE_DEPTH_WRITES_PROC)GetProcAddress(dll, "EnableDepthReads");
            EnableTexture2DProc = (ENABLE_TEXTURE_2D_PROC)GetProcAddress(dll, "EnableTexture2D");
            EnableTexture3DProc = (ENABLE_TEXTURE_3D_PROC)GetProcAddress(dll, "EnableTexture3D");
            EnableBackfaceCullingProc = (ENABLE_BACKFACE_CULLING_PROC)GetProcAddress(dll, "EnableBackfaceCulling");
            EnableFogProc = (ENABLE_FOG_PROC)GetProcAddress(dll, "EnableFog");
            EnableLightingProc = (ENABLE_LIGHTING_PROC)GetProcAddress(dll, "EnableLighting");
            GetTextureMatrixProc = (GET_TEXTURE_MATRIX_PROC)GetProcAddress(dll, "GetTextureMatrix");
            SetTextureMatrixProc = (SET_TEXTURE_MATRIX_PROC)GetProcAddress(dll, "SetTextureMatrix");
            SetProjectionMatrixProc = (SET_PROJECTION_MATRIX_PROC)GetProcAddress(dll, "SetProjectionMatrix");
            GetModelviewMatrixProc = (GET_MODELVIEW_MATRIX_PROC)GetProcAddress(dll, "GetModelviewMatrix");
            SetModelviewMatrixProc = (SET_MODELVIEW_MATRIX_PROC)GetProcAddress(dll, "SetModelviewMatrix");
            PushAllStateProc = (PUSH_ALL_STATE_PROC)GetProcAddress(dll, "PushAllState");
            PopAllStateProc = (POP_ALL_STATE_PROC)GetProcAddress(dll, "PopAllState");
            EnableBlendingProc = (ENABLE_BLENDING_PROC)GetProcAddress(dll, "EnableBlending");
            DisableBlendingProc = (DISABLE_BLENDING_PROC)GetProcAddress(dll, "DisableBlending");
            LoadTextureFromFileProc = (LOAD_TEXTURE_FROM_FILE_PROC)GetProcAddress(dll, "LoadTextureFromFile");
            EnableTextureProc = (ENABLE_TEXTURE_PROC)GetProcAddress(dll, "EnableTexture");
            Enable3DTextureProc = (ENABLE_3D_TEXTURE_PROC)GetProcAddress(dll, "Enable3DTexture");
            DisableTextureProc = (DISABLE_TEXTURE_PROC)GetProcAddress(dll, "DisableTexture");
            ReleaseTextureProc = (RELEASE_TEXTURE_PROC)GetProcAddress(dll, "ReleaseTexture");
            HasPointSpritesProc = (HAS_POINT_SPRITES_PROC)GetProcAddress(dll, "HasPointSprites");
            EnablePointSpritesProc = (ENABLE_POINT_SPRITES_PROC)GetProcAddress(dll, "EnablePointSprites");
            DisablePointSpritesProc = (DISABLE_POINT_SPRITES_PROC)GetProcAddress(dll, "DisablePointSprites");
            GetViewportProc = (GET_VIEWPORT_PROC)GetProcAddress(dll, "GetViewport");
            GetFOVProc = (GET_FOV_PROC)GetProcAddress(dll, "GetFOV");
            DrawQuadsProc = (DRAW_QUADS_PROC)GetProcAddress(dll, "DrawQuads");
            LoadFloatTextureRGBProc = (LOAD_FLOAT_TEXTURE_RGB_PROC)GetProcAddress(dll, "LoadFloatTextureRGB");
            HasFloatTexturesProc = (HAS_FLOAT_TEXTURES_PROC)GetProcAddress(dll, "HasFloatTextures");
            LoadFloatTextureProc = (LOAD_FLOAT_TEXTURE_PROC)GetProcAddress(dll, "LoadFloatTexture");
            LoadTextureProc = (LOAD_TEXTURE_PROC)GetProcAddress(dll, "LoadTexture");
            Load3DTextureProc = (LOAD_3D_TEXTURE_PROC)GetProcAddress(dll, "Load3DTexture");
            Load3DTextureRGBProc = (LOAD_3D_TEXTURE_RGB_PROC)GetProcAddress(dll, "Load3DTextureRGB");
            Load3DTextureLAProc = (LOAD_3D_TEXTURE_LA_PROC)GetProcAddress(dll, "Load3DTextureLA");
            SubLoad3DTextureLAProc = (SUBLOAD_3D_TEXTURE_LA_PROC)GetProcAddress(dll, "SubLoad3DTextureLA");
            Load3DTextureFloatProc = (LOAD_3D_TEXTURE_FLOAT_PROC)GetProcAddress(dll, "Load3DTextureFloat");
            InitRenderTargetProc = (INIT_RENDER_TARGET_PROC)GetProcAddress(dll, "InitRenderTarget");
            InitRenderTextureProc = (INIT_RENDER_TEXTURE_PROC)GetProcAddress(dll, "InitRenderTexture");
            MakeRenderTargetCurrentProc = (MAKE_RENDER_TARGET_CURRENT_PROC)GetProcAddress(dll, "MakeRenderTargetCurrent");
            MakeRenderTextureCurrentProc = (MAKE_RENDER_TEXTURE_CURRENT_PROC)GetProcAddress(dll, "MakeRenderTextureCurrent");
            RestoreRenderTargetProc = (RESTORE_RENDER_TARGET_PROC)GetProcAddress(dll, "RestoreRenderTarget");
            BindRenderTextureProc = (BIND_RENDER_TEXTURE_PROC)GetProcAddress(dll, "BindRenderTexture");
            GetRenderTextureTextureHandleProc = (GET_RENDER_TEXTURE_TEXTURE_HANDLE_PROC)GetProcAddress(dll, "GetRenderTextureTextureHandle");
            ReleaseRenderTextureProc = (RELEASE_RENDER_TEXTURE_PROC)GetProcAddress(dll, "ReleaseRenderTexture");
            ReleaseRenderTargetProc = (RELEASE_RENDER_TARGET_PROC)GetProcAddress(dll, "ReleaseRenderTarget");
            InitRenderTextureCubeProc = (INIT_RENDER_TEXTURE_CUBE_PROC)GetProcAddress(dll, "InitRenderTextureCube");
            MakeRenderTextureCubeCurrentProc = (MAKE_RENDER_TEXTURE_CUBE_CURRENT_PROC)GetProcAddress(dll, "MakeRenderTextureCubeCurrent");
            BindRenderTextureCubeProc = (BIND_RENDER_TEXTURE_CUBE_PROC)GetProcAddress(dll, "BindRenderTextureCube");
            GetRenderTextureCubeTextureHandleProc = (GET_RENDER_TEXTURE_CUBE_TEXTURE_HANDLE_PROC)GetProcAddress(dll, "GetRenderTextureCubeTextureHandle");
            ReleaseRenderTextureCubeProc = (RELEASE_RENDER_TEXTURE_CUBE_PROC)GetProcAddress(dll, "ReleaseRenderTextureCube");
            GetPixelsProc = (GET_PIXELS_PROC)GetProcAddress(dll, "GetPixels");
            SetPixelsProc = (SET_PIXELS_PROC)GetProcAddress(dll, "SetPixels");
            DrawAALineProc = (DRAW_AA_LINE_PROC)GetProcAddress(dll, "DrawAALine");
            DrawAALinesProc = (DRAW_AA_LINES_PROC)GetProcAddress(dll, "DrawAALines");
            HasQuadsProc = (HAS_QUADS_PROC)GetProcAddress(dll, "HasQuads");
            SetCurrentColorProc = (SET_CURRENT_COLOR_PROC)GetProcAddress(dll, "SetCurrentColor");
            ConfigureFogProc = (CONFIGURE_FOG_PROC)GetProcAddress(dll, "ConfigureFog");
            CreateLuminanceTextureProc = (CREATE_LUMINANCE_TEXTURE_PROC)GetProcAddress(dll, "CreateLuminanceTexture");
            CopyLuminanceFromScreenProc = (COPY_LUMINANCE_FROM_SCREEN_PROC)GetProcAddress(dll, "CopyLuminanceFromScreen");
            CopyLuminanceIntoTextureProc = (COPY_LUMINANCE_INTO_TEXTURE_PROC)GetProcAddress(dll, "CopyLuminanceIntoTexture");
            SetDefaultStateProc = (SET_DEFAULT_STATE_PROC)GetProcAddress(dll, "SetDefaultState");
            HeartBeatProc = (HEART_BEAT_PROC)GetProcAddress(dll, "HeartBeat");
            ContextBeingDeletedProc = (CONTEXT_BEING_DELETED_PROC)GetProcAddress(dll, "ContextBeingDeleted");
            SetViewportProc = (SET_VIEWPORT_PROC)GetProcAddress(dll, "SetViewport");
            StartOcclusionQueryProc = (START_OCCLUSION_QUERY_PROC)GetProcAddress(dll, "StartOcclusionQuery");
            EndOcclusionQueryProc = (END_OCCLUSION_QUERY_PROC)GetProcAddress(dll, "EndOcclusionQuery");
            GetOcclusionQueryResultProc = (GET_OCCLUSION_QUERY_RESULT_PROC)GetProcAddress(dll, "GetOcclusionQueryResult");
            SetDepthRangeProc = (SET_DEPTH_RANGE_PROC)GetProcAddress(dll, "SetDepthRange");
            GetDepthRangeProc = (GET_DEPTH_RANGE_PROC)GetProcAddress(dll, "GetDepthRange");
            DeviceLostProc = (DEVICE_LOST_PROC)GetProcAddress(dll, "DeviceLost");
            DeviceResetProc = (DEVICE_RESET_PROC)GetProcAddress(dll, "DeviceReset");
            GetNativeTextureProc = (GET_NATIVE_TEXTURE_PROC)GetProcAddress(dll, "GetNativeTexture");
            SetUserShadersProc = (SET_USER_SHADERS_PROC)GetProcAddress(dll, "SetUserShaders");
            GetShaderProgramObjectProc = (GET_SHADER_PROGRAM_OBJECT_PROC)GetProcAddress(dll, "GetShaderProgramObject");
            DeleteShaderProc = (DELETE_SHADER_PROC)GetProcAddress(dll, "DeleteShader");
            BackfaceCullClockwiseProc = (BACKFACE_CULL_CLOCKWISE_PROC)GetProcAddress(dll, "BackfaceCullClockwise");
            SetReverseZProc = (SET_REVERSE_Z_PROC)GetProcAddress(dll, "SetReverseZ");
            GetLineShaderProc = (GET_LINE_SHADER_PROC)GetProcAddress(dll, "GetLineShader");
            SetForceImmediateProc = (SET_FORCE_IMMEDIATE_PROC)GetProcAddress(dll, "SetForceImmediate");
            GetForceImmediateProc = (GET_FORCE_IMMEDIATE_PROC)GetProcAddress(dll, "GetForceImmediate");
            ExecuteStreamProc = (EXECUTE_STREAM_PROC)GetProcAddress(dll, "ExecuteStream");
            SetStreamProc = (SET_STREAM_PROC)GetProcAddress(dll, "SetStream");
            SettingsChangedProc = (SETTINGS_CHANGED_PROC)GetProcAddress(dll, "SettingsChanged");

            procsInitialized = true;
        }

        if (SetEnvProc) {
            const char *userVert = 0, *userFrag = 0;

            if (type == Atmosphere::OPENGL) {
                Configuration::GetStringValue("user-vert-shader-file-opengl", userVert);
                Configuration::GetStringValue("user-frag-shader-file-opengl", userFrag);
            }

            if (type == Atmosphere::OPENGL32CORE) {
                Configuration::GetStringValue("user-vert-shader-file-opengl32", userVert);
                Configuration::GetStringValue("user-frag-shader-file-opengl32", userFrag);
            }

            if (type == Atmosphere::VULKAN) {
                Configuration::GetStringValue("user-vert-shader-file-vulkan", userVert);
                Configuration::GetStringValue("user-frag-shader-file-vulkan", userFrag);
            }

            *contextOut = SetEnvProc(rightHanded, environment, Atmosphere::GetResourceLoader(), Allocator::GetAllocator(), Atmosphere::GetUserDefinedVertString(),
                                     Atmosphere::GetUserDefinedFragString(), avoidStalls, userVert, userFrag, useUBOs, deferred, userShaders);
            context = *contextOut;

            if (!(*contextOut)) {
                return Atmosphere::E_CANT_INITIALIZE_RENDERER_SUBSYSTEM;
            }
        }
    }

    FrameStarted(*contextOut);

    if (dll == 0) {
        return Atmosphere::E_CANT_LOAD_RENDERER_DLL;
    } else {
        return Atmosphere::E_NOERROR;
    }
#endif
}

void Renderer::DeviceLost(void *context)
{
    FN_TIMER

    if (DeviceLostProc) {
        DeviceLostProc(context);
    }
}

void Renderer::DeviceReset(void *context)
{
    FN_TIMER

    if (DeviceResetProc) {
        DeviceResetProc(context);
    }
}

void Renderer::ClearScreen(const Color& color)
{
    FN_TIMER

    if (ClearScreenProc) {
        ClearScreenProc(color, context);
    }

}

void Renderer::ClearDepth()
{
    FN_TIMER


    if (ClearDepthProc) {
        ClearDepthProc(context);
    }
}

void Renderer::SetUserShaders(const SL_VECTOR(unsigned int)& userShaders)
{
    if (SetUserShadersProc) {
        SetUserShadersProc(userShaders, context);
    }
}

unsigned int Renderer::GetShaderProgram(ShaderHandle shader)
{
    if (GetShaderProgramObjectProc) {
        return GetShaderProgramObjectProc(shader);
    }

    return 0;
}

unsigned int Renderer::GetLineShader()
{
    if (GetLineShaderProc) {
        return GetLineShaderProc(context);
    }
    return 0;
}


ShaderHandle Renderer::LoadShaderFromFile(const char *filename, const SL_VECTOR(SL_STRING)& defines)
{
    FN_TIMER
    bool noShaders = false;
    Configuration::GetBoolValue("disable-shaders", noShaders);

    const char *userVert = 0, *userFrag = 0;

    if (type == Atmosphere::OPENGL) {
        Configuration::GetStringValue("user-vert-shader-file-opengl", userVert);
        Configuration::GetStringValue("user-frag-shader-file-opengl", userFrag);
    }

    if (type == Atmosphere::OPENGL32CORE) {
        Configuration::GetStringValue("user-vert-shader-file-opengl32", userVert);
        Configuration::GetStringValue("user-frag-shader-file-opengl32", userFrag);
    }

    if (!noShaders) {
        if (LoadShaderFromFileProc) {

            bool enableObjectLabeling = false;
            Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);

            return LoadShaderFromFileProc(filename, userVert, userFrag, enableObjectLabeling, defines, context);
        }
    }

    return 0;
}

ShaderHandle Renderer::LoadShaderFromSource(const char *source)
{
    FN_TIMER
    bool noShaders = false;
    Configuration::GetBoolValue("disable-shaders", noShaders);

    if (!noShaders) {
        if (LoadShaderFromSourceProc) {
            return LoadShaderFromSourceProc(source);
        }
    }

    return 0;
}

void Renderer::DeleteShader(ShaderHandle shader)
{
    if (DeleteShaderProc && shader) {
        DeleteShaderProc(shader, context);
    }
}

int Renderer::GetConstantLocation(ShaderHandle shader, const char* uniformName) const
{
    if (GetConstantLocationProc && shader) {
        return GetConstantLocationProc(shader, uniformName);
    }
    return -1;
}

void Renderer::SetConstantVectorAtLocation(ShaderHandle shader, int loc, const Vector3& vector)
{
    if (SetConstantVector4AtLocationProc && shader) {
        float x[4];
        x[0] = (float)vector.x;
        x[1] = (float)vector.y;
        x[2] = (float)vector.z;
        x[3] = 1.0f;

        SetConstantVector4AtLocationProc(shader, loc, x, context);
    }
}

void Renderer::SetConstantVector4AtLocation(ShaderHandle shader, int loc, const Vector4& vector)
{
    if (SetConstantVector4AtLocationProc && shader) {
        float x[4];
        x[0] = (float)vector.x;
        x[1] = (float)vector.y;
        x[2] = (float)vector.z;
        x[3] = (float)vector.w;

        SetConstantVector4AtLocationProc(shader, loc, x, context);
    }
}

void Renderer::SetConstantVector4AtLocation(ShaderHandle shader, int loc, const Vector4f& vector)
{
    if (SetConstantVector4AtLocationProc && shader) {
        SetConstantVector4AtLocationProc(shader, loc, vector.floatPtr(), context);
    }
}

void Renderer::SetConstantMatrix4AtLocation(ShaderHandle shader, int loc, const Matrix4& matrix)
{
    if (SetConstantMatrix4AtLocationProc && shader) {
        float m[16];
        int i = 0;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                m[i++] = (float)(matrix.elem[row][col]);
            }
        }

        SetConstantMatrix4AtLocationProc(shader, loc, m, context);
    }
}

void Renderer::SetConstantMatrix4AtLocation(ShaderHandle shader, int loc, const float* matrix)
{
    if (SetConstantMatrix4AtLocationProc && shader) {
        return SetConstantMatrix4AtLocationProc(shader, loc, (float*)matrix, context);
    }
}

bool Renderer::SetConstantVector(ShaderHandle shader, const char *varName, const Vector3& vec)
{
    FN_TIMER
    if (SetConstantVector4Proc && shader) {
        float x[4];
        x[0] = (float)vec.x;
        x[1] = (float)vec.y;
        x[2] = (float)vec.z;
        x[3] = 1.0f;

        return SetConstantVector4Proc(shader, varName, x);
    }

    return false;
}

bool Renderer::SetConstantVector4(ShaderHandle shader, const char *varName, const Vector4& vec)
{
    FN_TIMER
    if (SetConstantVector4Proc && shader) {
        float x[4];
        x[0] = (float)vec.x;
        x[1] = (float)vec.y;
        x[2] = (float)vec.z;
        x[3] = (float)vec.w;

        return SetConstantVector4Proc(shader, varName, x);
    }

    return false;
}

bool Renderer::SetConstantVector4(ShaderHandle shader, const char *varName, const Vector4f& vec)
{
    FN_TIMER
    if (SetConstantVector4Proc && shader) {
        return SetConstantVector4Proc(shader, varName, vec.floatPtr());
    }

    return false;
}

bool Renderer::SetConstantInt(ShaderHandle shader, const char *varName, int val)
{
    FN_TIMER
    if (SetConstantIntProc && shader) {
        return SetConstantIntProc(shader, varName, val);
    }

    return false;
}

bool Renderer::SetConstantMatrix(ShaderHandle shader, const char *varName, const Matrix4& matrix)
{
    FN_TIMER
    if (SetConstantMatrix4Proc && shader) {
        float m[16];
        int i = 0;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                m[i++] = (float)(matrix.elem[row][col]);
            }
        }

        return SetConstantMatrix4Proc(shader, varName, m);
    }

    return false;
}

bool Renderer::SetConstantMatrix(ShaderHandle shader, const char *varName, const float *matrix)
{
    FN_TIMER
    if (SetConstantMatrix4Proc && shader) {
        return SetConstantMatrix4Proc(shader, varName, (float*)matrix);
    }

    return false;
}

bool Renderer::PreConstantsSet(ShaderHandle shader) const
{
    FN_TIMER
    if (PreConstantsSetProc && shader) {
        return PreConstantsSetProc(shader);
    }

    return false;

}

bool Renderer::PostConstantsSet(ShaderHandle shader) const
{
    FN_TIMER
    if (PostConstantsSetProc && shader) {
        return PostConstantsSetProc(shader);
    }

    return false;
}

bool Renderer::BindShader(ShaderHandle shader)
{
    FN_TIMER
    if (BindShaderProc && shader) {
        lastShader = shader;
        return BindShaderProc(shader, context);
    }

    return false;
}


bool Renderer::UnbindShader()
{
    FN_TIMER
    if (UnbindShaderProc) {
        lastShader = 0;
        return UnbindShaderProc(context);
    }

    return false;
}

void Renderer::IncrementConstantBuffersOffset(ShaderHandle shader)
{
    FN_TIMER
    if (IncrementConstantBuffersOffsetProc && shader) {
        IncrementConstantBuffersOffsetProc(shader, context);
    }
}

bool Renderer::Shutdown(void* _context)
{
    FN_TIMER

    if (ShutdownShaderSystemProc) {
        return ShutdownShaderSystemProc(context);
    }

    return false;
}

VertexBufferHandle Renderer::AllocateVertexBuffer(int nVerts, const char* name, const SilverLining::BufferProperties& bufferProperties)
{
    FN_TIMER
    if (AllocateVertexBufferProc) {
        return AllocateVertexBufferProc(nVerts, name, bufferProperties, context);
    }

    return 0;
}

bool Renderer::LockVertexBuffer(VertexBufferHandle bufferHandle)
{
    FN_TIMER
    if (LockVertexBufferProc) {
        return LockVertexBufferProc(bufferHandle);
    }

    return false;
}

Vertex *Renderer::GetVertices(VertexBufferHandle bufferHandle)
{
    FN_TIMER
    if (GetVerticesProc) {
        return GetVerticesProc(bufferHandle, context);
    }

    return 0;
}

bool Renderer::UnlockVertexBuffer(VertexBufferHandle bufferHandle)
{
    FN_TIMER
    if (UnlockVertexBufferProc) {
        return UnlockVertexBufferProc(bufferHandle, context);
    }

    return false;
}

bool Renderer::UpdateVertexBuffer(VertexBufferHandle bufferHandle, int offset, Vertex *verts, int nVerts, bool justColors)
{
    FN_TIMER

    bool updateOK = false;
    if (UpdateVertexBufferProc) {
        updateOK = UpdateVertexBufferProc(bufferHandle, offset, verts, nVerts, justColors, context);
    }

    if (!updateOK) {
#if SILVERLINING_TRACK_CONTEXT_OPERATIONS_DURING_DRAW
        if ((tcsData.GetIsDrawingObjects() || tcsData.GetIsDrawingSky())) {
            std::cout << "LockVertexBuffer/GetVertices/UnlockVertexBuffer during rendering!!" << std::endl;
        }
#endif
        if (LockVertexBuffer(bufferHandle)) {
            Vertex *v = GetVertices(bufferHandle);
            if (v) {
                v = v + offset;
                for (int i = 0; i < nVerts; i++) {
                    v[i] = verts[i];
                }
            }
            UnlockVertexBuffer(bufferHandle);
            return true;
        }
    }

    return updateOK;
}

bool Renderer::GetVertexBuffer(VertexBufferHandle bufferHandle, int offset, Vertex *verts, int nVerts)
{
    FN_TIMER

    bool getOK = false;
    if (GetVertexBufferProc) {
        getOK = GetVertexBufferProc(bufferHandle, offset, verts, nVerts, context);
    }

    if (!getOK) {
        if (LockVertexBuffer(bufferHandle)) {
            Vertex *v = GetVertices(bufferHandle) + offset;
            for (int i = 0; i < nVerts; i++) {
                verts[i] = v[i];
            }
            UnlockVertexBuffer(bufferHandle);
            return true;
        }
    } else {
        return true;
    }

    return false;
}

bool Renderer::ReleaseVertexBuffer(VertexBufferHandle bufferHandle)
{
    FN_TIMER
    if (ReleaseVertexBufferProc) {
        return ReleaseVertexBufferProc(bufferHandle, context);
    }

    return false;
}

bool Renderer::SetVertexBuffer(VertexBufferHandle bufferHandle, bool vertexColors)
{
    FN_TIMER
    if (SetVertexBufferProc) {
        return SetVertexBufferProc(bufferHandle, vertexColors, context);
    }

    return false;
}

bool Renderer::UnsetVertexBuffer()
{
    FN_TIMER
    if (UnsetVertexBufferProc) {
        return UnsetVertexBufferProc(context);
    }

    return false;
}

bool Renderer::IncrementOffsetIndex(VertexBufferHandle vbh)
{
    FN_TIMER
    if (IncrementOffsetIndexProc) {
        return IncrementOffsetIndexProc(vbh, context);
    }

    return true;
}

/** Index Buffers */
IndexBufferHandle Renderer::AllocateIndexBuffer(int nVerts, const char* name, const SilverLining::BufferProperties& bufferProperties)
{
    FN_TIMER
    if (AllocateIndexBufferProc) {
        return AllocateIndexBufferProc(nVerts, name, bufferProperties, context);
    }

    return 0;
}

bool Renderer::LockIndexBuffer(IndexBufferHandle ib)
{
#if SILVERLINING_TRACK_CONTEXT_OPERATIONS_DURING_DRAW
    if ((tcsData.GetIsDrawingObjects() || tcsData.GetIsDrawingSky())) {
        std::cout << "LockIndexBuffer during rendering!!" << std::endl;
    }
#endif
    FN_TIMER
    if (LockIndexBufferProc) {
        return LockIndexBufferProc(ib, context);
    }

    return false;
}

Index* Renderer::GetIndices(IndexBufferHandle ib)
{
#if SILVERLINING_TRACK_CONTEXT_OPERATIONS_DURING_DRAW
    if ((tcsData.GetIsDrawingObjects() || tcsData.GetIsDrawingSky())) {
        std::cout << "LockIndexBuffer during rendering!!" << std::endl;
    }
#endif
    FN_TIMER
    if (GetIndicesProc) {
        return GetIndicesProc(ib, context);
    }

    return 0;
}

bool Renderer::UnlockIndexBuffer(IndexBufferHandle ib)
{
#if SILVERLINING_TRACK_CONTEXT_OPERATIONS_DURING_DRAW
    if ((tcsData.GetIsDrawingObjects() || tcsData.GetIsDrawingSky())) {
        std::cout << "LockIndexBuffer during rendering!!" << std::endl;
    }
#endif
    FN_TIMER
    if (UnlockIndexBufferProc) {
        return UnlockIndexBufferProc(ib, context);
    }

    return false;
}

bool Renderer::ReleaseIndexBuffer(IndexBufferHandle ib)
{
    FN_TIMER
    if (ReleaseIndexBufferProc) {
        return ReleaseIndexBufferProc(ib, context);
    }

    return false;
}

bool Renderer::SetIndexBuffer(IndexBufferHandle ib)
{
    FN_TIMER
    if (SetIndexBufferProc) {
        return SetIndexBufferProc(ib, context);
    }

    return false;
}

bool Renderer::UnsetIndexBuffer()
{
    FN_TIMER
    if (UnsetIndexBufferProc) {
        return UnsetIndexBufferProc(context);
    }

    return false;
}

/** Drawing methods */
bool Renderer::DrawStrip(VertexBufferHandle vb, IndexBufferHandle ib, int startIdx, int nIndices,
                         int nVerts, bool vertColors, bool preDraw)
{
    FN_TIMER
    bool ok = false;
    if (DrawStripProc) {
        if (SetVertexBuffer(vb, vertColors)) {
            if (SetIndexBuffer(ib)) {
                ok = DrawStripProc(ib, startIdx, nIndices, nVerts, preDraw, context);
                UnsetIndexBuffer();
            }
            UnsetVertexBuffer();
        }
    }

    return ok;
}

bool Renderer::DrawMultiStrips(CommandBufferHandle commandBuffer)
{
    FN_TIMER
    bool ok = false;
    if (DrawMultiStripsProc) {
        ok = DrawMultiStripsProc(commandBuffer, context);
    }
    return ok;
}

bool Renderer::DrawStripDirect(IndexBufferHandle ib, int startIdx, int nIndices, int nVerts)
{
    FN_TIMER
    bool ok = false;
    if (DrawStripProc) {
        ok = DrawStripProc(ib, startIdx, nIndices, nVerts, true, context);
    }

    return ok;
}

bool Renderer::DrawStripDirectInstanced(IndexBufferHandle ib, int startIdx, int nIndices, int nVerts, int instanceCount, int firstInstance)
{
    FN_TIMER
    bool ok = false;
    if (DrawStripInstancedProc) {
        ok = DrawStripInstancedProc(ib, startIdx, nIndices, nVerts, instanceCount, firstInstance, context);
    }

    return ok;
}

bool Renderer::DrawLineStrip(VertexBufferHandle vb, const Color& color, int nVerts, float width)
{
    FN_TIMER

    bool ok = false;
    if (DrawLineStripProc && SetCurrentColorProc) {
        ok = DrawLineStripProc(vb, color, nVerts, width, context);
    }

    return ok;
}

bool Renderer::DrawPoints(VertexBufferHandle vb, double pointSize, int nPoints, int start, bool vertColors)
{
    FN_TIMER
    bool ok = false;
    if (DrawPointsProc) {
        if (SetVertexBuffer(vb, vertColors)) {
            ok = DrawPointsProc(pointSize, nPoints, start, context);
            UnsetVertexBuffer();
        }
    }

    return ok;
}

bool Renderer::HasQuads()
{
    FN_TIMER
    if (HasQuadsProc) {
        return HasQuadsProc();
    }

    return false;
}

bool Renderer::DrawQuads(VertexBufferHandle vb, int nPoints, int start, bool vertColors)
{
    FN_TIMER
    bool ok = false;

    if (DrawQuadsProc) {
        if (SetVertexBuffer(vb, vertColors)) {
            ok = DrawQuadsProc(nPoints, start, context);
            UnsetVertexBuffer();
        }

    }

    return ok;
}

/** State methods */
bool Renderer::EnableDepthWrites(bool enable)
{
    FN_TIMER

    if (disableDepthWrites) {
        return true;
    }

    if (EnableDepthWritesProc) {
        return EnableDepthWritesProc(enable, context);
    }

    return false;
}

bool Renderer::EnableDepthReads(bool enable)
{
    FN_TIMER
    if (disableDepthReads) {
        return true;
    }

    if (EnableDepthReadsProc) {
        return EnableDepthReadsProc(enable, context);
    }

    return false;
}

bool Renderer::EnableTexture2D(bool enable)
{
    FN_TIMER
    if (EnableTexture2DProc) {
        return EnableTexture2DProc(enable, context);
    }

    return false;
}

bool Renderer::EnableTexture3D(bool enable)
{
    FN_TIMER
    if (EnableTexture3DProc) {
        return EnableTexture3DProc(enable, context);
    }

    return false;
}

bool Renderer::EnableBackfaceCulling(bool enable)
{
    FN_TIMER

    if (enable != cullingEnabled || cullingUndefined || GetIsDirectX() || alwaysApplyState) {
        cullingEnabled = enable;
        cullingUndefined = false;

        if (EnableBackfaceCullingProc) {
            return EnableBackfaceCullingProc(enable, context);
        } else {
            return false;
        }
    }

    return true;
}

bool Renderer::BackfaceCullClockwise(bool cullCW)
{
    FN_TIMER

    if (BackfaceCullClockwiseProc) {
        return BackfaceCullClockwiseProc(cullCW, context);
    } else {
        return false;
    }
}

bool Renderer::EnableFog(bool enable)
{
    FN_TIMER

    fogEnabled = enable;

    if (EnableFogProc) {
        return EnableFogProc(enable, context);
    }

    return false;
}

bool Renderer::ConfigureFog(double density, double start, double end, const Color& c)
{
    fogDensity = density;
    fogStart = start;
    fogEnd = end;
    fogColor = c;

    if (ConfigureFogProc) {
        return ConfigureFogProc(density, start, end, c, context);
    }

    return false;
}

void Renderer::GetFog(double& density, double &start, double &end, Color& c) const
{
    density = fogDensity;
    start = fogStart;
    end = fogEnd;
    c = fogColor;
}

bool Renderer::EnableLighting(bool enable)
{
    FN_TIMER
    if (EnableLightingProc) {
        return EnableLightingProc(enable, context);
    }

    return false;
}

bool Renderer::SetCurrentColor(const Color& c)
{
    FN_TIMER
    if (SetCurrentColorProc) {
        return SetCurrentColorProc(c, context);
    }

    return false;
}

bool Renderer::StartOcclusionQuery(QueryHandle *q)
{
    FN_TIMER
    if (StartOcclusionQueryProc) {
        return StartOcclusionQueryProc(q);
    }

    return false;
}

bool Renderer::EndOcclusionQuery(QueryHandle q)
{
    FN_TIMER
    if (EndOcclusionQueryProc) {
        return EndOcclusionQueryProc(q);
    }

    return false;
}

unsigned int Renderer::GetOcclusionQueryResults(QueryHandle q)
{
    FN_TIMER
    if (GetOcclusionQueryResultProc) {
        return GetOcclusionQueryResultProc(q);
    }

    return 0;
}

bool Renderer::PushAllState()
{
    FN_TIMER
    if (PushAllStateProc) {
        return PushAllStateProc(context);
    }

    return false;
}

bool Renderer::PopAllState()
{
    FN_TIMER
    if (PopAllStateProc) {
        return PopAllStateProc(context);
    }

    lastSrc = lastDst = -1;
    cullingUndefined = true;
    lastTex = 0;
    lastShader = 0;
    blendingEnabled = false;

    return false;
}

bool Renderer::SetDefaultState()
{
    FN_TIMER

    cullingUndefined = true;
    blendingEnabled = false;
    lastSrc = lastDst = -1;
    lastTex = 0;
    lastShader = 0;

    static bool doDefault = true;
    static bool configRead = false;
    if (!configRead) {
        Configuration::GetBoolValue("set-default-state", doDefault);
        configRead = true;
    }

    if (doDefault && SetDefaultStateProc) {
        blendingEnabled = false;
        return SetDefaultStateProc(context);
    }

    return false;
}

bool Renderer::HeartBeat(void) const
{
    FN_TIMER

    if (HeartBeatProc) {
        return HeartBeatProc();
    }

    return false;
}

bool Renderer::ContextBeingDeleted(void* context) const
{
    FN_TIMER

    if (ContextBeingDeletedProc) {
        return ContextBeingDeletedProc(context);
    }

    return false;
}

bool Renderer::GetTextureMatrix(Matrix4 *m)
{
    FN_TIMER
    *m = texture;

    return true;
}

bool Renderer::SetTextureMatrix(const Matrix4& m)
{
    FN_TIMER
    if (SetTextureMatrixProc) {
        texture = m;
        return SetTextureMatrixProc(m, context);
    }
    return false;
}

bool Renderer::SetProjectionMatrix(const Matrix4& m)
{
    FN_TIMER
    if (SetProjectionMatrixProc) {
        projection = m;
        modelviewproj = projection *  modelview;
        SetProjectionMatrixProc(m, context);
    }

    return false;
}

bool Renderer::SetInfiniteProjectionMatrix(double nearClip, double fovy, double aspect)
{
    double e = 1.0 / tan(fovy * 0.5);
    const double epsilon = 2.4E-7;
    double zScale = type == Atmosphere::OPENGL ? 2.0 : 1.0;

    Matrix4 proj;

    if (rightHanded) {
        proj.elem[0][0] = e;
        proj.elem[0][1] = 0;
        proj.elem[0][2] = 0;
        proj.elem[0][3] = 0;

        proj.elem[1][0] = 0;
        proj.elem[1][1] = e / aspect;
        proj.elem[1][2] = 0;
        proj.elem[1][3] = 0;

        proj.elem[2][0] = 0;
        proj.elem[2][1] = 0;
        proj.elem[2][2] = epsilon - 1.0;
        proj.elem[2][3] = (epsilon - zScale) * nearClip;

        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = -1.0;
        proj.elem[3][3] = 0;
    } else {
        proj.elem[0][0] = e / aspect;
        proj.elem[0][1] = 0;
        proj.elem[0][2] = 0;
        proj.elem[0][3] = 0;
        proj.elem[1][0] = 0;
        proj.elem[1][1] = e;
        proj.elem[1][2] = 0;
        proj.elem[1][3] = 0;
        proj.elem[2][0] = 0;
        proj.elem[2][1] = 0;
        proj.elem[2][2] = 1.0;
        proj.elem[2][3] = (epsilon - zScale) * nearClip;
        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = 1.0 + epsilon;
        proj.elem[3][3] = 0;
    }

    return SetProjectionMatrix(proj);
}

bool Renderer::SetProjectionMatrix(double l, double r, double t, double b, double zn, double zf)
{
    Matrix4 proj;

    double zScale = GetIsOpenGL() ? 2.0 : 1.0;
    bool is_reversed = (minZ > maxZ);

    if (rightHanded) {
        proj.elem[0][0] = (2.0*zn) / (r - l);
        proj.elem[0][1] = 0;
        proj.elem[0][2] = (r + l) / (r - l);
        proj.elem[0][3] = 0;
        proj.elem[1][0] = 0;
        proj.elem[1][1] = (2.0*zn) / (t - b);
        proj.elem[1][2] = (t + b) / (t - b);
        proj.elem[1][3] = 0;
        proj.elem[2][0] = 0;
        proj.elem[2][1] = 0;
        if (is_reversed) {
            proj.elem[2][2] = zn / (zn - zf);
            proj.elem[2][3] = (zf*zn) / (zf - zn);
        } else {
            proj.elem[2][2] = -((zf + zn) / (zf - zn));
            proj.elem[2][3] = -((zScale*zn*zf) / (zf - zn));
        }
        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = -1;
        proj.elem[3][3] = 0;
    } else {
        proj.elem[0][0] = (2.0*zn) / (r - l);
        proj.elem[0][1] = 0;
        proj.elem[0][2] = 0;
        proj.elem[0][3] = 0;
        proj.elem[1][0] = 0;
        proj.elem[1][1] = (2.0*zn) / (t - b);
        proj.elem[1][2] = 0;
        proj.elem[1][3] = 0;
        proj.elem[2][0] = (l + r) / (l - r);
        proj.elem[2][1] = (t + b) / (b - t);
        proj.elem[2][2] = zf / (zf - zn);
        proj.elem[2][3] = 1;
        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = (zScale*zn*zf) / (zn - zf);
        proj.elem[3][3] = 0;

        proj.Transpose();
    }

    return SetProjectionMatrix(proj);
}

bool Renderer::SetProjectionMatrix(double nearClip, double farClip, double fovx, double fovy)
{
    FN_TIMER
    double l, r, t, b, n, f;

    n = nearClip;
    f = farClip;

    double halfAngle = fovx * 0.5;
    r = n * tan(halfAngle);
    l = -r;
    halfAngle = fovy * 0.5;
    t = n * tan(halfAngle);
    b = -t;

    return SetProjectionMatrix(l, r, t, b, n, f);
}

bool Renderer::SetOrthoMatrix(double left, double right, double bottom, double top, double pnear, double pfar)
{
    FN_TIMER

    Matrix4 m;

    if (rightHanded) {
        m.elem[0][0] = 2.0 / (right - left);
        m.elem[0][3] = -((right + left) / (right - left));

        m.elem[1][1] = 2.0 / (top - bottom);
        m.elem[1][3] = -((top + bottom) / (top - bottom));

        m.elem[2][2] = -2.0 / (pfar - pnear);
        m.elem[2][3] = -((pfar + pnear) / (pfar - pnear));
    } else {
        m.elem[0][0] = 2.0 / (right - left);
        m.elem[1][1] = 2.0 / (top - bottom);
        m.elem[2][2] = 1.0 / (pfar - pnear);
        m.elem[3][2] = pnear / (pnear - pfar);

        m.Transpose();
    }

    return SetProjectionMatrix(m);
}

bool Renderer::Set2DOrthoMatrix(double w, double h)
{
    FN_TIMER

    return SetOrthoMatrix(0, w, 0, h, 0, 1);
}

bool Renderer::GetModelviewMatrix(Matrix4 *m) const
{
    FN_TIMER
    *m = modelview;

    return true;
}

bool Renderer::SetModelviewMatrix(const Matrix4& m)
{
    FN_TIMER
    if (SetModelviewMatrixProc) {
        modelview = m;
        modelviewproj = projection * modelview;
        return SetModelviewMatrixProc(m, context);
    }

    return false;
}

bool Renderer::ProjectToScreen(const Vector3& pt, Vector3 *scr)
{
    FN_TIMER
    Vector4 pt4(pt.x, pt.y, pt.z, 1.0);

    pt4 = modelview * pt4;
    pt4 = projection * pt4;

    if (pt4.w == 0.0)
        return false;

    pt4.x /= pt4.w;
    pt4.y /= pt4.w;
    pt4.z /= pt4.w;

    scr->x = vpX + (1 + pt4.x) * vpW / 2;
    scr->y = vpY + (1 + pt4.y) * vpH / 2;
    scr->z = minZ + (1 + pt4.z) * (maxZ - minZ) / 2;

    return true;
}

bool Renderer::EnableBlending(int src, int dst, bool force)
{
    FN_TIMER

    if (!force) {
        if (blendingEnabled && src == lastSrc && dst == lastDst) {
            if (!alwaysApplyState) {
                return true;
            }
        }
    }

    if (EnableBlendingProc) {
        blendingEnabled = true;
        lastSrc = src;
        lastDst = dst;

        return EnableBlendingProc(src, dst, context);
    }

    return false;
}

bool Renderer::DisableBlending()
{
    FN_TIMER
    if (DisableBlendingProc) {
        blendingEnabled = false;
        lastSrc = lastDst = -1;
        return DisableBlendingProc(context);
    }

    return false;
}

bool Renderer::LoadTextureFromFile(const char *imgPath, TextureHandle *tex, bool repeatU, bool repeatV, const char* name)
{
    FN_TIMER
    if (LoadTextureFromFileProc) {
        return LoadTextureFromFileProc(imgPath, tex, repeatU, repeatV, name, context);
    }

    return false;
}

bool Renderer::HasFloatTextures()
{
    FN_TIMER
    if (HasFloatTexturesProc) {
        return HasFloatTexturesProc();
    }

    return false;
}

bool Renderer::LoadFloatTextureRGB(const float *data, int width, int height, TextureHandle *texture, const char* name)
{
    FN_TIMER
    if (LoadFloatTextureRGBProc) {
        return LoadFloatTextureRGBProc(data, width, height, texture, name, context);
    }

    return false;
}

bool Renderer::LoadFloatTexture(const float *data, int width, int height, TextureHandle *texture, const char* name)
{
    FN_TIMER
    if (LoadFloatTextureProc) {
        return LoadFloatTextureProc(data, width, height, texture, name);
    }

    return false;
}

bool Renderer::LoadTexture(const unsigned char *data, int width, int height, TextureHandle *texture, bool repeatU, bool repeatV, const char* name)
{
    FN_TIMER
    if (LoadTextureProc) {
        return LoadTextureProc(data, width, height, texture, repeatU, repeatV, name, context);
    }

    return false;
}

bool Renderer::Load3DTexture(const unsigned char *data, int width, int height, int depth, TextureHandle *texture,
                             bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    FN_TIMER
    if (Load3DTextureProc) {
        return Load3DTextureProc(data, width, height, depth, texture, repeatU, repeatV, repeatR, name);
    }

    return false;
}

bool Renderer::Load3DTextureRGB(const unsigned char *data, int width, int height, int depth, TextureHandle *texture,
                                bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    FN_TIMER
    if (Load3DTextureRGBProc) {
        return Load3DTextureRGBProc(data, width, height, depth, texture, repeatU, repeatV, repeatR, name, context);
    }

    return false;
}

bool Renderer::Load3DTextureLA(const unsigned char *data, int width, int height, int depth, TextureHandle *texture,
                               bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    FN_TIMER
    if (Load3DTextureLAProc) {
        return Load3DTextureLAProc(data, width, height, depth, texture, repeatU, repeatV, repeatR, name, context);
    }

    return false;
}

bool Renderer::SubLoad3DTextureLA(const unsigned char *data, int width, int height, int depth, int x, int y, int z,
                                  int dataRowPitch, int dataSlicePitch, TextureHandle texture)
{
    FN_TIMER
    if (SubLoad3DTextureLAProc) {
        return SubLoad3DTextureLAProc(data, width, height, depth, x, y, z, dataRowPitch, dataSlicePitch, texture, context);
    }

    return false;
}

bool Renderer::Load3DTextureFloat(const float *data, int width, int height, int depth, TextureHandle *texture,
                                  bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    FN_TIMER
    if (Load3DTextureFloatProc) {
        return Load3DTextureFloatProc(data, width, height, depth, texture, repeatU, repeatV, repeatR, name);
    }

    return false;
}

bool Renderer::EnableTexture(TextureHandle tex, int stage)
{
    FN_TIMER
    if (EnableTextureProc) {
        lastTex = tex;
        return EnableTextureProc(tex, stage, context);
    }

    return false;
}

bool Renderer::Enable3DTexture(TextureHandle tex, int stage)
{
    FN_TIMER
    if (Enable3DTextureProc) {
        lastTex = tex;
        return Enable3DTextureProc(tex, stage, context);
    }

    return false;
}

bool Renderer::DisableTexture(int stage)
{
    FN_TIMER
    if (DisableTextureProc) {
        lastTex = 0;
        return DisableTextureProc(stage, context);
    }

    return false;
}

bool Renderer::ReleaseTexture(TextureHandle tex)
{
    FN_TIMER
    if (ReleaseTextureProc && tex) {
        return ReleaseTextureProc(tex);
    }

    return false;
}

bool Renderer::HasPointSprites()
{
    FN_TIMER
    if (HasPointSpritesProc) {
        return HasPointSpritesProc();
    }

    return false;
}

bool Renderer::EnablePointSprites(double pointSize)
{
    FN_TIMER
    if (EnablePointSpritesProc) {
        return EnablePointSpritesProc(pointSize, context);
    }

    return false;
}

bool Renderer::DisablePointSprites()
{
    FN_TIMER
    if (DisablePointSpritesProc) {
        return DisablePointSpritesProc(context);
    }

    return false;
}
bool Renderer::InitRenderTexture(int w, int h, RenderTextureHandle *texture, const char* name)
{
    FN_TIMER
    if (InitRenderTextureProc) {
        return InitRenderTextureProc(w, h, texture, name, context);
    }

    return false;
}

bool Renderer::MakeRenderTextureCurrent(RenderTextureHandle texture, bool clear)
{
    FN_TIMER
    if (MakeRenderTextureCurrentProc) {
        return MakeRenderTextureCurrentProc(texture, clear, context);
    }

    return false;
}

bool Renderer::BindRenderTexture(RenderTextureHandle texture, void* camera)
{
    FN_TIMER
    if (BindRenderTextureProc) {
        bool ok = BindRenderTextureProc(texture, camera, context);
        return ok;
    }

    return false;
}

bool Renderer::GetRenderTextureTextureHandle(RenderTextureHandle renTexture, TextureHandle *texture)
{
    FN_TIMER
    if (GetRenderTextureTextureHandleProc) {
        return GetRenderTextureTextureHandleProc(renTexture, texture, context);
    }

    return false;
}

bool Renderer::ReleaseRenderTexture(RenderTextureHandle texture)
{
    FN_TIMER
    if (ReleaseRenderTextureProc) {
        return ReleaseRenderTextureProc(texture, context);
    }

    return false;
}

bool Renderer::InitRenderTextureCube(int w, int h, RenderTextureHandle *texture, bool floatingPoint, bool generateMipMaps, const char* name)
{
    FN_TIMER
    if (InitRenderTextureCubeProc) {
        return InitRenderTextureCubeProc(w, h, texture, floatingPoint, generateMipMaps, name, context);
    }

    return false;
}

bool Renderer::MakeRenderTextureCubeCurrent(RenderTextureHandle texture, bool clear, CubeFace face)
{
    FN_TIMER
    if (MakeRenderTextureCubeCurrentProc) {
        return MakeRenderTextureCubeCurrentProc(texture, clear, face, context);
    }

    return false;
}

bool Renderer::BindRenderTextureCube(RenderTextureHandle texture)
{
    FN_TIMER
    if (BindRenderTextureCubeProc) {
        return BindRenderTextureCubeProc(texture, context);
    }

    return false;
}

bool Renderer::GetRenderTextureCubeTextureHandle(RenderTextureHandle renTexture, TextureHandle *texture) const
{
    FN_TIMER
    if (GetRenderTextureCubeTextureHandleProc) {
        return GetRenderTextureCubeTextureHandleProc(renTexture, texture, context);
    }

    return false;
}

bool Renderer::ReleaseRenderTextureCube(RenderTextureHandle texture)
{
    FN_TIMER
    if (ReleaseRenderTextureCubeProc) {
        return ReleaseRenderTextureCubeProc(texture, context);
    }

    return false;
}

bool Renderer::InitRenderTarget(int w, int h, RenderTargetHandle *tgt)
{
    FN_TIMER
    if (InitRenderTargetProc) {
        return InitRenderTargetProc(w, h, tgt);
    }

    return false;
}

bool Renderer::MakeRenderTargetCurrent(RenderTargetHandle tgt)
{
    FN_TIMER
    if (MakeRenderTargetCurrentProc) {
        return MakeRenderTargetCurrentProc(tgt);
    }

    return false;
}

bool Renderer::RestoreRenderTarget(RenderTargetHandle tgt)
{
    FN_TIMER
    if (RestoreRenderTargetProc) {
        return RestoreRenderTargetProc(tgt);
    }

    return false;
}

bool Renderer::ReleaseRenderTarget(RenderTargetHandle tgt)
{
    FN_TIMER
    if (ReleaseRenderTargetProc) {
        return ReleaseRenderTargetProc(tgt);
    }

    return false;
}

bool Renderer::GetPixels(int x, int y, int w, int h, void *pixels, bool immediate)
{
    FN_TIMER
    if (GetPixelsProc) {
        return GetPixelsProc(x, y, w, h, pixels, immediate, context);
    }

    return false;
}

bool Renderer::SetPixels(int x, int y, int w, int h, void *pixels, void* _context)
{
    FN_TIMER
    if (SetPixelsProc) {
        return SetPixelsProc(x, y, w, h, pixels, _context);
    }

    return false;
}

bool Renderer::DrawAALine(const Color& c, double width, const Vector3& p1, const Vector3& p2)
{
    FN_TIMER
    if (DrawAALineProc) {
        return DrawAALineProc(c, width, p1, p2, context);
        cullingUndefined = true;
    }

    return false;
}

bool Renderer::DrawAALines(const Color& c, double width, const SL_VECTOR(Vector3)& points)
{
    FN_TIMER
    if (DrawAALinesProc) {
        return DrawAALinesProc(c, width, points, context);
        cullingUndefined = true;
    }

    return false;
}

bool Renderer::GetViewport(int& x, int& y, int& w, int &h) const
{
    FN_TIMER
    x = vpX;
    y = vpY;
    w = vpW;
    h = vpH;
    return true;
}

bool Renderer::SetViewport(int x, int y, int w, int h)
{
    vpX = x;
    vpY = y;
    vpW = w;
    vpH = h;
    if (SetViewportProc) {
        SetViewportProc(x, y, w, h, context);
    }

    return true;
}

bool Renderer::GetDepthRange(float& zmin, float& zmax) const
{
    FN_TIMER
    zmin = minZ;
    zmax = maxZ;
    return true;
}

bool Renderer::SetDepthRange(float zmin, float zmax)
{
    if (zmin != minZ || zmax != maxZ || alwaysApplyState) {
        minZ = zmin;
        maxZ = zmax;
        if (SetDepthRangeProc) {
            SetDepthRangeProc(zmin, zmax, context);
        }
    }
    return true;
}

bool Renderer::SetReverseZ(bool reverse)
{
    if (SetReverseZProc) {
        SetReverseZProc(reverse, context);
    }
    return true;
}

bool Renderer::RecomputeFOV()
{
    if (GetFOVProc) {
        GetFOVProc(fov, context);
        return true;
    }

    return false;
}

bool Renderer::GetFOV(double& pfov) const
{
    FN_TIMER
    pfov = fov;
    return true;
}

bool Renderer::CopyLuminanceFromScreen(int x, int y, int w, int h, unsigned char *buf)
{
    if (CopyLuminanceFromScreenProc) {
        return CopyLuminanceFromScreenProc(x, y, w, h, buf);
    }

    return false;
}

bool Renderer::CopyLuminanceIntoTexture(TextureHandle texture, int w, int h, unsigned char *buf)
{
    if (CopyLuminanceIntoTextureProc) {
        return CopyLuminanceIntoTextureProc(texture, w, h, buf);
    }

    return false;
}

bool Renderer::CreateLuminanceTexture(int w, int h, TextureHandle *texture, const char* name)
{
    if (CreateLuminanceTextureProc) {
        return CreateLuminanceTextureProc(w, h, texture, name);
    }

    return false;
}

void Renderer::SetContext(void* _context)
{
    SL_ASSERT(context == _context);
    if (SetContextProc) {
        SetContextProc(context);
    }
}

void Renderer::FrameStarted(void* _context)
{
    SL_ASSERT(context == _context);
    FN_TIMER

    if (SetContextProc) {
        SetContextProc(context);
    }

    blendedObjects.clear();

    // Cache all frame-specific data that would stall the pipeline to retrieve
    if (!hasExplicitViewport && GetViewportProc) {
        GetViewportProc(vpX, vpY, vpW, vpH);
    }

    if (!hasExplicitDepthRange && GetDepthRangeProc) {
        GetDepthRangeProc(minZ, maxZ);
    }

    if (GetFOVProc && !avoidStalls) {
        GetFOVProc(fov, context);
    }

    if (GetTextureMatrixProc) {
        //GetTextureMatrixProc(&texture);
        Matrix4 identity;
        texture = identity;
    }

    if (!hasExplicitMatrices && GetModelviewMatrixProc) {
        GetModelviewMatrixProc(&modelview, context);
    }

    blendedObjects.clear();
}

const RenderableListener* Renderer::GetListener(void) const
{
    return listener;
}
RenderableListener* Renderer::GetListener(void)
{
    return listener;
}

void Renderer::SubmitBlendedObject(Renderable *obj)
{
    if (obj) {
        obj->AddListenerIfNecessary(listener);
        blendedObjects.push_back(obj);
    }
}

void Renderer::ClearBlendedObjects()
{
    blendedObjects.clear();
    currentBlendedObjectBeingDrawn = -1;
}

void Renderer::RemoveBlendedObject(Renderable *obj)
{
    SL_VECTOR(Renderable *) ::iterator it;
    for (it = blendedObjects.begin(); it != blendedObjects.end(); it++) {
        if (*it == obj) {
            blendedObjects.erase(it);
            return;
        }
    }
}

struct RenderableCompare {
    RenderableCompare(const Camera* _camera, const Matrix4& _sortMV, const Vector3& _sortCamPos, bool _sortRightHanded, ThreadCameraStreamData* _tcsData)
        : camera(_camera)
        , sortMV(_sortMV)
        , sortCamPos(_sortCamPos)
        , sortRightHanded(_sortRightHanded)
        , tcsData(_tcsData)
    {

    }
    bool operator()(const Renderable *c1, const Renderable *c2)
    {

        if (c1 == 0 || c2 == 0) return false;
        if (c1 == c2) return false;

        if (camera) {
            Vector3 p1, p2;
            if (c1) p1 = c1->GetSortPosition(camera, tcsData);
            if (c2) p2 = c2->GetSortPosition(camera, tcsData);

            double d1 = c1 ? c1->GetDistance(sortCamPos, p2, sortMV, sortRightHanded, camera, tcsData) + c1->GetDepthOffset() : 0;
            double d2 = c2 ? c2->GetDistance(sortCamPos, p1, sortMV, sortRightHanded, camera, tcsData) + c2->GetDepthOffset() : 0;

            return (d1 > d2);
        } else {
            return false;
        }
    }
private:
    const Camera* camera;
    const Matrix4 sortMV;
    const Vector3 sortCamPos;
    const bool sortRightHanded;
    ThreadCameraStreamData* tcsData;
};


void Renderer::SortAndDrawBlendedObjects(bool enableDepthTest, bool enableDepthWrites, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
        , const OverriddenCameraPos& overriddenCameraPos
        , bool fadeOverride
        , ThreadCameraStreamData* tcsData)
{
    SortBlendedObjects(atmosphere, sceneCamera, renderCamera, overriddenCameraPos, tcsData);
    DrawBlendedObjects(enableDepthTest, enableDepthWrites, atmosphere, sceneCamera, renderCamera, overriddenBillboardMatrix, overriddenCameraPos, fadeOverride, tcsData);
}

void Renderer::SortBlendedObjects(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, ThreadCameraStreamData* tcsData)
{
    sort(blendedObjects.begin(), blendedObjects.end(), RenderableCompare(sceneCamera, renderCamera->GetModelViewMatrix(), renderCamera->GetPosition(), rightHanded, tcsData));
}

void Renderer::DrawBlendedObjects(bool enableDepthTest, bool enableDepthWrites, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride, ThreadCameraStreamData* tcsData)
{
    SL_ASSERT(this == tcsData->GetRenderer());
    // Set the blend and z buffer modes
    EnableBlending(ONE, INVSRCALPHA);
    EnableDepthReads(enableDepthTest);
    if (enableDepthWrites) EnableDepthWrites(true);
    disableDepthReads = !enableDepthTest;
    disableDepthWrites = enableDepthWrites;
    EnableDepthWrites(false);
    EnableTexture2D(true);
    EnableLighting(false);

    currentBlendedObjectBeingDrawn = -1;
    for (SL_VECTOR(Renderable *)::iterator it = blendedObjects.begin(); it != blendedObjects.end(); it++) {
        ++currentBlendedObjectBeingDrawn;
        (*it)->DrawBlendedObject(atmosphere, sceneCamera, renderCamera, overriddenBillboardMatrix, overriddenCameraPos, fadeOverride, tcsData);
    }

    EnableDepthWrites(true);
    disableDepthReads = false;
    disableDepthWrites = false;
}

bool Renderer::GetIsDirectX() const
{
    return (type == Atmosphere::DIRECTX9 || type == Atmosphere::DIRECTX11
            || type == Atmosphere::DIRECTX11_1);
}

bool Renderer::GetIsOpenGL() const
{
    return (type == Atmosphere::OPENGL || type == Atmosphere::OPENGL32CORE);
}

bool Renderer::GetIsVulkan() const
{
    return type == Atmosphere::VULKAN;
}

void *Renderer::GetNativeTexture(TextureHandle tex) const
{
    if (GetNativeTextureProc) {
        return GetNativeTextureProc(tex);
    }
    return 0;
}

Matrix4 Renderer::GetScaleAndBiasMatrix(void) const
{
    if (GetIsDirectX()) { // Should this also apply to Vulkan? I don't think so, but if people report problems applying the shadow map this is the first place to look
        return Matrix4(
                   0.5, 0.0, 0.0, 0.5,
                   0.0, -0.5, 0.0, 0.5,
                   0.0, 0.0, 0.0, 1.0,
                   0.0, 0.0, 0.0, 1.0
               );
    } else {
        return Matrix4(
                   0.5f, 0.0f, 0.0f, 0.5f,
                   0.0f, 0.5f, 0.0f, 0.5f,
                   0.0f, 0.0f, 0.5f, 0.5f,
                   0.0f, 0.0f, 0.0f, 1.0f
               );
    }
}

bool Renderer::AreEqual(const Camera* camera)
{
    Matrix4 theModelView;
    GetModelviewMatrix(&theModelView);

    bool mv_equal = (theModelView == camera->GetModelViewMatrix());

    return mv_equal;
}

bool Renderer::HasInstancing(void) const
{
    if (HasInstancingProc) {
        return HasInstancingProc();
    }
    return false;
}
bool Renderer::HasBindlessTextures(void) const
{
    if (HasBindlessTexturesProc) {
        return HasBindlessTexturesProc();
    }
    return false;
}
bool Renderer::HasUBOs(void) const
{
    if (HasUBOsProc) {
        return HasUBOsProc();
    }
    return false;
}
bool Renderer::HasSSBOs(void) const
{
    if (HasSSBOsProc) {
        return HasSSBOsProc();
    }
    return false;
}
bool Renderer::Has64BitSupport(void) const
{
    if (Has64BitSupportProc) {
        return Has64BitSupportProc();
    }
    return false;
}
bool Renderer::HasBindlessIndirectRendering(void) const
{
    if (HasBindlessIndirectRenderingProc) {
        return HasBindlessIndirectRenderingProc();
    }
    return false;
}

Buffer* Renderer::CreateBuffer(const char* name, BufferType bufferType, int size, const BufferProperties& _bufferProperties)
{
    if (CreateBufferProc) {
        return CreateBufferProc(name, bufferType, size, _bufferProperties, context);
    }
    return 0;
}

void Renderer::DestroyBuffer(Buffer* buffer)
{
    if (DestroyBufferProc) {
        DestroyBufferProc(buffer, context);
    }
}

GPUAddressType Renderer::GetTextureAddress(TextureHandle texture)
{
    if (GetTextureAddressProc && texture) {
        return GetTextureAddressProc(texture);
    }
    return 0;
}
void Renderer::MakeTextureAddressResident(GPUAddressType textureAddress)
{
    if (MakeTextureAddressResidentProc) {
        MakeTextureAddressResidentProc(textureAddress);
    }
}

GPUAddressType Renderer::GetBufferAddress(BufferHandle buffer)
{
    if (GetBufferAddressProc) {
        return GetBufferAddressProc(buffer);
    }
    return 0;
}
void Renderer::MakeBufferResident(BufferHandle buffer, bool makeResident)
{
    if (MakeBufferResidentProc) {
        MakeBufferResidentProc(buffer, makeResident);
    }
}
bool Renderer::IsBufferResident(BufferHandle buffer)
{
    if (IsBufferResidentProc) {
        return IsBufferResidentProc(buffer);
    }
    return false;
}


void Renderer::SetForceImmediate(bool val)
{
    if (SetForceImmediateProc) {
        SetForceImmediateProc(val, context);
    }
}

//! internal. do not use!
bool Renderer::GetForceImmediate(void) const
{
    if (GetForceImmediateProc) {
        return GetForceImmediateProc(context);
    }
    return false;
}
bool Renderer::ExecuteStream(void)
{
    if (ExecuteStreamProc) {
        ExecuteStreamProc(context);
        return true;
    }
    return false;
}

void Renderer::SetStream(void* stream, int frameIndex)
{
    if (SetStreamProc) {
        SetStreamProc(stream, frameIndex, context);
    }
}

void Renderer::SettingsChanged(void* _environment)
{
    if (SettingsChangedProc) {
        SettingsChangedProc(_environment, context);
    }
}
void* Renderer::GetContext(void)
{
    return context;
}

bool Renderer::SupportsInstancedRendering() const
{
    if (GetType() != Atmosphere::OPENGL32CORE && GetType()!=Atmosphere::OPENGL && GetType()!=Atmosphere::VULKAN) {
        return false;
    }
    return HasInstancing()
           && HasBindlessTextures()
           && HasUBOs()
           && HasSSBOs()
           && Has64BitSupport();
}

bool Renderer::SupportsBindlessIndirectRendering() const
{
    if (HasBindlessIndirectRenderingProc) {
        return HasBindlessIndirectRenderingProc();
    }
    return false;
}

bool Renderer::SupportsConstantLocations(void) const
{
    return (GetType() == Atmosphere::OPENGL32CORE || GetType() == Atmosphere::OPENGL || GetType() == Atmosphere::VULKAN);
}
}
