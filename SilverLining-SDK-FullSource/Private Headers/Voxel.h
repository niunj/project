// Copyright (c) 2004-2012  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Voxel.h
    \brief A single cell in the cellular automata that comprises a CumulusCloud.
 */

#ifndef VOXEL_H
#define VOXEL_H

#include "Metaball.h"
#include "Color.h"
#include "Vector3.h"
#include "Frustum.h"
#include "VoxelSortingModes.h"

#include <bitset>
#include <stdio.h>
#include <iostream>

//#define USE_BITSET

namespace SilverLining
{
enum VoxelFlags
{
    hasCloud = 0,
    vapor,
    phaseTransition,
    lightning,
    created,
    destroyed,
    NUM_FLAGS
};

class BillboardBuffer;

/** A single cell in the cellular automata that makes up a CumulusCloud. Extends a
   Metaball which models the actual lighting of this cell, which in turn contains a
   Billboard that performs the actual rendering.
 */
class Voxel : public Metaball
{
public:
/** Constructs a voxel of a specified size and density.
   \param dimension The CumulusCloud is broken into a 3D array of Voxels, which
   may be thought of as little cubes stacked against each other. The dimension
   is the length of any side of an individual cube, in world units.
   \param particleSize The diameter of the simulated particles that make up
   the cloud puff within this voxel, specifically the water droplets. This is
   used to compute the optical depth of this voxel.
   \param maxDensity The simulated density of the cloud puff, in particles per
   cubic world unit. This is also used to compute the optical depth of this
   voxel.
   \param spinRate The maximum rate of the voxel spin, in radians per second
 */
    Voxel(double dimension, double particleSize, double maxDensity, double spinRate, int atlasIdx, const float rotateAngle, const float rotateSpeed, bool shaderInstancing, bool hdr, const BillboardBuffer& buffer);

    /** Restores a voxel from disk. */
    Voxel(std::istream& s, const RandomNumberGenerator* rng, bool shaderInstancing, bool hdr, const BillboardBuffer& buffer);

    /** Allocates the underlying Billboard resources on the GPU if they haven't already been allocated. */
    void AllocateGraphics(double dimension, double spinRate, int atlasIdx, const bool randomRotate, const RandomNumberGenerator* rng, bool shaderInstancing, bool hdr, const BillboardBuffer& buffer);


    /** This version of the constructor initalizes the state of the voxel, but does not create
    any associated resources on the GPU. If this voxel must actually render a billboard,
    AllocateGraphics() must subsequently be called on it when using this constructor. */
    Voxel(double dimension, double particleSize, double maxDensity);

/** Destructor; disposes of the Metaball contained by this class. */
    virtual ~Voxel();

/** Saves a voxel to disk. */
    bool Serialize(std::ostream& s);

/** Returns the simulated radius of the cloud puff within this voxel, in world units. */
    static double ComputeVoxelRadius(double dimension) {
        return 1.4142 * dimension;
    }

/** Retreives whether this voxel contains a simulated cloud puff or not. */
    bool GetHasCloud() const {
#ifdef USE_BITSET
        return flags[hasCloud];
#else
        return (flags & (1 << hasCloud)) != 0;
#endif
    }

/** Sets whether this voxel contains a simulated cloud puff or not. */
    void SetHasCloud(bool b) {
#ifdef USE_BITSET
        flags.set(hasCloud, b);
#else
        if (b)
            flags |= (1 << hasCloud);
        else
            flags &= ~(1 << hasCloud);
#endif
    }

/** Gets whether this voxel is modeled to contain a high amount of water vapor. */
    bool GetVapor() const {
#ifdef USE_BITSET
        return flags[vapor];
#else
        return (flags & (1 << vapor)) != 0;
#endif
    }

/** Sets whether this voxel is modeled to contain a high amount of water vapor. */
    void SetVapor(bool b) {
#ifdef USE_BITSET
        flags.set(vapor, b);
#else
        if (b)
            flags |= (1 << vapor);
        else
            flags &= ~(1 << vapor);
#endif
    }

/** Gets whether this voxel is currently illuminated by lightning */
    bool GetHasLightning() const {
#ifdef USE_BITSET
        return flags[lightning];
#else
        return (flags & (1 << lightning)) != 0;
#endif
    }

/** Sets whether this voxel is currently illuminated by lightning */
    void SetHasLightning(bool b) {
#ifdef USE_BITSET
        flags.set(lightning, b);
#else
        if (b)
            flags |= (1 << lightning);
        else
            flags &= ~(1 << lightning);
#endif
    }

/** Gets whether this voxel is undergoing a "phase transition" from water vapor. */
    bool GetPhaseTransition() const {
#ifdef USE_BITSET
        return flags[phaseTransition];
#else
        return (flags & (1 << phaseTransition)) != 0;
#endif
    }

/** Sets whether this voxel is undergoing a "phase transition" from water vapor. */
    void SetPhaseTransition(bool b) {
#ifdef USE_BITSET
        flags.set(phaseTransition, b);
#else
        if (b)
            flags |= (1 << phaseTransition);
        else
            flags &= ~(1 << phaseTransition);
#endif
    }

/** Gets whether this voxel was created since the last iteration and should be fading in. */
    bool GetCreated() const {
#ifdef USE_BITSET
        return flags[created];
#else
        return (flags & (1 << created)) != 0;
#endif
    }

/** Sets whether this voxel was created since the last iteration. */
    void SetCreated(bool b) {
#ifdef USE_BITSET
        flags.set(created, b);
#else
        if (b)
            flags |= (1 << created);
        else
            flags &= ~(1 << created);
#endif
    }

    /** Gets whether this voxel was destroyed since the previous iteration and should be fading out. */
    bool GetDestroyed() const {
#ifdef USE_BITSET
        return flags[destroyed];
#else
        return (flags & (1 << destroyed)) != 0;
#endif
    }

/** Sets whether this voxel was destroyed since the previous iteration. */
    void SetDestroyed(bool b) {
#ifdef USE_BITSET
        flags.set(destroyed, b);
#else
        if (b)
            flags |= (1 << destroyed);
        else
            flags &= ~(1 << destroyed);
#endif
    }

/** Returns the optical depth of this voxel. */
    float GetOpticalDepth() const {
        return opticalDepth;
    }

private:

#ifdef USE_BITSET
    std::bitset<6> flags;
#else
    char flags;
#endif

    float opticalDepth;

};
}

#endif
