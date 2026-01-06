// Copyright (c) 2004-2023  Sundog Software, LLC All rights reserved worldwide.

/**
    \file SilverLiningDLLCommon.h
    \brief Shared header for the specific renderer DLL's, and for use by the Renderer
    class of the engine.
 */

#ifndef SILVERLININGDLLCOMMON_H
#define SILVERLININGDLLCOMMON_H

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "Color.h"
#include "Frustum.h"
#include "Matrix4.h"
#include "Vertex.h"
#include "ResourceLoader.h"
#include "Buffer.h"
#include "BufferProperties.h"
#include <vector>

// Passes in settings from Atmosphere::Initialize, including the env pointer that you can use to pass
// along device information or pointers to your engine. This is a good point to do any required initialization.
// Return a "context" object of your choosing, that will be passed back to you via SetContext() whenever the
// current Atmosphere changes.
SILVERLININGDLL_API void *           SetEnvironment(bool rightHanded, void *env, SilverLining::ResourceLoader *resourceLoader, SilverLining::Allocator *allocator,
    const char* userDefinedVertString, const char* userDefinedFragString, bool avoidStalls, const char *userVertFilename, const char *userFragFilename, bool useUBos, 
    bool deferred, const SL_VECTOR(unsigned int)& userShaders);
typedef void *                       (*SET_ENV_PROC)(bool, void *, SilverLining::ResourceLoader *, SilverLining::Allocator *, const char*, const char*, bool, const char *, const char *, bool, bool, const SL_VECTOR(unsigned int)&);

SILVERLININGDLL_API void             DeviceLost(void *);
typedef void                        (*DEVICE_LOST_PROC)(void *);

SILVERLININGDLL_API void             DeviceReset(void *);
typedef void                         (*DEVICE_RESET_PROC)(void *);

// Called whenever the current Atmosphere changes, generally indicated a different viewport / render context
// is now in effect. The input is the object returned by SetEnvironment for this Atmosphere.
SILVERLININGDLL_API void             SetContext(void *context);
typedef void (*SET_CONTEXT_PROC)(void *);

SILVERLININGDLL_API bool HasInstancing(void);
typedef bool (*HAS_INSTANCING_PROC)(void);

SILVERLININGDLL_API bool HasBindlessTextures(void);
typedef bool(*HAS_BINDLESS_TEXTURES_PROC)(void);

SILVERLININGDLL_API bool HasUBOs(void);
typedef bool(*HAS_UBOS_PROC)(void);

SILVERLININGDLL_API bool HasSSBOs(void);
typedef bool(*HAS_SSBOS_PROC)(void);

SILVERLININGDLL_API bool Has64BitSupport(void);
typedef bool(*HAS_64BIT_SUPPORT)(void);

SILVERLININGDLL_API bool HasBindlessIndirectRendering(void);
typedef bool(*HAS_BINDLESS_INDIRECT_RENDERING)(void);

SILVERLININGDLL_API SilverLining::Buffer* CreateBuffer(const char* name, SilverLining::BufferType type, int size, const SilverLining::BufferProperties& _bufferProperties, void* _context);
typedef SilverLining::Buffer* (*CREATE_BUFFER_PROC)(const char* name, SilverLining::BufferType type, int size, const SilverLining::BufferProperties& , void* _context);

SILVERLININGDLL_API void DestroyBuffer(SilverLining::Buffer* buffer, void* _context);
typedef void (*DESTROY_BUFFER_PROC)(SilverLining::Buffer* buffer, void* _context);

SILVERLININGDLL_API SilverLining::GPUAddressType GetTextureAddress(SilverLining::TextureHandle textureHandle);
typedef SilverLining::GPUAddressType (*GET_TEXTURE_ADDRESS_PROC)(SilverLining::TextureHandle textureHandle);

SILVERLININGDLL_API void MakeTextureAddressResident(SilverLining::GPUAddressType textureAddress);
typedef void (*MAKE_TEXTURE_ADDRESS_RESIDENT_PROC)(SilverLining::GPUAddressType textureAddress);


SILVERLININGDLL_API SilverLining::GPUAddressType GetBufferAddress(SilverLining::BufferHandle bufferHandle);
typedef SilverLining::GPUAddressType(*GET_BUFFER_ADDRESS_PROC)(SilverLining::BufferHandle bufferHandle);

SILVERLININGDLL_API void MakeBufferResident(SilverLining::BufferHandle bufferHandle, bool resident);
typedef void(*MAKE_BUFFER_RESIDENT_PROC)(SilverLining::BufferHandle bufferHandle, bool resident);

SILVERLININGDLL_API bool IsBufferResident(SilverLining::BufferHandle bufferHandle);
typedef bool(*IS_BUFFER_RESIDENT_PROC)(SilverLining::BufferHandle bufferHandle);

