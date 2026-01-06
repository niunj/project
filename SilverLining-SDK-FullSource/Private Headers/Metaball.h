// Copyright (c) 2004-2016  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Metaball.h
    \brief Models the "puffs" that cumulus clouds are made of.
 */

#ifndef METABALL_H
#define METABALL_H

#include "Billboard.h"
#include "Renderer.h"
#include "Color.h"
#include <map>

namespace SilverLining
{
class Atmosphere;

/** A cumulus cloud is composed of many Voxel objects, which are elements of the cellular
   automata that generates the clouds. Each Voxel contains a Metaball, which is responsible for
   modeling the scattering of light throughout the voxel. The Metaball extends a Billboard,
   which ultimately renders each "puff" that makes up the cloud. */
class Metaball : public Billboard
{
public:

/** Default constructor. */
    Metaball() : radius(0), spinRate(0) {
    }

/** Creates the underlying Billboard. */
    void InitializeMetaball(const float radius, const float SpinRate, const int atlasIndex, const float rotateAngle, const float rotateSpeed, bool useShaderInstancing, bool hdr, const BillboardBuffer& buffer);

/** Destructor. */
    virtual ~Metaball();

/** Sets the modeled radius of the metaball in world units (important
   for modelling the amount of light scattered in the metaball.) */
    void SetRadius(double radius);

/** Retrieves modeled radius of the metaball in world units. */
    double GetRadius() const {
        return radius;
    }

/** Retrieves the maximum spin rate of the metaball in radians per second. */
    double GetSpinRate() const {
        return spinRate;
    }

/** Sets the color to modulate the metaball by. */
    void SetMetaballColor(const Color& color, bool hdr);

/** Retrieves the color previously set by SetColor(). */
    const Color& GetMetaballColor() const {
        return color;
    }

/** Sets the translucency of the metaball.

   \param alpha The alpha value that modulates the metaball's alpha channel.
   0 is transparent, 1 is opaque. */
    void SetAlpha(double alpha);

/** Computes the billboard color for the given pass. Should be called prior to DrawInBatch() */
    void ComputeBillboardColor(int pass, double colorRandomness, float ambientScattering, ThreadCameraStreamData* tcsData);

/** Tests if the metaball is visible within the specified Frustum.

   \param f The view Frustum to test visibility against.
   \param offset A vector representing the difference between the underlying Billboard's
   position and the Metaball's position in the world, if any. The Billboard may be in
   coordinates relative to its cloud, for example.
   \return True if the object is not visible and should be culled; false otherwise.
 */
    bool IsCulled(const Frustum& f, const Vector3& offset) const;


protected:
// Note - keep data members at a minimum, there are a lot of these objects.

    float radius, spinRate;
    Color color;

};
}

#endif
