// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Renderer.h
    \brief Abstracts low-level rendering calls, that are directed to OpenGL or DirectX methods.
 */
#ifndef RENDERER_H
#define RENDERER_H

#include "Color.h"
#include "Frustum.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Buffer.h"
#include "Matrix3.h"
#include "Matrix4.h"
#include "SilverLiningTypes.h"
#include "ThreadCameraStreamData.h"
#include "MutexContainer.h"
#include "BufferProperties.h"
#include <vector>

namespace SilverLining
{
class Renderable;
class OverriddenBillboardMatrix;
class OverriddenCameraPos;
class Camera;
class RenderableListener;
class Atmosphere;
class ThreadCameraStreamData;

#define SILVERLINING_TRACK_CONTEXT_OPERATIONS_DURING_DRAW 0

/** A singleton that abstracts low-level rendering calls from OpenGL or Direct3D. */
class Renderer : public MemObject
{
public:
    /** Default constructor. */
    Renderer(const ThreadCameraStreamData& _tcsData);

    /** Virtual destructor. */
    virtual ~Renderer();

/** The first method call to Renderer must be Initialize(). It gives the graphics
   subsystem a chance to do whatever it needs to set itself up.

   \param rendererType Pass in either Atmosphere::OPENGL, OPENGL32CORE, DIRECTX9, 
   DIRECTX11, or DIRECTX11_1) to
   specify which renderer DLL should be loaded. Obviously it should be consistent with
   whatever graphics system you're using to render the other objects in your scenes.
   \param rightHanded Pass true if you're using a right-handed coordinate system (as
   with OpenGL) with the Z axis pointing "in", or false for a left-handed system
   (sometimes used with DirectX) where the Z axis points "out". In both cases, we
   assume that X points "right" and Y points "up."
   \param environment Unused for OpenGL. For DirectX, pass in a pointer to your
   IDirect3DDevice9 or ID3D10Device interface.
   \param context Returns a pointer that must be passed into FrameStarted each frame, in
   order to set the proper rendering context or device.
   \param avoidStalls If true, SilverLining may skip capturing and restoring the currently bound
   shaders and render targets to avoid a resulting pipeline stall.
   \param userShaders A list of user shaders to link into every shader loaded by SilverLining
   (only used in OpenGL renderers.)
   \return An error code from the Atmosphere::InitializeErrors enumeration, or
   Atmosphere::E_NOERROR.
 */
    int Initialize(int rendererType, bool rightHanded, void *environment, void **context, bool avoidStalls, bool deferred, const SL_VECTOR(unsigned int)& userShaders);

/** Call when a Direct3D9 device is lost. */
    void DeviceLost(void *context);

/** Call when a Direct3D9 device is reset. */
    void DeviceReset(void *context);

/** Call this at the beginning of the frame, prior to setting the camera and projection matrices and
     prior to FrameStarted. */
    void SetContext(void *context);

/** Call this at the beginning of the frame, to enable the Renderer to capture
   the modelview and projection matrices as they were set up in the client
   application. The view frustum and other useful objects will be computed based
   on this. */
    void FrameStarted(void *context);

/** Retrieves the renderer type (Atmosphere::OPENGL, OPENGL32CORE, DIRECTX9, DIRECTX11, DIRECTX11_1) that
   this renderer was initialized with in Initialize(). */
    int GetType() const {
        return type;
    }

    /** Retrieves whether the renderer type is some version of DirectX. */
    bool GetIsDirectX() const;

    /** Whether OpenGL32 or OpenGL */
    bool GetIsOpenGL() const;

    /** Retrieves whether the renderer type is some version of Vulkan. */
    bool GetIsVulkan() const;

/** Retrieves whether this Renderer assumes a right handed coordinate system, as
   it was specified in Initialize(). */
    bool GetIsRightHanded() const {
        return rightHanded;
    }

/** Clears the color buffer to the specified color. */
    void ClearScreen(const Color& color);

/** Clears the depth buffer. */
    void ClearDepth();

/** Retrieves the viewport for rendering.
   \param x The x coordinate of the viewport's lower-left origin.
   \param y The y corodinate of the viewport's lower-left origin.
   \param w The viewport width, in pixels.
   \param h The viewport height, in pixels.
   \sa SetViewport() SetDepthRange() GetDepthRange()
 */
    bool GetViewport(int& x, int& y, int& w, int& h) const;

/** Sets the viewport for rendering.
   \param x The x coordinate of the viewport's lower-left origin.
   \param y The y corodinate of the viewport's lower-left origin.
   \param w The viewport width, in pixels.
   \param h The viewport height, in pixels.
   \sa GetViewport() SetDepthRange() GetDepthRange()
 */
    bool SetViewport(int x, int y, int w, int h);

/** Retrieves the depth range for rendering.
   \param zmin The depth buffer range min value, float [0.0 .. 1.0].
   \param zmax The depth buffer range max value, float [0.0 .. 1.0].
   \sa GetViewport() SetViewport() SetDepthRange() 
 */
    bool GetDepthRange(float& zmin, float& zmax) const;

/** Sets the depth range for rendering.
   \param zmin The depth buffer range min value, float [0.0 .. 1.0].
   \param zmax The depth buffer range max value, float [0.0 .. 1.0].
   \sa SetViewport() GetViewport() GetDepthRange()
 */
    bool SetDepthRange(float zmin, float zmax);

/** Sets reverse z rendering.
   \param reverse indicates that reverse z rendering should be used.
 */
    bool SetReverseZ(bool reverse);

/** Retrieves the vertical field of view of the view frustum. */
    bool GetFOV(double& fov) const;

/** Force a recompute of the current FOV */
    bool RecomputeFOV();

/** Reads a rectangle of pixels from the back buffer.
   \param x The x coordinate of the lower-left origin of the area to read back.
   \param y The y coordinate of the lower-left origin of the area to read back.
   \param w The width of the area to read back, in pixels.
   \param h The height of the area to read back, in pixels.
   \param pixels A pointer to a buffer to retrieve the area specified. Must be
   allocated to at least w * h * the size of an individual pixel, which depends
   on how you created your framebuffer.
   \param immediate Whether this method should block and return the pixels currently
   at this position, or if you're OK with receiving this result on the next query
   instead. Only works on 1x1 queries under OpenGL.
   \return True if the operation completed successfully. False if the back-buffer
   wasn't lockable (in DirectX, this must be configured at initialization time.)
 */
    bool GetPixels(int x, int y, int w, int h, void *pixels, bool immediate);

/** Writes a rectangle of pixels to the back buffer.
   \param x The x coordinate of the lower-left origin of the area to write to.
   \param y The y coordinate of the lower-left origin of the area to write to.
   \param w The width of the area to write to, in pixels.
   \param h The height of the area to write to, in pixels.
   \param pixels A pointer to a buffer to containing the RGBA pixels to set. Must be
   allocated to at least w * h * the size of an individual pixel, which depends
   on how you created your framebuffer.
   \return True if the operation completed successfully. False if the back-buffer
   wasn't lockable (in DirectX, this must be configured at initialization time.)
 */
    bool SetPixels(int x, int y, int w, int h, void *pixels, void* _context);

/** Adds user-provided OpenGL shader objects to any shader program linked by SilverLining.
    \param userShaders A vector of shader objects created with glCreateShaderObject() to link
    into any subsequently created shader programs. Pass an empty vector to clear the list.
*/
    void SetUserShaders(const SL_VECTOR(unsigned int)& userShaders);

/** OpenGL only: retrieve the underlying OpenGL program object for a given shader. */
    unsigned int GetShaderProgram(ShaderHandle shader);

/** OpenGL only: retrieve the underlying OpenGL program object for the line shader. */
    unsigned int GetLineShader(void);

/** Loads a Cg shader from a named text file.
   \param filename A full pathname to the Cg file.
   \return A handle to the shader if loaded, or NULL if an error occurred.
 */
    ShaderHandle LoadShaderFromFile(const char *filename, const SL_VECTOR(SL_STRING)& defines = SL_VECTOR(SL_STRING)());

/** Loads a Cg shader from a text buffer.
   \param source A pointer to a null-terminated string that contains the source for the
   Cg shader to load.
   \return A handle to the shader if loaded, or NULL if an error occurred.
 */
    ShaderHandle LoadShaderFromSource(const char *source);

/** Deletes a shader explicitly (normally you don't have to do this; shaders will be disposed of
    automatically at shutdown. But maybe you're reloading an existing shader. */
    void DeleteShader(ShaderHandle shader);