// Clear the color buffer to the specified color.
SILVERLININGDLL_API void             ClearScreen(const SilverLining::Color& c, void* _context);
typedef void (*CLEAR_SCREEN_PROC)(const SilverLining::Color&, void* _context);

// Clear the depth buffer.
SILVERLININGDLL_API void             ClearDepth(void* _context);
typedef void (*CLEAR_DEPTH_PROC)(void*);

// Return the position and size of the current viewport.
SILVERLININGDLL_API bool             GetViewport(int& x, int& y, int& w, int& h);
typedef bool (*GET_VIEWPORT_PROC)(int&, int&, int&, int&);

// Sets the position and size of the current viewport.
SILVERLININGDLL_API bool             SetViewport(int x, int y, int w, int h, void* _context);
typedef bool (*SET_VIEWPORT_PROC)(int, int, int, int, void*);

// Return depth range.
SILVERLININGDLL_API bool             GetDepthRange(float& zmin, float& zmax);
typedef bool (*GET_DEPTH_RANGE_PROC)(float& , float& );

// Set depth range.
SILVERLININGDLL_API bool             SetDepthRange(float zmin, float zmax, void* _context);
typedef bool (*SET_DEPTH_RANGE_PROC)(float, float, void*);

// Set depth range.
SILVERLININGDLL_API bool             SetReverseZ(bool reverse, void* _context);
typedef bool (*SET_REVERSE_Z_PROC)(bool, void*);

// Return the vertical field of view in radians.
SILVERLININGDLL_API bool             GetFOV(double& fov, void* _context);
typedef bool (*GET_FOV_PROC)(double&, void*);

// Load a shader from a source file. The filename will have the suffix ".cg" - swap this out if you're using
// a different shader suffix (such as .fx). Return a handle for referring to this shader later on.
SILVERLININGDLL_API SilverLining::ShaderHandle     LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling, const SL_VECTOR(SL_STRING)& defines, void* _context);
typedef SilverLining::ShaderHandle(*LOAD_SHADER_FROM_FILE_PROC)(const char *, const char *, const char *, bool, const SL_VECTOR(SL_STRING)&, void*);

// OK to stub out and return NULL; this is no longer called.
SILVERLININGDLL_API SilverLining::ShaderHandle     LoadShaderFromSource(const char *shaderSource);
typedef SilverLining::ShaderHandle (*LOAD_SHADER_FROM_SOURCE_PROC)(const char *);

/** Sets a user defined string to be prepended to all vertex shaders.
 */
SILVERLININGDLL_API void SetUserDefinedVertString( const char *userDefinedString );
typedef void (*SET_USER_DEFINED_VERT_STRING_PROC)( const char *userDefinedString );

/** Sets a user defined string to be prepended to all fragment shaders.
 */
SILVERLININGDLL_API void SetUserDefinedFragString( const char *userDefinedString );
typedef void (*SET_USER_DEFINED_FRAG_STRING_PROC)( const char *userDefinedString );

/** Retrieves the user defined vertex string previously set with SetUserDefinedVertString.
 */
SILVERLININGDLL_API const char * GetUserDefinedVertString();
typedef const char * (*GET_USER_DEFINED_VERT_STRING_PROC)();

/** Retrieves the user defined fragment string previously set with SetUserDefinedFragString.
 */
SILVERLININGDLL_API const char * GetUserDefinedFragString();
typedef const char * (*GET_USER_DEFINED_FRAG_STRING_PROC)();

/** OpenGL32, OpenGL only: Get the constant location of a uniform or an offset into a UBO for that uniform. */
SILVERLININGDLL_API int             GetConstantLocation(SilverLining::ShaderHandle shader, const char *varName);
typedef int(*GET_CONSTANT_LOCATION_PROC)(SilverLining::ShaderHandle, const char *);

/** OpenGL32, OpenGL only: Set a constant  vec4 at a given location/offset into UBO in the shader */
SILVERLININGDLL_API void             SetConstantVector4AtLocation(SilverLining::ShaderHandle shader, int loc, const float* data, void* _context);
typedef void(*SET_CONSTANT_VECTOR4_AT_LOCATION_PROC)(SilverLining::ShaderHandle, int loc, const float* data, void* _context);

/** OpenGL32, OpenGL only: Set a constant  mat4 at a given location/offset into UBO in the shader */
SILVERLININGDLL_API void             SetConstantMatrix4AtLocation(SilverLining::ShaderHandle shader, int loc, float* data, void* _context);
typedef void(*SET_CONSTANT_MATRIX4_AT_LOCATION_PROC)(SilverLining::ShaderHandle, int loc, float* data, void* _context);

// Sets a 4D vector constant for the specified shader.
SILVERLININGDLL_API bool             SetConstantVector4(SilverLining::ShaderHandle shader, const char *varName, const float *data);
typedef bool (*SET_CONSTANT_VECTOR4_PROC)(SilverLining::ShaderHandle, const char *, const float *);

