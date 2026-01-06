// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file MetaballRenderingParameters.h
\brief A class for describing and managing data/rendering of all metaballs in particular thread and associated camera, stream
*/

#pragma once

#ifdef SWIG
%module SilverLiningMetaballRenderingParameters
#define SILVERLINING_API
%{
#include "MetaballRenderingParamaters.h"
    using namespace SilverLining;
    % }
#endif

#include "MemAlloc.h"

#include "Color.h"
#include "Vector3.h"

namespace SilverLining
{
    /** There are several methods for modelling the scattering of light through
    a cloud (rayleigh scattering seems to yield the best
    visual results.) Isotropic is just single-scattering. Anistropic adds forward
    scattering based on the angle between the light vector and the camera vector
    from the metaball. Lambertian is "physically correct" but tends to scatter
    too much light for realistic visual results. Rayleigh scattering is simple
    yet effective - it models multiple forward scattering. Finally, we offer
    a double-lobed Henyey-Greenstein function that allows you to tweak the
    amount of forward and back scattering simulated. */
    enum PhaseFunction
    {
        ISOTROPIC,
        ANISOTROPIC,
        LAMBERTIAN,
        RAYLEIGH,
        HENYEY_GREENSTEIN
    };


    class MetaballRenderingParameters : public MemObject
    {
    public:
        MetaballRenderingParameters();
        virtual ~MetaballRenderingParameters();

        /** Sets/Gets an ambient term for the metaballs lighting, which might model ambient
        ambient skylight.
        */
        void SetAmbientColor(const Color& amb);
        Color GetAmbientColor() const;

        /** Sets/Gets the normalized direction vector toward the dominant infinitely-distance
        light source. Must be called at least once per frame before Metaballs are drawn. */
        void SetLightPosition(const Vector3& pos);
        const Vector3& GetLightPosition(void) const;

        /** Sets/Gets the world position of the camera. Must be called at least once per
        frame before metaballs are drawn. */
        void SetCameraPosition(const Vector3& pos);
        const Vector3& GetCameraPosition() const;

        /** If the metaball's position is relative to its parent cloud, specify the
        offset between the cloud and world coordinates, so that lighting may be properly
        computed. Must be called at least once per frame before metaballs are drawn. */
        void SetOffsetPosition(const Vector3& pos);
        const Vector3& GetOffsetPosition() const;

        /** Helper. Compute the phase function*/
        double PhaseFunction(double cosa) const;

        /** Get the max metaball color*/
        float GetMaxMetaballColor(void) const;

    protected:
        void ReadConfig(void);
    protected:
        Color ambient;
        int phaseFunction;
        double henyeyGreensteinG1, henyeyGreensteinG2, henyeyGreensteinF;
        double anisotropicX;
        Vector3 lightPosition, cameraPosition, offsetPosition;
        float maxMetaballColor;
    };
}