	/** OpenGL32, OpenGL only: Get the constant location of a uniform or an offset into a UBO for that uniform. */
	int GetConstantLocation(ShaderHandle shader, const char* uniformName) const;

	/** OpenGL32, OpenGL only: Set a constant  Vector3 at a given location/offset into UBO in the shader */
	void SetConstantVectorAtLocation(ShaderHandle shader, int loc, const Vector3& vector);

	/** OpenGL32, OpenGL only: Set a constant  Vector4 at a given location/offset into UBO in the shader */
	void SetConstantVector4AtLocation(ShaderHandle shader, int loc, const Vector4& vector);

	/** Same as above. Takes a Vector4f instead */
    void SetConstantVector4AtLocation(ShaderHandle shader, int loc, const Vector4f& vector);

	/** OpenGL32, OpenGL only: Set a constant  Matrix4 at a given location/offset into UBO in the shader */
	void SetConstantMatrix4AtLocation(ShaderHandle shader, int loc, const Matrix4& matrix);

	/** OpenGL32, OpenGL only: Set a constant  Matrix4 at a given location/offset into UBO in the shader */
	void SetConstantMatrix4AtLocation(ShaderHandle shader, int loc, const float* matrix);

	/** OpenGL32, OpenGL only: Set a constant  int at a given location/offset into UBO in shader */
	void SetConstantIntAtLocation(ShaderHandle shader, int loc, int val);

/** Assigns a constant 3D vector to a named shader vector.
   \param shader The handle returned from LoadShaderFromFile() or LoadShaderFromSource()
   \param varName The name of the constant vector in the shader program
   \param vector The Vector3 to set the constant vector to.
   \return True if the operation succeeded.
 */
    bool SetConstantVector(ShaderHandle shader, const char *varName, const Vector3& vector);

/** Assigns a constant 4D vector to a named shader vector.
   \param shader The handle returned from LoadShaderFromFile() or LoadShaderFromSource()
   \param varName The name of the constant vector in the shader program
   \param vector The Vector3 to set the constant vector to.
   \return True if the operation succeeded.
 */
    bool SetConstantVector4(ShaderHandle shader, const char *varName, const Vector4& vector);
    
	/** Same as above. Takes a Vector4f instead */
    bool SetConstantVector4(ShaderHandle shader, const char *varName, const Vector4f& vector);