SILVERLININGDLL_API bool             SetConstantInt(SilverLining::ShaderHandle shader, const char* varName, int data);
typedef bool(*SET_CONSTANT_INT_PROC)(SilverLining::ShaderHandle, const char*, int);

// Sets a 4x4 matrix constant for the specified shader.
SILVERLININGDLL_API bool             SetConstantMatrix4(SilverLining::ShaderHandle shader, const char *varName, float *data);
typedef bool (*SET_CONSTANT_MATRIX4_PROC)(SilverLining::ShaderHandle, const char *, float *);

// Set up renderer specific things before constants can be set
SILVERLININGDLL_API bool             PreConstantsSet(SilverLining::ShaderHandle shader);
typedef bool(*PRE_CONSTANTS_SET_PROC)(SilverLining::ShaderHandle);

// Set up renderer specific things *after* constants are set
SILVERLININGDLL_API bool             PostConstantsSet(SilverLining::ShaderHandle shader);
typedef bool(*POST_CONSTANTS_SET_PROC)(SilverLining::ShaderHandle);

// Make the specified shader active.
SILVERLININGDLL_API bool             BindShader(SilverLining::ShaderHandle shader, void* _context);
typedef bool (*BIND_SHADER_PROC)(SilverLining::ShaderHandle, void*);

// Make the specified shader not active (this can be a no-op)
SILVERLININGDLL_API bool             UnbindShader(void* _context);
typedef bool (*UNBIND_SHADER_PROC)(void*);

SILVERLININGDLL_API bool             IncrementConstantBuffersOffset(SilverLining::ShaderHandle shader, void* _context);
typedef bool (*INCREMENTCONSTANTSBUFFEROFFSET_PROC)(SilverLining::ShaderHandle, void*);

// Delete a shader ahead of shader system shutdown.
SILVERLININGDLL_API void             DeleteShader(SilverLining::ShaderHandle shader, void* _context);
typedef void (*DELETE_SHADER_PROC)(SilverLining::ShaderHandle, void*);

// Called when the Atmosphere is destroyed; perform any cleanup here.
SILVERLININGDLL_API bool             ShutdownShaderSystem(void *context);
typedef bool (*SHUTDOWN_SHADER_SYSTEM_PROC)(void *);

// Allocate a vertex buffer of some sort (using the Vertex format in Vertex.h) for the given number of vertices.
SILVERLININGDLL_API void *           AllocateVertexBuffer(int nVerts, const char* name, const SilverLining::BufferProperties& bufferProperties, void* _context);
typedef void *                       (*ALLOCATE_VERTEX_BUFFER_PROC)(int, const char*, const SilverLining::BufferProperties& , void*);

// Lock the given vertex buffer such that it may be directly modified by the CPU.
SILVERLININGDLL_API bool             LockVertexBuffer(void *buffer);
typedef bool (*LOCK_VERTEX_BUFFER_PROC)(void *);

// Return a pointer to the vertex data of the given locked vertex buffer, such that modifications to it will
// be reflected when unlocked.
SILVERLININGDLL_API SilverLining::Vertex * GetVertices(void *buffer, void* _context);
typedef SilverLining::Vertex *       (*GET_VERTICES_PROC)(void*, void*);

// Unlocks the given vertex buffer - it may be inaccessible to the CPU after this.
SILVERLININGDLL_API bool             UnlockVertexBuffer(void *buffer, void* _context);
typedef bool (*UNLOCK_VERTEX_BUFFER_PROC)(void*, void*);

// Return a locked pointer to the vertex data for this buffer. This may be stubbed out by just returning false, and
// SilverLining will fall back to different methods.
SILVERLININGDLL_API bool    GetVertexBuffer(void *buffer, int offset, SilverLining::Vertex *verts, int nVerts, void* _context);
typedef bool (*GET_VERTEX_BUFFER_PROC)(void*, int, SilverLining::Vertex*, int, void*);

// Update the vertex data in the given buffer with the data passed in. This may be stubbed out by just returning false,
// and SilverLining will fall back to different methods.
SILVERLININGDLL_API bool    UpdateVertexBuffer(void *buffer, int offset, SilverLining::Vertex *verts, int nVerts, bool justColors, void* _context);
typedef bool(*UPDATE_VERTEX_BUFFER_PROC)(void*, int, SilverLining::Vertex*, int, bool, void*);

// Dispose of the given vertex buffer.
SILVERLININGDLL_API bool             ReleaseVertexBuffer(void *buffer, void* _context);
typedef bool (*RELEASE_VERTEX_BUFFER_PROC)(void*, void*);

// Make the specified vertex buffer active. If vertexColors is true, use the color information in the vertex data.
SILVERLININGDLL_API bool             SetVertexBuffer(void *buffer, bool vertexColors, void* _context);
typedef bool (*SET_VERTEX_BUFFER_PROC)(void *, bool, void*);

