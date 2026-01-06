// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

/** \file Billboard.h
   \brief Classes that support the efficient drawing of quads oriented toward the viewer.
 */

#ifndef BILLBOARD_H
#define BILLBOARD_H

#if defined(WIN32) || defined(WIN64)
#pragma warning(disable: 4786)
#endif

#include "SLMap.h"
#include "VertexBuffer.h"
#include "Renderer.h"
#include "MemAlloc.h"

namespace SilverLining
{
class Atmosphere;
class SharedVertexBuffer;
class Camera;
class RandomNumberGenerator;

class BillboardBuffer;
class BillboardShader;

/** The Billboard object represents a quad that is always oriented toward the viewer.
    Since it may be textured with non-symmetric textures, orientation relative to
    the world is important. It will honor the "up" vector and rotate about it when
    possible, however it will orient toward the viewer regardless of the "up"
    direction when necessary for large view angles.

    The Billboard uses every trick it can get away with to maximize drawing speed,
    such as vertex shaders and vertex buffer objects.
 */
class Billboard : public MemObject
{
    friend class SharedVertexBuffer;
public:
/** Constructor.
 */
    Billboard();

/** Initialize the resources for rendering this Billboard; should be called right after instantiating.

    \param randomRotate If true, the billboard will be rolled by a random amount
    relative to the viewpoint. This is used to randomize the orientation of
    cloud puffs that make up a cloud, in order to make them look more natural.
    \param diameter The size of the billboard in world uits
    \param spinRate The maximum rate at which the billboard spins, in radians per second.
 */
    void Initialize(float rotateAngle, float rotateSpeed, const float diameter, bool useShader, const BillboardBuffer& buffer);

/** Destructor; also destroys any shared resources that may no longer be in
    use required for the rendering of this billboard. */
    virtual ~Billboard();

/** Sets the diameter of this billboard in terms of screen pixels.  In this
   mode, the billboard will always appear as the same screen size. */
    void SetScreenSize(double diameter, const Vector3& camPos, ThreadCameraStreamData* tcsData);

/** Sets the diameter of this billboard in terms of world units. In this
   mode, the billboard will be subject to perspective correction. */
    void SetWorldSize(double diameter);

/** Sets the diameter of this billboard in terms of view degrees. In this
   mode, the billboard will always appear as the same screen size. Useful
   for astronomical objects, such as the sun and the moon, whose size is known
   as 0.5 degrees across. */
    void SetAngularSize(double degrees);

/** Returns the radius or 1/2 the diameter of this billboard. */
    float GetRadius() const { return radius; }

/** Draws this billboard individually, with the given texture. It's actually
    quite inefficient to draw billboards individually; normally you'd want to
    draw a bunch of them together using StartBatchDraw(), DrawInBatch(), and
    EndBatchDraw(). Otherwise a whole bunch of state needs to be set up
    and torn down as part of this method.

    \param tex The texture to draw this billboard with.
    \param pass Set to 0 for the lighting pass, 1 for the drawing pass.
   \param fade The "alpha" value of the billboard. 0 is transparent, 1 is fully visible.
    \param objectSpace whether the billboard vertices are in object space or world space.
    If in object space, the vertices will be transformed by the local basis.

    \return true if the operation completed successfully.
 */
    virtual bool Draw(TextureHandle tex, int pass, double fade, const Vector3& outputScale, bool objectSpace, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
        , ThreadCameraStreamData* tcsData, bool infiniteDepth) const;

/** Draws this billboard as part of a batch of billboards, thereby bypassing
    all the state setup required. Must be wrapped by calls to StartBatchDraw()
    and EndBatchDraw().

    \param drawImmediately Normally, all drawing is deferred until the end of
    the frame, so we can sort all of the translucent objects in the scene.
    If instead, you need to draw this billboard *right now*, you can set this
    to true.

    \return true if the operation completed successfully.
 */
    bool DrawInBatch(bool drawImmediately, const Camera* renderCamera, const OverriddenBillboardMatrix* overriddenBillboardMatrix, ThreadCameraStreamData* tcsData) const;

/** Tests if this billboard is visible within the given Frustum.

    \param f The Frustum to test visibility against.

    \return true if the billboard is not visible in this frustum, and should
    be culled.
 */
    virtual bool IsCulled(const Frustum& f) const;

/** Sets the position of this billboard in world coordinates. */
    void SetWorldPosition(const Vector3f& pos);

/** Retrieves the position of this billboard in world coordinates. */
    const Vector3f& GetWorldPosition() const {
        return centerPos;
    }

/** Sets the color to modulate this billboard by. */
    void SetColor(const Color& col);

/** Gets the color to modulate this billboard by. */
    Color GetColor() const;

/** Set the rate at which to fade this billboard per second. */
    void SetFadeRate(float fadeRate);

/** Sets the texture atlas index to use, assuming a 4x4 atlas texture. Index 0 means no atlas is in use
    and the entire texture should be used. Index 1 is the top-left-most texture, and 16 is the bottom-right. */
    void SetAtlasIndex(int idx) {
        atlasIndex = (unsigned char)idx;
    }

/** Retrieves the atlas texture index set via SetAtlasIndex(), or 0 if no atlas is in use. */
    int GetAtlasIndex() const {
        return atlasIndex;
    }

    //! helper
    static void GetRandomRotateAngleAndSpeed(const RandomNumberGenerator* rng, const float spinRate, float& rotateAngle, float& rotateSpeed);

protected:
    void UpdateQuadVertices(bool changeSize, double newSize, bool rotate = true, float fadeRate = 0.0f);

protected:
// Note - keep data members to a minimum, there are a lot of these objects.

    SharedVertexBuffer *sharedVb;
    int vbIdx;
    Vector3f centerPos;
    Color billboardColor;
    float rotateAngle, rotateSpeed, radius, fadeRate;
    float absRotateSpeed;
    unsigned char atlasIndex;
    bool useShader;
    static const Color theWhiteColor;
};
}

#endif