    bool SetConstantInt(ShaderHandle shader, const char *varName, int val);

/** Assigns a constant 4x4 matrix to a named shader matrix.
   \param shader The handle returned from LoadShaderFromFile() or LoadShaderFromSource()
   \param varName The name of the constant matrix in the shader program
   \param matrix the Matrix4 to set the constant matrix to.
   \return True if the operation succeeded.
 */
    bool SetConstantMatrix(ShaderHandle shader, const char *varName, const Matrix4& matrix);
	bool SetConstantMatrix(ShaderHandle shader, const char *varName, const float *matrix);

/** Set up renderer specific things before constants can be set
   \param shader The handle returned from LoadShaderFromFile() or LoadShaderFromSource()
 */    
    bool PreConstantsSet(ShaderHandle shader) const;

/** Set up renderer specific things *after* constants are set
   \param shader The handle returned from LoadShaderFromFile() or LoadShaderFromSource()
 */    
    bool PostConstantsSet(ShaderHandle shader) const;

/** Makes the specified shader active.
   \param shader The handle returned from LoadShaderFromFile() or LoadShaderFromSource()
   \return True if the operation succeeded
 */
    bool BindShader(ShaderHandle shader);

/** Deactivates the specified shader type and restores the usual rendering pipeline.
   \return True if the operation succeeded
 */
    bool UnbindShader();

/** In other renderers, we can update the ubos multiple times and render from them multiple times.
*   We can't do this easily in vulkan:
*   -A flush or mapped data buffer update takes the last updated value at the point the queue is submitted  (so we can't even do what we do for example in OpenGL deferred pipeline)
*   -We can't use copy buffers commands during rendering/recording the command buffers when render pass is active
*   -We don't want to use multiple buffers/bind different descriptor sets (can cause proliferation of buffers/can be slow)
*   So, we use dynamic ubo buffers. This helper call, increments the offset of the dynamic ubo buffer, so that the offset slides by 1 
*   So we can do this: 
*       setup ubo constants
*           render, render, render
*       setup ubo constants
*           render, render
*       ...and so on
*   This function does not affect other renderers
*/
    void IncrementConstantBuffersOffset(ShaderHandle shader);

/** Cleans up the Cg shader system once you are done rendering with shaders altogether.
   \return True if the operation succeeded.
 */
    bool Shutdown(void *context);

/** Allocates a vertex buffer for the specified number of vertices.
   \param nVerts The number of vertices this vertex buffer will contain.
   \return A handle to the vertex buffer created, or NULL if it could not be created.
 */
    VertexBufferHandle AllocateVertexBuffer(int nVerts, const char* name, const SilverLining::BufferProperties& bufferProperties);

/** Locks the vertex buffer so that the vertices may be directly read from or written
   to. LockVertexBuffer() and UnlockVertexBuffer() must enclose any calls to GetVertices().
   \param bufferHandle The handle returned from AllocateVertexBuffer() specifying the buffer
   to lock.
   \return True if the operation succeeded.
 */
    bool LockVertexBuffer(VertexBufferHandle bufferHandle);

/** Retrieves a pointer to the vertices of the vertex buffer, which may be read from
   or written to. Must be preceded by a call to LockVertexBuffer(), and followed by a
   call to UnlockVertexBuffer().
   \param bufferHandle The handle returned from AllocateVertexBuffer() specifying the buffer
   to lock.
   \return A pointer to the vertices of the buffer, or NULL if the vertices could not be
   obtained (buffer not locked?)
 */
    Vertex *GetVertices(VertexBufferHandle bufferHandle);

/** Unlocks the vertex buffer so that vertices may not be directly read from or written to,
   and may be used efficiently for rendering. UnlockVertexBuffer() should be called as soon
   as you are done accessing the Vertex pointer returned by GetVertices(). Once UnlockVertexBuffer()
   has been called, this pointer is no longer valid.
   \sa LockVertexBuffer() GetVertices()
   \param bufferHandle The handle returned from AllocateVertexBuffer() specifying the buffer
   to lock.
   \return True if the operation succeeded.
 */
    bool UnlockVertexBuffer(VertexBufferHandle bufferHandle);

/** Copies the contents of the vertex buffer to a Vertex array that you provide. This may be
   more efficient than calling LockVertexBuffer(), GetVertices(), and UnlockVertexBuffer() in
   series if you need to retrieve a contiguous range of vertices and copy them into system
   memory.
   \param bufferHandle The handle returned from AllocateVertexBuffer() specifying the buffer
   to copy vertices from.
   \param offset The vertex offset into the buffer to start copying from.
   \param verts A pointer to a Vertex array of size nVerts * sizeof(Vertex) that you have already
   allocated.
   \param nVerts The number of vertices to copy into the verts pointer.
   \return True if the operation succeeded.
 */
    bool GetVertexBuffer(VertexBufferHandle bufferHandle, int offset, Vertex *verts, int nVerts);

/** Copies a Vertex array that you provide into a vertex buffer. This may be
   more efficient than calling LockVertexBuffer(), GetVertices(), and UnlockVertexBuffer() in
   series if you need to overwrite a contiguous range of vertices in a vertex buffer.
   \param bufferHandle The handle returned from AllocateVertexBuffer() specifying the buffer
   to copy vertices into.
   \param offset The vertex offset into the buffer to start copying into.
   \param verts A pointer to a Vertex array of size nVerts * sizeof(Vertex) that you have already
   allocated.
   \param nVerts The number of vertices to copy into the vertex buffer.
   \return True if the operation succeeded.
 */
    bool UpdateVertexBuffer(VertexBufferHandle bufferHandle, int offset, Vertex *verts, int nVerts, bool justColors);

/** Disposes of a vertex buffer previously allocated via AllocateVertexBuffer(). Do not attempt
   to use this buffer after calling ReleaseVertexBuffer().
   \param bufferHandle The handle returned from AllocateVertexBuffer() specifying the vertex buffer
   to deallocate and destroy.
   \return True if the operation succeeded.
 */
    bool ReleaseVertexBuffer(VertexBufferHandle bufferHandle);

/** Sets the vertex buffer to be currently active and in use for future rendering calls.
   \sa UnsetVertexBuffer()
   \param bufferHandle The handle returned from AllocateVertexBuffer() specifying the vertex buffer
   to make active for rendering.
   \param vertexColors Whether to use the per-vertex color information when rendering.
   \return True if the operation succeeded.
 */
    bool SetVertexBuffer(VertexBufferHandle bufferHandle, bool vertexColors);

/** Restores the state to not use the vertex buffer from the previous call to SetVertexBuffer() for
   rendering.
   \sa SetVertexBuffer()
   \return True if the operation succeeded.
 */
    bool UnsetVertexBuffer();

/** Only for vulkan.
    Similar to mapped ubos/ssbos that can be changed/bound at a given offset.
*/
    bool IncrementOffsetIndex(VertexBufferHandle vbh);

/** Allocates an index buffer for the specified number of indices.
   \param nVerts The number of indices this vertex buffer will contain.
   \return A handle to the index buffer created, or NULL if it could not be created.
 */
    IndexBufferHandle AllocateIndexBuffer(int nVerts, const char* name, const SilverLining::BufferProperties& bufferProperties);

/** Locks the index buffer so that the indices may be directly read from or written
   to. LockIndexBuffer() and UnlockIndexBuffer() must enclose any calls to GetIndices().
   \param ib The handle returned from AllocateIndexBuffer() specifying the buffer
   to lock.
   \return True if the operation succeeded.
 */
    bool LockIndexBuffer(IndexBufferHandle ib);

/** Retrieves a pointer to the indices of the index buffer, which may be read from
   or written to. Must be preceded by a call to LockIndexBuffer(), and followed by a
   call to UnlockIndexBuffer().
   \param ib The handle returned from AllocateIndexbuffer() specifying the buffer
   to lock.
   \return A pointer to the indices of the buffer, or NULL if the indices could not be
   obtained (buffer not locked?)
 */
    Index *GetIndices(IndexBufferHandle ib);

/** Unlocks the index buffer so that indices may not be directly read from or written to,
   and may be used efficiently for rendering. UnlockIndexBuffer() should be called as soon
   as you are done accessing the Index pointer returned by GetIndices(). Once UnlockIndexBuffer()
   has been called, this pointer is no longer valid.
   \sa LockIndexBuffer() GetIndices()
   \param ib The handle returned from AllocateIndexBuffer() specifying the buffer
   to lock.
   \return True if the operation succeeded.
 */
    bool UnlockIndexBuffer(IndexBufferHandle ib);

/** Disposes of an index buffer previously allocated via AllocateIndexBuffer(). Do not attempt
   to use this buffer after calling ReleaseIndexBuffer().
   \param ib The handle returned from AllocateIndexBuffer() specifying the index buffer
   to deallocate and destroy.
   \return True if the operation succeeded.
 */
    bool ReleaseIndexBuffer(IndexBufferHandle ib);

/** Sets the index buffer to be currently active and in use for future rendering calls.
   \sa UnsetIndexBuffer()
   \param ib The handle returned from AllocateIndexBuffer() specifying the index buffer
   to make active for rendering.
   \return True if the operation succeeded.
 */
    bool SetIndexBuffer(IndexBufferHandle ib);

/** Restores the state to not use the index buffer from the previous call to SetIndexBuffer() for
   rendering.
   \sa SetIndexBuffer()
   \return True if the operation succeeded.
 */
    bool UnsetIndexBuffer();

/** Draws a triangle strip using the specified vertex buffer and index buffer.
   \param vb The vertex buffer to containing the vertices of the triangle strip.
   \param ib The index buffer containing indices into the vertex buffer specified that specifies
   the order to draw the triangle strip vertices in.
   \param startIdx The offset into the index buffer to start drawing from, or 0 for no offset.
   \param nIndices The number of indices in the index buffer to draw as a triangle strip.
   \param nVerts The number of vertices in the vertex buffer specified.
   \param vertColors Specifies whether or not to use per-vertex colors in the vertex buffer.
   \param preDraw Specifies whether or not pre draw happens before rendering on the current shader.
   \return True if the operation succeeded.
 */
    bool DrawStrip(VertexBufferHandle vb, IndexBufferHandle ib, int startIdx, int nIndices,
                   int nVerts, bool vertColors, bool preDraw);