// Make the specified vertex buffer no longer active. This may be a no-op.
SILVERLININGDLL_API bool             UnsetVertexBuffer(void* _context);
typedef bool (*UNSET_VERTEX_BUFFER_PROC)(void*);

SILVERLININGDLL_API bool             IncrementOffsetIndex(void* buffer, void* _context);
typedef bool (*INCREMENT_OFFSET_INDEX_PROC)(void*, void*);

// Allocate an index buffer of shorts for the given number of indices.
SILVERLININGDLL_API void *           AllocateIndexBuffer(int nIndices, const char* name, const SilverLining::BufferProperties& bufferProperties, void* _context);
typedef void *                       (*ALLOCATE_INDEX_BUFFER_PROC)(int, const char*, const SilverLining::BufferProperties&, void*);

// Lock the given index buffer for writing.
SILVERLININGDLL_API bool             LockIndexBuffer(void *buffer, void* _context);
typedef bool (*LOCK_INDEX_BUFFER_PROC)(void *, void*);

// Retrieve a pointer to the locked index data for writing.
SILVERLININGDLL_API SilverLining::Index * GetIndices(void *buffer, void* _context);
typedef SilverLining::Index *       (*GET_INDICES_PROC)(void *, void*);

// Unlock the given index buffer. Writes may no longer be possible after calling this.
SILVERLININGDLL_API bool             UnlockIndexBuffer(void *buffer, void* _context);
typedef bool (*UNLOCK_INDEX_BUFFER_PROC)(void *, void*);

// Dispose of the given index buffer.
SILVERLININGDLL_API bool             ReleaseIndexBuffer(void *buffer, void* _context);
typedef bool (*RELEASE_INDEX_BUFFER_PROC)(void*, void*);

// Make the given index buffer active.
SILVERLININGDLL_API bool             SetIndexBuffer(void *buffer, void* _context);
typedef bool (*SET_INDEX_BUFFER_PROC)(void *, void*);

// Make the given index buffer no longer active (this may be a no-op)
SILVERLININGDLL_API bool             UnsetIndexBuffer(void* _context);
typedef bool (*UNSET_INDEX_BUFFER_PROC)(void*);

// Draw a line strip using the currently active vertex buffer.
SILVERLININGDLL_API bool            DrawLineStrip(void* vbh, const SilverLining::Color& c, int numVerts, float width, void* _context);
typedef bool(*DRAW_LINE_STRIP_PROC)(void*, const SilverLining::Color&, int, float, void*);

// Draw a triangle strip using the specified index buffer and index range.
SILVERLININGDLL_API bool             DrawStrip(void *indexBuffer, int startIdx, int nIndices, int numVerts, bool preDraw, void* _context);
typedef bool (*DRAW_STRIP_PROC)(void *, int, int, int, bool, void*);

// Draw a triangle strip using the specified index buffer and index range.
SILVERLININGDLL_API bool             DrawMultiStrips(void* commandBufferHandle, void* _context);
typedef bool(*DRAW_MULTI_STRIPS_PROC)(void*, void*);

// Draw a triangle strip using the specified index buffer and index range.
SILVERLININGDLL_API bool             DrawStripInstanced(void *indexBuffer, int startIdx, int nIndices, int numVerts, int instanceCount, int firstInstance, void* _context);
typedef bool(*DRAW_STRIP_INSTANCED_PROC)(void *, int, int, int, int, int, void*);

// Draw a series of points of the given point size using the currently active vertex buffer.
SILVERLININGDLL_API bool             DrawPoints(double pointSize, int nPoints, int start, void* _context);
typedef bool (*DRAW_POINTS_PROC)(double, int, int, void*);

// Return false unless you want to implement DrawQuads(). SilverLining will fall back to triangle strips.
SILVERLININGDLL_API bool             HasQuads();
typedef bool (*HAS_QUADS_PROC)(void);

SILVERLININGDLL_API bool             DrawQuads(int nPoints, int start, void* _context);
typedef bool (*DRAW_QUADS_PROC)(int, int, void*);

// Set whether depth writes are enabled.
SILVERLININGDLL_API bool             EnableDepthWrites(bool, void* _context);
typedef bool (*ENABLE_DEPTH_WRITES_PROC)(bool, void*);

// Set whether depth reads are enabled.
SILVERLININGDLL_API bool             EnableDepthReads(bool, void* _context);
typedef bool(*ENABLE_DEPTH_READS_PROC)(bool, void*);

// Set whether 2D texture mapping is enabled.
SILVERLININGDLL_API bool             EnableTexture2D(bool, void* _context);
typedef bool (*ENABLE_TEXTURE_2D_PROC)(bool, void*);

// Set whether 2D texture mapping is enabled.
SILVERLININGDLL_API bool             EnableTexture3D(bool, void* _context);
typedef bool (*ENABLE_TEXTURE_3D_PROC)(bool, void*);

// Set whether backface culling is enabled.
SILVERLININGDLL_API bool             EnableBackfaceCulling(bool, void* _context);
typedef bool(*ENABLE_BACKFACE_CULLING_PROC)(bool, void*);

