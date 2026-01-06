// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file LuminanceMapper.h
    \brief Manages tone-mapping for objects in the scene.
 */

#ifndef LUMINANCE_MAPPER_H
#define LUMINANCE_MAPPER_H

#include "MemAlloc.h"

namespace SilverLining
{
class Vector3;

/** Manages tone-mapping of high dynamic range images (such as the sky!) to
   values a computer display can reproduce. In addition to dynamic range
   compression, it also simulates scotopic vision by applying a blue-shift
   and loss of color information as lighting conditions degrade.

   See DURAND, F., AND DORSEY, J. 2000. Interactive tone mapping. In <i>Eurographics
   Workshop on Rendering</i>, 219Ð230 for some of the ideas behind this
   class.
 */
class LuminanceMapper : public MemObject
{
public:
    LuminanceMapper();
    ~LuminanceMapper();
public:

/** Sets the modeled maximum luminance of the display, in "nits", or
   candela per square meter. */
    void SetMaxDisplayLuminance(double nits);

    void EnableToneMapping(bool enabled) {
        disableToneMapping = !enabled;
    }

/** Sets the log-average of the scene's luminance as perceived by both
   the eye's rods and cones, in nits. */
    void SetSceneLogAvg(double rodNits, double coneNits);

/** Performs tone-mapping on an xyY color, where x and y are
   chromaticity and Y is luminance. The values passed in are modified
   by this method. Assumes that SetMaxDisplayLuminance() and
   SetSceneLogAvg() were previously called. */
    void DurandMapper(double *x, double *y, double *Y) const;

/** Performs tone-mapping on a XYZ color. The color passed in is
   modified by this method. Assumes that SetMaxDisplayLuminance() and
   SetSceneLogAvg() were previously called. */
    void DurandMapperXYZ(Vector3 *XYZ) const;

/** Returns the computed scale factors for mapping luminance for
   both the eye's rods and cones. Assumes SetMaxDisplayLuminance() and
   SetSceneLogAvg() were previously called. */
    void GetLuminanceScales(double *rodSF, double *coneSF) const
    {
        *rodSF = mR;
        *coneSF = mC;
    }

/** Retrieves the maximum display luminance previously set by
   SetMaxDisplayLuminance(). */
    double GetMaxDisplayLuminance() const {
        return Ldmax;
    }

/** Returns the luminance, in nits, that is mapped to the maximum
   luminance the display can represent. Luminances higher than this are displayed
   as white. Assumes SetMaxDisplayLuminance() and SetSceneLogAvg() were
   previously called. */
    double GetBurnoutLuminance() const;

/** Retrieves the computed blend factor between rod and cone perception
   based on the current lighting conditions. Assumes SetSceneLogAvg() was
   previously called. */
    double GetRodConeBlend() const {
        return k;
    }

/** Retrieves the log-average rod and cone luminances in nits, as
   previously set by SetSceneLogAvg(). */
    void GetSceneLogAvg(double *rodNits, double *coneNits)
    {
        *rodNits = LsavgR;
        *coneNits = LsavgC;
    }

private:
    void ComputeScaleFactors();
    double Ldmax, Lsavg;
    double mR, mC, k, LsavgR, LsavgC;
    bool disableToneMapping;
	double scotopicX, scotopicY, scotopicZ;
};
}

#endif
