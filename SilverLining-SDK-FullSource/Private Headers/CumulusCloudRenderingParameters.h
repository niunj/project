// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file CumulusCloudRenderingParameters.h
\brief A class for describing and managing data/rendering of a Cumulus Cloud in particular thread and associated camera, stream
*/

#pragma once

#ifdef SWIG
%module SilverLiningCumulusCloudRenderingParameters
#define SILVERLINING_API
%{
#include "CumulusCloudRenderingParameters.h"
    using namespace SilverLining;
    % }
#endif

#include "MemAlloc.h"
#include "Color.h"
#include "Vector3.h"

namespace SilverLining
{
    class CumulusCloudRenderingParameters : public MemObject
    {
    public:
        CumulusCloudRenderingParameters(double unitScale);
        virtual ~CumulusCloudRenderingParameters();

    public:
        /** Call when a frame starts, so we know to reset our count of how many
        cloud recompiles we're allowed to do. */
        void SetFrameStarted();

        //! Read member constants from the config file.
        void ReadConfig(double unitScale);

        //! various getters
        double GetSortAngleThreshold(void) const;
        int GetRecompiles(void) const;
        int GetRecompileBudget(void) const;
        double GetColorRandomness(void) const;
        double GetColorRandomnessHiRes(void) const;
        float GetSizeRandomness(void) const;
        double GetQuickLightingAttenuation(void) const;
        double GetQuickLightingAttenuationHiRes(void) const;
        bool GetNoUpdate(void) const;
        bool GetCullInteriorVoxels(void) const;
        double GetAlphaCullThreshold(void) const;
        bool GetDarkenWithDensity(void) const;
        double GetDarkenAmount(void) const;
        bool GetSpreadOutFog(void) const;
        bool GetAtlasBlend(void) const;
        bool GetUseVrmlCloudFormat(void) const;
        unsigned int GetFogRefreshFrequency(void) const;

        int GetFogRefreshSlice(void) const;
        void IncrementRecompiles(void);
    protected:
        double sortAngleThreshold;
        int recompiles;
        int recompileBudget;
        double colorRandomness, colorRandomnessHiRes;
        float sizeRandomness;
        double quickLightingAttenuation, quickLightingAttenuationHiRes;
        bool noUpdate;
        bool cullInteriorVoxels;
        double alphaCullThreshold;
        bool darkenWithDensity;
        double darkenAmount;
        bool spreadOutFog;
        bool atlasBlend;
        bool useVrmlCloudFormat;
        unsigned int fogRefreshFrequency;

        mutable unsigned int inc;
    };
}