// Set which faces are culled
SILVERLININGDLL_API bool             BackfaceCullClockwise(bool, void* _context);
typedef bool(*BACKFACE_CULL_CLOCKWISE_PROC)(bool, void*);

// Set whether exponential fog is enabled.
SILVERLININGDLL_API bool             EnableFog(bool, void* _context);
typedef bool (*ENABLE_FOG_PROC)(bool, void*);

// Configure exponential fog using the specified density and color. Start and end are currently ignored.
SILVERLININGDLL_API bool    ConfigureFog(double density, double start, double end, const SilverLining::Color& c, void* _context);
typedef bool (*CONFIGURE_FOG_PROC)(double, double, double, const SilverLining::Color&, void*);

// Set whether per-vertex lighting is enabled.
SILVERLININGDLL_API bool             EnableLighting(bool, void* _context);
typedef bool (*ENABLE_LIGHTING_PROC)(bool, void*);

// Set the current color for use when vertex colors are disabled.
SILVERLININGDLL_API bool             SetCurrentColor(const SilverLining::Color& c, void* _context);
typedef bool (*SET_CURRENT_COLOR_PROC)(const SilverLining::Color&, void*);

// Set the current projection matrix.
SILVERLININGDLL_API bool             SetProjectionMatrix(const SilverLining::Matrix4& m, void* _context);
typedef bool (*SET_PROJECTION_MATRIX_PROC)(const SilverLining::Matrix4&, void*);

// Get the currently active texture matrix.
SILVERLININGDLL_API bool    GetTextureMatrix(SilverLining::Matrix4 *m, void* _context);
typedef bool (*GET_TEXTURE_MATRIX_PROC)(SilverLining::Matrix4 *m, void*);

// Set the currently active texture matrix.
SILVERLININGDLL_API bool    SetTextureMatrix(const SilverLining::Matrix4& m, void* _context);
typedef bool(*SET_TEXTURE_MATRIX_PROC)(const SilverLining::Matrix4&, void*);

// Get the current view matrix.
SILVERLININGDLL_API bool             GetModelviewMatrix(SilverLining::Matrix4 *m, void* _context);
typedef bool(*GET_MODELVIEW_MATRIX_PROC)(SilverLining::Matrix4 *m, void*);

// Set the current view matrix.
SILVERLININGDLL_API bool             SetModelviewMatrix(const SilverLining::Matrix4& m, void* _context);
typedef bool (*SET_MODELVIEW_MATRIX_PROC)(const SilverLining::Matrix4&, void*);

// Push all rendering state onto the stack.
SILVERLININGDLL_API bool             PushAllState(void* _context);
typedef bool (*PUSH_ALL_STATE_PROC)(void*);

// Restore all rendering state from the stack.
SILVERLININGDLL_API bool             PopAllState(void* _context);
typedef bool (*POP_ALL_STATE_PROC)(void*);

// Set default rendering state.
SILVERLININGDLL_API bool    SetDefaultState(void* _context);
typedef bool (*SET_DEFAULT_STATE_PROC)(void*);

// heart beat function
SILVERLININGDLL_API bool HeartBeat(void);
typedef bool(*HEART_BEAT_PROC)(void);

// context deletion function
SILVERLININGDLL_API bool ContextBeingDeleted(void*);
typedef bool(*CONTEXT_BEING_DELETED_PROC)(void*);

// Enable the given blending mode.
SILVERLININGDLL_API bool             EnableBlending(int srcFactor, int dstFactor, void* _context);
typedef bool (*ENABLE_BLENDING_PROC)(int, int, void*);

// Disable blending.
SILVERLININGDLL_API bool             DisableBlending(void* _context);
typedef bool (*DISABLE_BLENDING_PROC)(void*);

// Load a 2D texture from the given filename. Return a handle to the texture in texHandle.
// u,v wrapping is specified in the boolean parameters.
SILVERLININGDLL_API bool             LoadTextureFromFile(const char *imgPath, SilverLining::TextureHandle *texHandle, bool wrapU, bool wrapV, const char* name, void* _context);
typedef bool (*LOAD_TEXTURE_FROM_FILE_PROC)(const char *, SilverLining::TextureHandle *, bool, bool, const char*, void*);

// Returns whether floating point textures are supported. This is only needed for glare effects, which are disabled
// by default, so you can return false and be done with it and with implementing LoadFloatTextureRGB / LoadFloatTexture.
SILVERLININGDLL_API bool             HasFloatTextures();
typedef bool (*HAS_FLOAT_TEXTURES_PROC)(void);

SILVERLININGDLL_API bool             LoadFloatTextureRGB(const float *data, int width, int height, SilverLining::TextureHandle *texHandle, const char* name, void* _context);
typedef bool (*LOAD_FLOAT_TEXTURE_RGB_PROC)(const float *, int, int, SilverLining::TextureHandle *, const char*, void*);