	bool DrawMultiStrips(CommandBufferHandle commandBuffer);

/** Draws a line strip using the selected vertex buffer and color.
    \param vb The vertex buffer to use when drawing this line.
    \param color The color to use for this line.
    \param nVerts The number of vertices to draw.
    \param width The width of the line in pixels.
    \return True if the operation succeeded.
*/
    bool DrawLineStrip(VertexBufferHandle vb, const Color& color, int nVerts, float width);

/** Draws a triangle strip using the vertex and index buffer previously set by SetVertexBuffer() and
    SetIndexBuffer(). The caller is responsible for ensuring these are both set to valid buffers prior
    to this call, and for unsetting these buffers afterwards.

   \param ib The active index buffer containing indices into the active vertex buffer specified that specifies
   the order to draw the triangle strip vertices in.
   \param startIdx The offset into the index buffer to start drawing from, or 0 for no offset.
   \param nIndices The number of indices in the index buffer to draw as a triangle strip.
   \param nVerts The number of vertices in the vertex buffer specified.
   \return True if the operation succeeded.
 */
    bool DrawStripDirect(IndexBufferHandle ib, int startIdx, int nIndices, int nVerts);

    bool DrawStripDirectInstanced(IndexBufferHandle ib, int startIdx, int nIndices, int nVerts, int instanceCount, int firstInstance);

/** Draws the vertices in a vertex buffer as a series of individual points.
   \param vb The vertex buffer containing the point locations to render.
   \param pointSize The size of each point to render, in pixels.
   \param nPoints The number of points to render from the vertex buffer specified.
   \param start The offset into the vertex buffer to begin drawing from, or 0 for no offset.
   \param vertColors Specifies whether or not to use per-vertex colors from the vertex buffer.
   \return True if the operation succeeded.
 */
    bool DrawPoints(VertexBufferHandle vb, double pointSize, int nPoints, int start = 0,
                    bool vertColors = true);

/** Returns whether this Renderer supports drawing lists of Quads as primitives.
   \sa DrawQuads()
 */
    bool HasQuads();

/** Draws a series of 4-point quad primitives from a vertex buffer. Only works if HasQuads() returns
   true; not all renderers support quad primitives.
   \param vb The vertex buffer containing the series of vertices that define a list of quad primitives.
   \param nPoints The number of vertices to draw in the vertex buffer specified.
   \param start The offset into the vertex buffer to start drawing from, or 0 for no offset.
   \param vertColors Specifies whether to use per-vertex colors in the vertex buffer.
   \return True if the opeation succeeded.
 */
    bool DrawQuads(VertexBufferHandle vb, int nPoints, int start = 0, bool vertColors = true);

/** Draws an anti-aliased line between two 3D points. (Useful for lightning effects, for example)
   \param c The color to draw the line as.
   \param width The width of the line, in pixels.
   \param p1 The point to start drawing the line from.
   \param p2 The point to draw the line to.
   \return True if the operation succeeded.
 */
    bool DrawAALine(const Color& c, double width, const Vector3& p1, const Vector3& p2);

/** Draws a series of connected anti-aliased line segments that connect the vector of Vector3 's
   specified. Useful for, say, a lightning branch.
   \param c The color the draw the line segments as.
   \param width The width of the line segments, in pixels.
   \param points The series of 3D points to connect-the-dots with.
   \return True if the operation succeeded.
 */
    bool DrawAALines(const Color& c, double width, const SL_VECTOR(Vector3)& points);

/** Enables or disables writing into the depth buffer when objects are rendered. */
    bool EnableDepthWrites(bool);

/** Enables or disables reading from the depth buffer and performing z-buffer tests when items
   are rendered. */
    bool EnableDepthReads(bool);

/** Enables or disables the application of a 2D texture map when rendering polygons.
   \sa EnableTexture() DisableTexture()
 */
    bool EnableTexture2D(bool);

/** Enables or disables the application of a 3D texture map when rendering polygons.
   \sa EnableTexture() DisableTexture()
 */
    bool EnableTexture3D(bool);

/** Enables or disables backface culling tests when rendering polygons. */
    bool EnableBackfaceCulling(bool);

/** Sets the winding order of culled polygons when backface culling is enabled. */
    bool BackfaceCullClockwise(bool);

/** Enables or disables the application of fog when rendering polygons.
   \sa ConfigureFog() GetFog()
 */
    bool EnableFog(bool);

/** Sets the properties of exponential fog to render with.
   \param density The density to apply to the exponential fog equation.
   \param start The z distance at which fog effects begin for linear fog (currently not used)
   \param end The z distance at which fog effects end for linear fog (currently not used)
   \param c The fog color
   \return True if the operation succeeded.
   \sa EnableFog() GetFog()
 */
    bool ConfigureFog(double density, double start, double end, const Color& c);

/** Retrieves the fog properties previously set by ConfigureFog()
   \param density Receives the exponential fog density parameter.
   \param start Receives the linear fog start depth (currently unused)
   \param end Receives the linear fog end depth (currently unused)
   \param c Receives the fog color.
 */
    void GetFog(double& density, double& start, double& end, Color& c) const;

/** Enables or disables per-vertex lighting based on material properties and light sources. */
    bool EnableLighting(bool);

/** Enables the application of alpha-blending when rendering polygons, using the specified
   source and destination blend factors from the BlendFactors enumeration.
   \sa DisableBlending()
 */
    bool EnableBlending(int srcBlendFactor, int dstBlendFactor, bool force = false);

/** Disables the application of alpha-blending when rendering polygons.
   \sa EnableBlending()
 */
    bool DisableBlending();

/** Pushes all current rendering state onto a stack, which may be later popped back by a call to
   PopAllState(). */
    bool PushAllState();

/** Restores the rendering state as it was when the last call to PushAllState() was made, and pops
   this state from the stack. */
    bool PopAllState();

/** Sets all rendering states to the default settings for the graphics subsystem. Useful for clearing out
   any odd states that the application may have set prior to caling us. */
    bool SetDefaultState();

/** Heartbeat function, call per frame, etc */
    bool HeartBeat(void) const;

/** Notify the renderer that a context is going to be deleted*/
    bool ContextBeingDeleted(void* context) const;

/** Determines if this renderer has support for point-sprites; that is, rendering textured points
   to quickly render billboards. */
    bool HasPointSprites();

/** Enables point sprites; subsequent point-drawing calls will draw a textured quad with the current
   texture at the specified pointSize in pixels. Note that many implementations may only draw a limited
   range of pointSizes.
   \sa HasPointSprites() DisablePointSprites() EnableTexture() DrawPoints()
 */
    bool EnablePointSprites(double pointSize);

/** Disables the rendering of points as point sprites; disables a prior call to EnablePointSprites(). */
    bool DisablePointSprites();

/** Polygons rendered after this call, that are not using per-vertex colors from a vertex buffer, will
   be drawn with the specified Color. */
    bool SetCurrentColor(const Color& c);

/** Retrieves the current texture matrix in use for texture unit 0. */
    bool GetTextureMatrix(Matrix4 *m);

/** Overwrites the current texture matrix with the one specified for texture unit 0. */
    bool SetTextureMatrix(const Matrix4& m);

/** Overwrites the current projection matrix with the one specified. */
    bool SetProjectionMatrix(const Matrix4& m);

/** Computes a projection matrix for the given near and far clipping plane distances, and the
   specified horizontal and vertical fields of view. This matrix is then set as the current projection
   matrix for rendering. The fields of view are provided in radians.*/
    bool SetProjectionMatrix(double nearClip, double farClip, double fovX, double fovY);

/** Computes a projection matrix for arbitrary left, right, top, bottom, znear, and zfar.
    Left, right, top, bottom are extents of the near clipping plane bounded by the
    desired view frustum, just like glFrustum. */
    bool SetProjectionMatrix(double l, double r, double t, double b, double zn, double zf);

/** Computes an orthographic projection matrix for the given dimensions, and sets this as the current
   projection matrix for rendering. */
    bool SetOrthoMatrix(double left, double right, double bottom, double top, double pnear, double pfar);

/** Computes a 2D orthographic projection matrix for the given width and height, and sets this
   as the current proejction matrix for rendering. */
    bool Set2DOrthoMatrix(double w, double h);

/** Computes a special projection matrix where the far clip plane is at infinity, and sets
   this as the current projection matrix for rendering. */
    bool SetInfiniteProjectionMatrix(double nearClip, double fovy, double aspect);

/** Retrieves the modelview matrix currently used for rendering. */
    bool GetModelviewMatrix(Matrix4 *m) const;

/** Sets the modelview matrix used for rendering to the one specified. */
    bool SetModelviewMatrix(const Matrix4& m);

/** Projects a 3D point to screen coordinates, using the current modelview and projection transforms. */
    bool ProjectToScreen(const Vector3& pt, Vector3 *scr);

/** Creates a 2D texture from a TGA or DDS file. This image should be RGB or RGBA and have power-of-2
   dimensions.
   \param imgPath The full pathname of the image file.
   \param texture The TextureHandle to receive the texture object.
   \param repeat Whether this texture should repeat or clamp when wrapping across 1.0 u,v coordinates.
   \return True if the operation succeeded.
 */
    bool LoadTextureFromFile(const char *imgPath, TextureHandle *texture, bool repeatU, bool repeatV, const char* name);

/** Creates a color texture object using an array of floating-point data.
   \param data An array of floats that will define this texture object, should be RGB data of size
   width * height * 3 * sizeof(float).
   \param width The width of the texture, should be a power of 2.
   \param height The height of the texture, should be a power of 2.
   \param texture The TextureHandle to receive the created texture object.
   \return True if the operation succeeded.
   \sa HasFloatTextures()
 */
    bool LoadFloatTextureRGB(const float *data, int width, int height, TextureHandle *texture, const char* name);

/** Creates an intensity / alpha texture using an array of floating point data.
   \param data An array of floats that will define this texture object, should be IA data of size
   width * height * 2 * sizeof(float)
   \param width The width of the texture, should be a power of 2.
   \param height The height of the texture, should be a power of 2.
   \param texture The TextureHandle to receive the created texture object.
   \return True if the operation succeeded.
   \sa HasFloatTextures()
 */
    bool LoadFloatTexture(const float *data, int width, int height, TextureHandle *texture, const char* name);

/** Creates an LA texture from a buffer of LA data.
   \param data A pointer to an array of LA data of size width * height * 2 * sizeof(uchar)
   \param width The width of the texture data, should be a power of 2.
   \param height The height of the texture data, should be a power of 2.
   \param texture The TextureHandle to receive the texture object created.
   \param repeat Whether this texture should repeat or clamp when wrapping across 1.0 u,v coordinates.
   \return True if the operation succeeded.
 */
    bool LoadTexture(const unsigned char *data, int width, int height, TextureHandle *texture, bool repeatU, bool repeatV, const char* name);

/** Creates an 3D Luminance texture from a buffer of RGB data.
   \param data A pointer to an array of 24-bit RGB data of size width * height * depth * 3 * sizeof(uchar)
   \param width The width of the texture data.
   \param height The height of the texture data.
   \param texture The TextureHandle to receive the texture object created.
   \param repeat Whether this texture should repeat or clamp when wrapping across 1.0 u,v,t coordinates.
   \return True if the operation succeeded.
 */
    bool Load3DTextureRGB(const unsigned char *data, int width, int height, int depth, TextureHandle *texture, bool repeatU, bool repeatV, bool repeatR, const char* name);

/** Creates an 3D Luminance texture from a buffer of Luminance data.
   \param data A pointer to an array of 8-bit luminance data of size width * height * depth  * sizeof(uchar)
   \param width The width of the texture data.
   \param height The height of the texture data.
   \param texture The TextureHandle to receive the texture object created.
   \param repeat Whether this texture should repeat or clamp when wrapping across 1.0 u,v,t coordinates.
   \return True if the operation succeeded.
 */
    bool Load3DTexture(const unsigned char *data, int width, int height, int depth, TextureHandle *texture, bool repeatU, bool repeatV, bool repeatR, const char* name);

/** Creates an 3D Luminance Alpha texture from a buffer of Luminance Alpha data.
   \param data A pointer to an array of 16-bit luminance alpha data of size width * height * depth  * 2 * sizeof(uchar)
   \param width The width of the texture data.
   \param height The height of the texture data.
   \param depth The height of the texture data.
   \param texture The TextureHandle to receive the texture object created.
   \param repeat Whether this texture should repeat or clamp when wrapping across 1.0 u,v,t coordinates.
   \return True if the operation succeeded.
 */
    bool Load3DTextureLA(const unsigned char *data, int width, int height, int depth, TextureHandle *texture, bool repeatU, bool repeatV, bool repeatR, const char* name);

/** Updates an 3D Luminance Alpha texture block from a buffer of Luminance Alpha data.
   \param data A pointer to an array of 16-bit luminance alpha data of size width * height * depth  * 2 * sizeof(uchar)
   \param width The width of the texture block to update.
   \param height The height of the texture block to update.
   \param depth The height of the texture block to update.   
   \param xoffset The X origin of the texture block to update.
   \param yoffset The Y origin of the texture block to update.
   \param zoffset The Z origin of the texture block to update.
   \param dataRowPitch The size in bytes of one row of pixels in source data volume. Can be larger than width * 2 * sizeof(uchar).
   \param dataSlicePitch The size in bytes of one slice (image) of pixels in source data volume. Can be larger than width * height * 2 * sizeof(uchar).
   \param texture The TextureHandle to for texture object to update (created earlier with Load3DTextureLA )
   \return True if the operation succeeded.
*/
    bool SubLoad3DTextureLA(const unsigned char * data, int width, int height, int depth, int xoffset, int yoffset, int zoffset,
                             int dataRowPitch, int dataSlicePitch, TextureHandle texHandle );

