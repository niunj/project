// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file BillboardRenderingParameters.h
\brief A class for describing and managing data/rendering of billboards in particular thread and associated camera, stream
*/

#pragma once

#ifdef SWIG
%module SilverLiningBillboardRenderingParameters
#define SILVERLINING_API
%{
#include "BillboardRenderingParameters.h"
    using namespace SilverLining;
    % }
#endif

#include "MemAlloc.h"
#include "Color.h"
#include "Vector3.h"

namespace SilverLining
{
    class Camera;

    class BillboardRenderingParameters : public MemObject
    {
    public:
        BillboardRenderingParameters();
        virtual ~BillboardRenderingParameters();
    public:

        /** Billboards are drawn using vertex shaders whenever possible, and so are responsible
        for implementing their own fog effects. Let the billboards know what the fog color is
        by calling SetFogColor() at least once per scene, prior to any billboard drawing. */
        void SetFogColor(const Color& fogColor);

        /** Retrieves the current fog color being used to render billboards with. */
        Color GetFogColor() const;

        /** Billboards are drawn using vertex shaders whenever possible, and so are responsible
        for implementing their own fog effects. Let the billboards know how thick the fog is by
        calling SetFogDensity() at least once per frame, prior to any billboard drawing.

        \param density The density of the fog, to be used in the exponential fog equation.
        */
        void SetFogDensity(double density);

        /** Retrieves the current fog density being used to render billboards with.*/
        double GetFogDensity() const;

        //! helper to compute fog fade
        double CalculateFogFade(const Vector3& center, const Camera* camera) const;

        /** Whether billboard spin is always disabled, for example when rendering shadows. */
        void SetSpinEnabled(bool enabled);

        /** Returns current spin enabled flag. */
        bool GetSpinEnabled() const;


        /** Sets the distance used for soft particles at the base of clouds. */
        void SetSoftness(float pSoftness);

        /** Returns the current softness distance */
        float GetSoftness() const;

        /** Control whether all billboards are forced to a constant color (used for rendering shadow maps.) */
        void ForceConstantColor(bool force, const Color& color);

        /** Get the force constant color and whether force coloring is enabled */
        bool GetForceConstantColor(Color& color) const;


        /** Control whether billboards are drawn with additive blending or not. */
        void SetAdditiveBlending(bool additive);

        /** Get whether billboards are drawn with additive blending or not. */
        bool GetAdditiveBlending() const;

        /** set/get vertical gradient. */
        void SetVerticalGradient(double gradient);
        double GetVerticalGradient() const;

        /** set/get flattening. */
        void SetFlattening(float f);
        float GetFlattening() const;

        /** set/get fade start time. */
        void SetFadeStartTime(unsigned long ms);
        unsigned long GetFadeStartTime() const;

        /** set/get fog color boost. */
        void SetFogColorBoost(float val);
        float GetFogColorBoost(void) const;

    protected:
        Color fog;
        double fogDensity;
        float fogColorBoost;

        bool spinEnabled;

        float softness;

        bool forceConstantColor;
        Color forceColor;

        bool additiveBlending;

        double verticalGradient;
        float flattening;

        unsigned long fadeStartTime;
    };
}