SILVERLININGDLL_API bool             LoadFloatTexture(const float *data, int width, int height, SilverLining::TextureHandle *texHandle, const char* name);
typedef bool (*LOAD_FLOAT_TEXTURE_PROC)(const float *, int, int, SilverLining::TextureHandle *, const char*);

// Load a 2D texture from the bytes passed in explicitly.
SILVERLININGDLL_API bool             LoadTexture(const unsigned char *data, int width, int height, SilverLining::TextureHandle *texHandle, bool, bool, const char*, void*);
typedef bool (*LOAD_TEXTURE_PROC)(const unsigned char *, int, int, SilverLining::TextureHandle *, bool, bool, const char*, void*);

// Load a 3D texture from the bytes passed in explicitly (8 bits per texel).
SILVERLININGDLL_API bool             Load3DTexture(const unsigned char *data, int width, int height, int depth, SilverLining::TextureHandle *texHandle, bool, bool, bool, const char*);
typedef bool (*LOAD_3D_TEXTURE_PROC)(const unsigned char *, int, int, int, SilverLining::TextureHandle *, bool, bool, bool, const char*);

// Load a 3D texture from the bytes passed in explicitly (24 bits per texel).
SILVERLININGDLL_API bool             Load3DTextureRGB(const unsigned char *data, int width, int height, int depth, SilverLining::TextureHandle *texHandle, bool, bool, bool, const char*, void*);
typedef bool (*LOAD_3D_TEXTURE_RGB_PROC)(const unsigned char *, int, int, int, SilverLining::TextureHandle *, bool, bool, bool, const char*, void*);

// Load a 3D texture from the bytes passed in explicitly (16 bits per texel).
SILVERLININGDLL_API bool             Load3DTextureLA(const unsigned char *data, int width, int height, int depth, SilverLining::TextureHandle *texHandle, bool, bool, bool, const char*, void*);
typedef bool (*LOAD_3D_TEXTURE_LA_PROC)(const unsigned char *, int, int, int, SilverLining::TextureHandle *, bool, bool, bool, const char*, void*);

// Update a 3D texture block from the bytes passed in explicitly (16 bits per texel).
SILVERLININGDLL_API bool             SubLoad3DTextureLA( const unsigned char * data, int width, int height, int depth, int x, int y, int z, int dataRowPitch, int dataSlicePitch, SilverLining::TextureHandle texHandle, void* _context);
typedef bool(*SUBLOAD_3D_TEXTURE_LA_PROC)(const unsigned char *, int, int, int, int, int, int, int, int, SilverLining::TextureHandle, void*);

// Load a 3D texture from the bytes passed in explicitly (16 bits per texel).
SILVERLININGDLL_API bool             Load3DTextureFloat(const float *data, int width, int height, int depth, SilverLining::TextureHandle *texHandle, bool, bool, bool, const char*);
typedef bool(*LOAD_3D_TEXTURE_FLOAT_PROC)(const float *, int, int, int, SilverLining::TextureHandle *, bool, bool, bool, const char*);

// Retrieves a pointer to the platform-specific texture handle containing the texture. On OpenGL this is a GLuint; on DirectX9 
// it is a IDirect3DTexture9 *; on DirectX11 it is a ID3D11ShaderResourceView *
SILVERLININGDLL_API void *           GetNativeTexture(SilverLining::TextureHandle texHandle);
typedef void * (*GET_NATIVE_TEXTURE_PROC)(SilverLining::TextureHandle);

// Enable the given 2D texture on the given texture layer.
SILVERLININGDLL_API bool             EnableTexture(SilverLining::TextureHandle texture, int stage, void* _context);
typedef bool (*ENABLE_TEXTURE_PROC)(SilverLining::TextureHandle, int, void*);

// Enable the given 3D texture on the given texture layer.
SILVERLININGDLL_API bool             Enable3DTexture(SilverLining::TextureHandle texture, int stage, void* _context);
typedef bool (*ENABLE_3D_TEXTURE_PROC)(SilverLining::TextureHandle, int, void*);

// Disable the current texture on the given texture layer.
SILVERLININGDLL_API bool             DisableTexture(int stage, void* _context);
typedef bool (*DISABLE_TEXTURE_PROC)(int, void*);

// Release the given texture's resources.
SILVERLININGDLL_API bool             ReleaseTexture(SilverLining::TextureHandle texture);
typedef bool (*RELEASE_TEXTURE_PROC)(SilverLining::TextureHandle);

// Create a 2D texture map that's only required to store a single byte of luminance information per texel.
// The luminance textures only need to be implemented if you want to support the automatic generation of
// shadow maps from the clouds.
SILVERLININGDLL_API bool    CreateLuminanceTexture(int w, int h, SilverLining::TextureHandle *texture, const char* name);
typedef bool (*CREATE_LUMINANCE_TEXTURE_PROC)(int, int, SilverLining::TextureHandle*, const char*);