	/** Creates an 3D half-precision signed floating point texture from a buffer of floating point data.
	\param data A pointer to an array of floating point data of size width * height * depth  * sizeof(float)
	\param width The width of the texture data.
	\param height The height of the texture data.
	\param texture The TextureHandle to receive the texture object created.
	\param repeat Whether this texture should repeat or clamp when wrapping across 1.0 u,v,t coordinates.
	\return True if the operation succeeded.
	*/
	bool Load3DTextureFloat(const float *data, int width, int height, int depth, TextureHandle *texture, bool repeatU, bool repeatV, bool repeatR, const char* name);

/// Retrieves a pointer to the platform-specific texture handle containing the texture. On OpenGL this is a GLuint; on DirectX9 
/// it is a IDirect3DTexture9 *; on DirectX11 it is a ID3D11ShaderResourceView *
    void* GetNativeTexture(TextureHandle texHandle) const;

/** Determines if this Renderer supports floating-point texture maps. Should be checked prior to
   attempting to use LoadFloatTextureRGB() or LoadFloatTexture().
 */
    bool HasFloatTextures();

/** Makes the specified 2D texture active for the specified multitexture stage (0 for the base texture.)
   \sa DisableTexture()
 */
    bool EnableTexture(TextureHandle texture, int stage);

/** Makes the specified 3D texture active for the specified multitexture stage (0 for the base texture.)
   \sa DisableTexture()
 */
    bool Enable3DTexture(TextureHandle texture, int stage);

/** Disables texture mapping for the specified multitexture stage (0 for the base texture.)
   \sa EnableTexture()
 */
    bool DisableTexture(int stage);

/** Disposes of a texture previously created, it may no longer be referenced after this call.
   \sa LoadTexture() LoadFloatTexture() LoadFloatTextureRGB() LoadTextureFromFile()
 */
    bool ReleaseTexture(TextureHandle texture);

/** Initializes a texture map that may be used as a rendering target.

   Use of render-to-texture involves the following steps:

   1. Call InitRenderTexture() at initialization time to pre-allocate your texture map
   2. Each frame that you wish to render to a texture, call MakeRenderTextureCurrent() prior
   to rendering.
   3. When you're done rendering to the texture, call BindRenderTexture().
   4. To obtain the 2D texture map to use for rendering other objects, call GetRenderTextureTextureHandle().
   5. At application shutdown, destroy the render-to-texture objects with ReleaseRenderTexture().

   \param w The width of the texture to use as a rendering target, in texels.
   \param h The height of the texture to use as a rendering target, in texels.
   \param texture The RenderTextureHandle to receive the texture created.
   \return True if the operation succeeded. Many Renderers will not support render-to-texture, so
   be sure to check the result.

   \sa MakeRenderTextureCurrent() BindRenderTexture() GetRenderTextureTextureHandle() ReleaseRenderTexture()
 */
    bool InitRenderTexture(int w, int h, RenderTextureHandle *texture, const char* name);

/** Sets the rendering target to the texture specified, instead of the back buffer.
   \param texture A RenderTextureHandle previously successfully allocated with InitRenderTexture()
   \param clear Whether the render texture should be cleared to black
   \return True if the operation succeeded.
 */
    bool MakeRenderTextureCurrent(RenderTextureHandle texture, bool clear);

/** Stops rendering to the specified RenderTextureHandle, and uploads the resulting texture
   map to the video card. Restores the render target to the back buffer.

   \sa MakeRenderTextureCurrent()
 */
    bool BindRenderTexture(RenderTextureHandle texture, void* camera);

/** Retrieves the underlying TextureHandle for the RenderTextureHandle, so it may be used
   as any other texture map for rendering. */
    bool GetRenderTextureTextureHandle(RenderTextureHandle renTexture, TextureHandle *texture);

/** Releases all resources used by the RenderTextureHandle previously allocated via
   InitRenderTexture(), rendering this RenderTextureHandle invalid. */
    bool ReleaseRenderTexture(RenderTextureHandle texture);

/** Initializes a texture cube map that may be used as a rendering target.

   Use of render-to-texture involves the following steps:

   1. Call InitRenderTextureCube() at initialization time to pre-allocate your texture map
   2. Each frame that you wish to render to a face, call MakeRenderTextureCubeCurrent() prior
   to rendering.
   3. When you're done rendering to the texture, call BindRenderTextureCube().
   4. To obtain the 2D texture map to use for rendering other objects, call GetRenderTextureCubeTextureHandle().
   5. At application shutdown, destroy the render-to-texture objects with ReleaseRenderTextureCube().

   \param w The width of the texture to use as a rendering target, in texels.
   \param h The height of the texture to use as a rendering target, in texels.
   \param texture The RenderTextureHandle to receive the texture created.
   \param floatingPoint Whether the cube map texture is 16-bit floating point or 8-bit unsigned byte.
   \return True if the operation succeeded. Many Renderers will not support render-to-texture, so
   be sure to check the result.

   \sa MakeRenderTextureCubeCurrent() BindRenderTextureCube() GetRenderTextureCubeTextureHandle() ReleaseRenderTextureCube()
 */
    bool InitRenderTextureCube(int w, int h, RenderTextureHandle *texture, bool floatingPoint, bool generateMipMaps, const char* name);

/** Sets the rendering target to the texture and face specified, instead of the back buffer.
   \param texture A RenderTextureHandle previously successfully allocated with InitRenderTextureCube()
   \param clear Whether the render texture should be cleared to black
   \return True if the operation succeeded.
 */
    bool MakeRenderTextureCubeCurrent(RenderTextureHandle texture, bool clear, CubeFace face);

/** Stops rendering to the specified RenderTextureHandle, and uploads the resulting texture
   map to the video card. Restores the render target to the back buffer.

   \sa MakeRenderTextureCubeCurrent()
 */
    bool BindRenderTextureCube(RenderTextureHandle texture);

