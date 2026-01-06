// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudRenderingParameters.h
\brief A class for describing and managing data/rendering of a Cloud in particular thread and associated camera, stream
*/

#pragma once

#ifdef SWIG
%module SilverLiningCloudRenderingParameters
#define SILVERLINING_API
%{
#include "CloudRenderingParameters.h"
    using namespace SilverLining;
    % }
#endif

#include "MemAlloc.h"
#include "Color.h"
#include "Vector3.h"

namespace SilverLining
{
    class CloudRenderingParameters : public MemObject
    {
    public:
        CloudRenderingParameters();
        virtual ~CloudRenderingParameters();
    public:
        /** Clouds are lit by directional light from the sun and/or moon, which is scattered throughout
        the cloud, but they are also lit by ambient skylight. While real skylight does vary somewhat
        by direction, for performance reasons we simplify skylight to a non-directional ambient term.
        Set the skylight color using this method. */
        void SetAmbientColor(const Color& val);

        double GetCosLightChangeAngle(void) const;
    protected:
        Color ambientColor;
        double cosLightChangeAngle;
    };
}