// Copy 8-bit luminance data into the given luminance texture.
SILVERLININGDLL_API bool    CopyLuminanceIntoTexture(SilverLining::TextureHandle texture, int w, int h, unsigned char *buf);
typedef bool (*COPY_LUMINANCE_INTO_TEXTURE_PROC)(SilverLining::TextureHandle, int, int, unsigned char *);

// Copy an area of the screen into the given buffer as 8-bit luminance data.
SILVERLININGDLL_API bool    CopyLuminanceFromScreen(int x, int y, int w, int h, unsigned char *buf);
typedef bool (*COPY_LUMINANCE_FROM_SCREEN_PROC)(int, int, int, int, unsigned char *);

// Returns if point sprites are supported by this renderer. You can just return false and SilverLining will draw
// billboards on its own using triangle strips.
SILVERLININGDLL_API bool             HasPointSprites();
typedef bool (*HAS_POINT_SPRITES_PROC)(void);

SILVERLININGDLL_API bool             EnablePointSprites(double pointSize, void* _context);
typedef bool (*ENABLE_POINT_SPRITES_PROC)(double, void*);

SILVERLININGDLL_API bool             DisablePointSprites(void* _context);
typedef bool (*DISABLE_POINT_SPRITES_PROC)(void*);

// You can just return false for all the render target functions
SILVERLININGDLL_API bool    InitRenderTarget(int w, int h, SilverLining::RenderTargetHandle *tgtHandle);
typedef bool (*INIT_RENDER_TARGET_PROC)(int, int, SilverLining::RenderTargetHandle *);

SILVERLININGDLL_API bool             InitRenderTexture(int w, int h, SilverLining::RenderTextureHandle *texHandle, const char* name, void* _context);
typedef bool(*INIT_RENDER_TEXTURE_PROC)(int, int, SilverLining::RenderTextureHandle *, const char* name, void*);

SILVERLININGDLL_API bool    MakeRenderTargetCurrent(SilverLining::RenderTargetHandle tgtHandle);
typedef bool (*MAKE_RENDER_TARGET_CURRENT_PROC)(SilverLining::RenderTargetHandle);

SILVERLININGDLL_API bool             MakeRenderTextureCurrent(SilverLining::RenderTextureHandle texHandle, bool clear, void* _context);
typedef bool (*MAKE_RENDER_TEXTURE_CURRENT_PROC)(SilverLining::RenderTextureHandle, bool, void*);

SILVERLININGDLL_API bool    RestoreRenderTarget(SilverLining::RenderTargetHandle tgtHandle);
typedef bool (*RESTORE_RENDER_TARGET_PROC)(SilverLining::RenderTargetHandle);

SILVERLININGDLL_API bool             BindRenderTexture(SilverLining::RenderTextureHandle texHandle, void* camera, void* _context);
typedef bool(*BIND_RENDER_TEXTURE_PROC)(SilverLining::RenderTextureHandle, void*, void*);

SILVERLININGDLL_API bool             GetRenderTextureTextureHandle(SilverLining::RenderTextureHandle renTexHandle, SilverLining::TextureHandle *texHandle, void* _context);
typedef bool (*GET_RENDER_TEXTURE_TEXTURE_HANDLE_PROC)(SilverLining::RenderTextureHandle, SilverLining::TextureHandle*, void*);

SILVERLININGDLL_API bool    ReleaseRenderTarget(SilverLining::RenderTargetHandle tgtHandle);
typedef bool (*RELEASE_RENDER_TARGET_PROC)(SilverLining::RenderTargetHandle);

SILVERLININGDLL_API bool             ReleaseRenderTexture(SilverLining::RenderTextureHandle texHandle, void* _context);
typedef bool (*RELEASE_RENDER_TEXTURE_PROC)(SilverLining::RenderTextureHandle, void*);

SILVERLININGDLL_API bool             InitRenderTextureCube(int w, int h, SilverLining::RenderTextureHandle *texHandle, bool floatingPoint, bool generateMipMaps, const char* name, void* _context);
typedef bool (*INIT_RENDER_TEXTURE_CUBE_PROC)(int, int, SilverLining::RenderTextureHandle *, bool, bool, const char*, void*);

SILVERLININGDLL_API bool             MakeRenderTextureCubeCurrent(SilverLining::RenderTextureHandle texHandle, bool clear, CubeFace face, void* _context);
typedef bool (*MAKE_RENDER_TEXTURE_CUBE_CURRENT_PROC)(SilverLining::RenderTextureHandle, bool, CubeFace, void*);

SILVERLININGDLL_API bool             GetRenderTextureCubeTextureHandle(SilverLining::RenderTextureHandle renTexHandle, SilverLining::TextureHandle *texHandle, void* _context);
typedef bool (*GET_RENDER_TEXTURE_CUBE_TEXTURE_HANDLE_PROC)(SilverLining::RenderTextureHandle, SilverLining::TextureHandle*, void*);