    bool GetRenderTextureCubeTextureHandle(RenderTextureHandle renTexture, TextureHandle *texture) const;

/** Releases all resources used by the RenderTextureHandle previously allocated via
   InitRenderTexture(), rendering this RenderTextureHandle invalid. */
    bool ReleaseRenderTextureCube(RenderTextureHandle texture);

/** Initializes a surface that may be used as a rendering target.

   Use of render-to-surface involves the following steps:

   1. Call InitRenderTarget() at initialization time to pre-allocate your surface
   2. Each frame that you wish to render to an offscreen surface, call MakeRenderTargetCurrent() prior
   to rendering.
   3. When you're done rendering to the texture, call RestoreRenderTarget().
   4. At application shutdown, destroy the render target objects with ReleaseRenderTarget().

   \param w The width of the surface to use as a rendering target, in pixels.
   \param h The height of the surface to use as a rendering target, in pixels.
   \param target The RenderTargetHandle to receive the surface created.
   \return True if the operation succeeded. Many Renderers will not support render targets, so
   be sure to check the result.

   \sa MakeRenderTargetCurrent() BindRenderTarget() ReleaseRenderTarget()
 */
    bool InitRenderTarget(int w, int h, RenderTargetHandle *target);

/** Sets the rendering target to the surface specified, instead of the back buffer.
   \param texture A RenderTargetHandle previously successfully allocated with InitRenderTarget()
   \return True if the operation succeeded.
 */
    bool MakeRenderTargetCurrent(RenderTargetHandle target);

/** Stops rendering to the specified RenderTargetHandle.
   Restores the render target to the back buffer.

   \sa MakeRenderTargetCurrent()
 */
    bool RestoreRenderTarget(RenderTargetHandle previousTarget);

/** Releases all resources used by the RenderTargetHandle previously allocated via
   InitRenderTarget(), rendering this RenderTargetHandle invalid. */
    bool ReleaseRenderTarget(RenderTargetHandle target);

/** Creates a 2D luminance texture, suitable for use as a shadow map.
   \param w The width of the texture to create, in texels. Should be a power of 2.
   \param h The height of the texture to create, in texels. Should be a power of 2.
   \param texture The TextureHandle to receive the texture created.
   \return True if the operation completed successfully.
 */
    bool CreateLuminanceTexture(int w, int h, TextureHandle *texture, const char* name);

/** Copies RGBA pixels from the back-buffer into a specified buffer of system memory.
   \param x The minimum X value of the rectangle to copy from the backbuffer, in pixels.
   \param y The minimum Y value of the rectangle to copy from the backbuffer, in pixels.
   \param w The width of the rectangle to copy from the backbuffer, in pixels.
   \param h The height of the rectangle to copy from the backbuffer, in pixels.
   \param buf A buffer of size w * h * 4 to receive the requested data.
   \return True if the operation succeeded and buf was populated.
 */
    bool CopyLuminanceFromScreen(int x, int y, int w, int h, unsigned char *buf);

/** Copies RGBA data from system memory into a given texture map.
   \param texture The texture to modify with the specified RGBA buffer.
   \param w The width of the RGBA image in the specified buffer, in texels.
   \param h The height of the RGBA image in the specified buffer, in texels.
   \param buf A pointer to a buffer of size w * h * 4 that contains the RGBA data to
   update the texture with.
   \return True if the operation succeeded.
 */
    bool CopyLuminanceIntoTexture(TextureHandle texture, int w, int h, unsigned char *buf);

/** Begin an occlusion query; during an occlusion query, any drawing will increment a counter
   of how many fragments passed the depth and stencil tests.
   \param queryHandle Pointer to receive a handle to the occlusion query, which will be later passed to
                   EndOcclusionQuery() and GetOcclusionQueryResults().
   \return True if occlusion queries are supported on this system, false if not.
 */
    bool StartOcclusionQuery(QueryHandle *queryHandle);

/** End an occlusion query. \sa StartOcclusionQuery()
   \param queryHandle The handle returned by StartOcclusionQuery() representing the occlusion query to end.
   \return False if occlusion queries are not supported on this system.
 */
    bool EndOcclusionQuery(QueryHandle queryHandle);

/** Get the results of an occlusion query; should be called after EndOcclusionQuery(). Ideally, let
   some work happen between calling EndOcclusionQuery() and GetOcclusionQueryResults(), as this call may
   block otherwise.
   \param queryHandle The handle returned by StartOcclusionQuery() representing the occlusion query to end.
   \return The number of samples that passed the depth and stencil tests during the occlusion query.
 */
    unsigned int GetOcclusionQueryResults(QueryHandle queryHandle);

/** Retrieves if fog was previously enabled via a call to EnableFog(true). */
    bool GetFogEnabled() const {
        return fogEnabled;
    }

/** Retrieves the fog density parameter for exponential fog, as previously set by ConfigureFog(). */
    double GetFogDensity() const {
        return fogDensity;
    }

/** Retrieves the linear fog start depth, as previously set by ConfigureFog(). */
    double GetFogStart() const {
        return fogStart;
    }

/** Retrieves the linear fog end depth, as previously set by ConfigureFog(). */
    double GetFogEnd() const {
        return fogEnd;
    }

/** Retrieves the current fog color, as previously set by ConfigureFog(). */
    const Color& GetFogColor() const {
        return fogColor;
    }

/** Submits a translucent object to the Renderer that must be sorted against all other
   translucent objects at the end of the frame and drawn together.
   \sa SortAndDrawBlendedObjects()
 */
    void SubmitBlendedObject(Renderable *obj);

/** Removes a Renderable from the list of translucent objects, if it exists.
   \sa SubmitBlendedObject()
 */
    void RemoveBlendedObject(Renderable *obj);

/** Clears the list of blended objects to be drawn. Called implicitly at the start of every frame. */
    void ClearBlendedObjects();

/** Sorts any Renderable objects submitted since the last call to SortAndDrawBlendedObjects() via
   SubmitBlendedObject() from back to front from the camera position, and then renders them by calling
   their Draw() methods. Useful for proper rendering of translucent objects (like clouds) against each
   other. 
   
   \param enableDepthTest Whether depth testing against the depth buffer is enabled while drawing translucent
   objects. You would only set this to true in a situation where you have an undefined depth buffer, as in 
   deferred shading that uses a depth texture instead of the depth buffer.
   
   \param enableDepthWrites Whether depth buffer writes will be forced. Normally you would only do this in
   very specialized situations such as rendering clouds to a depth buffer for shadows.
   */
    void SortAndDrawBlendedObjects(bool enableDepthTest, bool enableDepthWrites, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride, ThreadCameraStreamData* tcsData);

