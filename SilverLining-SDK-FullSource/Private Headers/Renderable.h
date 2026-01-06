// Copyright (c) 2004-2008  Sundog Software, LLC All rights reserved worldwide.

/**
    \file Renderable.h
    \brief An interface for objects that are drawable.
 */

#ifndef RENDERABLE_H
#define RENDERABLE_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable:4786)
#endif

#include "MemAlloc.h"
#include "Frustum.h"
#include "Vector3.h"
#include "Renderer.h"
#include "Configuration.h"

namespace SilverLining
{
    class RenderableListener : public MemObject
    {
    public:
        virtual void renderableDeleted(Renderable* renderable) = 0;
    };

class Atmosphere;
class OverriddenBillboardMatrix;
class OverriddenCameraPos;
class ThreadCameraStreamData;

/** An interface for objects that are drawable. */
class Renderable : public MemObject
{
public:
/** Default constructor. 
   \param threadSafeListenerOperations whether listener operations are to be thread safe (for strato*)
*/
    Renderable(bool threadSafeListenerOperations=false);

/** Virtual destructor. */
    virtual ~Renderable();
public:

    void AddListenerIfNecessary(RenderableListener* listener);

    void RemoveListenener(RenderableListener* listener);

/** Set the position of the object in world coordinates.
   \sa GetWorldPosition()
 */
    virtual void SetWorldPosition(const Vector3& pos) {
    }

/** Retrieves the position of the object in world coordinates.
   \sa SetWorldPosition()
 */
    virtual Vector3 GetWorldPosition(const ThreadCameraStreamData* tcsData) const = 0;

    virtual Vector3 GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const {
        return GetWorldPosition(tcsData);
    }

/** Translucent objects may submit themselves to the Renderer for later drawing
   once all translucent objects in the scene have been sorted from back to front.
   The Renderer will call the object's DrawBlendedObject() method to actually
   draw the object in this case. */
    virtual void DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const = 0;

/** Adds an offset to the distance from the camera computed when sorting objects,
   in world units.
   \sa GetDepthOffset()
 */
    void SetDepthOffset(double offset) {
        depthOffset = offset;
    }

/** Retrieves the offset to the distance from the camera used when sorting objects,
   in world units.
   \sa SetDepthOffset()
 */
    double GetDepthOffset() const {
        return depthOffset;
    }

/** Computes the screen depth of this object, along the vector specified, for use in depth sorting.
   \param from The point from which to measure distance from
   \param to The point to measure the distance to
   \param mv The modelview matrix
   \return The eye space depth of the point of intersection with the offset.
 */
    virtual double GetDistance(const Vector3& from, const Vector3& to, const Matrix4& mv, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const
    {
        if (tcsData->GetSortByScreenDepth()) {
			Vector3 eyeSpace = mv * GetWorldPosition(tcsData);
			return rightHanded ? -eyeSpace.z : eyeSpace.z;
		}
		else {
			Vector3 V = from - GetWorldPosition(tcsData);
			return V.Length();
		}
    }

    virtual void SetInstanced(bool _instanced)
    {
        instanced = _instanced;
    }
    virtual bool GetInstanced(void) const
    {
        return instanced;
    }
private:
    double depthOffset;

    SL_SET(RenderableListener*) listeners;

    //! flag to indicate if this renderable
    //! is part of a multi-draw/instanced call
    bool instanced;

    Mutex* mutex;
};
}

#endif