SILVERLININGDLL_API bool             BindRenderTextureCube(SilverLining::RenderTextureHandle texHandle, void* _context);
typedef bool (*BIND_RENDER_TEXTURE_CUBE_PROC)(SilverLining::RenderTextureHandle, void*);

SILVERLININGDLL_API bool             ReleaseRenderTextureCube(SilverLining::RenderTextureHandle texHandle, void* _context);
typedef bool (*RELEASE_RENDER_TEXTURE_CUBE_PROC)(SilverLining::RenderTextureHandle, void*);

// Reads back RGBA data from the current color buffer. You can just return false and leave this unimplemented
SILVERLININGDLL_API bool             GetPixels(int x, int y, int w, int h, void *pixels, bool immediate, void* _context);
typedef bool (*GET_PIXELS_PROC)(int, int, int, int, void *, bool, void*);

// Draws the given RGBA data to the area of the current color buffer.
SILVERLININGDLL_API bool             SetPixels(int x, int y, int w, int h, void *pixels, void* _context);
typedef bool (*SET_PIXELS_PROC)(int, int, int, int, void*, void*);

// Draw a (preferably) anti-aliased line of the given color and width between two points.
SILVERLININGDLL_API bool             DrawAALine(const SilverLining::Color& c, double width, const SilverLining::Vector3& p1, const SilverLining::Vector3& p2, void* _context);
typedef bool(*DRAW_AA_LINE_PROC)(const SilverLining::Color& c, double, const SilverLining::Vector3&, const SilverLining::Vector3&, void*);

// Draw (preferably) anti-aliased lines connecting the given vector points using the given color and width.
SILVERLININGDLL_API bool             DrawAALines(const SilverLining::Color& c, double width, const SL_VECTOR(SilverLining::Vector3)& points, void* _context);
typedef bool (*DRAW_AA_LINES_PROC)(const SilverLining::Color& c, double width, const SL_VECTOR(SilverLining::Vector3)&,void*);

// Begin an occlusion query - everything drawn between StartOcclusionQuery and EndOcclusionQuery will
// count up the number of fragments that pass the depth and stencil tests. If you choose not to implement
// the occlusion query interface, it just means that the optional lens flare effect won't be disabled
// when the sun is occluded, and sun/moon occlusion data won't be passed back to the application.
SILVERLININGDLL_API bool             StartOcclusionQuery(SilverLining::QueryHandle *queryHandle);
typedef bool (*START_OCCLUSION_QUERY_PROC)(SilverLining::QueryHandle *queryHandle);

// End the occlusion query.
SILVERLININGDLL_API bool             EndOcclusionQuery(SilverLining::QueryHandle queryHandle);
typedef bool (*END_OCCLUSION_QUERY_PROC)(SilverLining::QueryHandle);

// Get the results of the occlusion query (must be called after StartOcclusionQuery and EndOcclusionQuery).
// The number of fragments that passed the depth and stencil tests during the query is returned.
SILVERLININGDLL_API unsigned int     GetOcclusionQueryResult(SilverLining::QueryHandle queryHandle);
typedef unsigned int (*GET_OCCLUSION_QUERY_RESULT_PROC)(SilverLining::QueryHandle);

// OpenGL only: sets a list of user-provided shader objects to be linked into subsequently created shader programs.
SILVERLININGDLL_API void             SetUserShaders(const SL_VECTOR(unsigned int)& shaderObjects, void* _context);
typedef void (*SET_USER_SHADERS_PROC)(const SL_VECTOR(unsigned int)&, void*);

// OpenGL only: retrieves the OpenGL program object for a given shader handle.
SILVERLININGDLL_API unsigned int     GetShaderProgramObject(SilverLining::ShaderHandle shader);
typedef unsigned int (*GET_SHADER_PROGRAM_OBJECT_PROC)(SilverLining::ShaderHandle);

// OpenGL only: retrieves the OpenGL program object for a line shader.
SILVERLININGDLL_API unsigned int     GetLineShader(void* _context);
typedef unsigned int(*GET_LINE_SHADER_PROC)(void*);

SILVERLININGDLL_API void     SetForceImmediate(bool val, void* _context);
typedef void(*SET_FORCE_IMMEDIATE_PROC)(bool, void*);

SILVERLININGDLL_API bool     GetForceImmediate(void* _context);
typedef bool(*GET_FORCE_IMMEDIATE_PROC)(void*);

SILVERLININGDLL_API void     ExecuteStream(void* _context);
typedef void(*EXECUTE_STREAM_PROC)(void*);

SILVERLININGDLL_API void     SetStream(void* _stream, int frameIndex, void* _context);
typedef void(*SET_STREAM_PROC)(void*, int, void*);

SILVERLININGDLL_API void     SettingsChanged(void* _environment, void* _context);
typedef void(*SETTINGS_CHANGED_PROC)(void*, void*);

#endif