    /** Sorts the list of translucent objects back to front. */
    void SortBlendedObjects(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, ThreadCameraStreamData* tcsData);

    /** Draws the list of translucent objects without first sorting them. */
    void DrawBlendedObjects(bool enableDepthTest, bool enableDepthWrites, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride, ThreadCameraStreamData* tcsData);

    const SL_VECTOR(Renderable *)& GetBlendedObjects() const {
        return blendedObjects;
    }

    /** When sorted blended objects are being drawn, it indicates the index of the object in the sorted list being currently
        drawn. This makes it accessible in the DrawBlendedObject for example. */
    int GetCurrentBlendedObjectBeingDrawnIndex(void) const {
        return currentBlendedObjectBeingDrawn;
    }

/** Sets whether viewport and projection matrices have been set explicitly by the application. */
    void SetHasExplicitMatrices(bool on) {
        hasExplicitMatrices = on;
    }

/** Sets whether the viewport has been set explicitly by the application. */
    void SetHasExplicitViewport(bool on) {
        hasExplicitViewport = on;
    }

/** Sets whether the depth range has been set explicitly by the application. */
    void SetHasExplicitDepthRange(bool on) {
        hasExplicitDepthRange = on;
    }

    Matrix4 GetScaleAndBiasMatrix(void) const;

/** Internal */
    bool AreEqual(const Camera* camera);

    bool HasInstancing(void) const;
    bool HasBindlessTextures(void) const;
    bool HasUBOs(void) const;
    bool HasSSBOs(void) const;
	bool Has64BitSupport(void) const;
	bool HasBindlessIndirectRendering(void) const;

    Buffer* CreateBuffer(const char* name, BufferType bufferType, int size, const BufferProperties& _bufferProperties);
    void DestroyBuffer(Buffer* buffer);

    GPUAddressType GetTextureAddress(TextureHandle texture);
    void MakeTextureAddressResident(GPUAddressType textureAddress);

	GPUAddressType GetBufferAddress(BufferHandle buffer);
	void MakeBufferResident(BufferHandle buffer, bool makeResident);
	bool IsBufferResident(BufferHandle buffer);

    const RenderableListener* GetListener(void) const;
    RenderableListener* GetListener(void);

    //! internal. do not use!
    void SetForceImmediate(bool val);

    //! internal. do not use!
    bool GetForceImmediate(void) const;

    //! internal. do not use!
    bool ExecuteStream(void);

    //! internal. do not use!
    void SetStream(void* stream, int frameIndex);

    //! internal. do not use!
    void SettingsChanged(void* environment);

    //! internal. do not use!
    void* GetContext(void);

    //! internal. do not use!
    bool SupportsInstancedRendering(void) const;

	//! internal. do not use!
	bool SupportsBindlessIndirectRendering(void) const;

	//! internal. do not use!
    bool SupportsConstantLocations(void) const;
private:
    int type;

    bool blendingEnabled;
    int lastSrc, lastDst;
    TextureHandle lastTex;
    ShaderHandle lastShader;

    bool fogEnabled;
    double fogDensity, fogStart, fogEnd;
    Color fogColor;

    bool alwaysApplyState;

    Matrix4 projection, modelview, modelviewproj, texture;

    int vpX, vpY, vpW, vpH;
    float minZ, maxZ;
    double fov;
    bool disableDepthReads, disableDepthWrites;
    bool hasExplicitMatrices, hasExplicitViewport, hasExplicitDepthRange;

    bool rightHanded;

    bool cullingEnabled, cullingUndefined;

    SL_VECTOR(Renderable *) blendedObjects;
    int currentBlendedObjectBeingDrawn;

    RenderableListener* listener;

    void* context;
private:
    static MutexContainer internalMutexContainer;
    static int refCount;
    static bool triedToLoadDll;
    static bool procsInitialized;

    const ThreadCameraStreamData& tcsData;

    bool avoidStalls;
};
}

#endif
