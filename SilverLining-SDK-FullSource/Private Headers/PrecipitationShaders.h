// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file PrecipitationShaders.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

#include "PrecipitationShaderConstantLocations.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class PrecipitationShaders : public MemObject
    {
    public:
        PrecipitationShaders(ThreadCameraStreamData* _tcsData);
        virtual ~PrecipitationShaders();
    public:
        /** Reloads the precipitation shaders, re-linking in any user shaders. */
        void ReloadShaders();

        /** Retrieves the precipitation shader program.
        **!!Deprecated!!**
        */
        ShaderHandle GetShaderProgram() const;

        /** Retrieves the precipitation shader program for rain (instanced version). */
        ShaderHandle GetShaderProgramInstancedRain() const;

        /** Retrieves the precipitation shader program for snow (instanced version). */
        ShaderHandle GetShaderProgramInstancedSnow() const;

        /** Retrieves the precipitation shader program for sleet (instanced version). */
        ShaderHandle GetShaderProgramInstancedSleet() const;

        /** Retrieves the precipitation shader program constant locations for rain (instanced version). */
        const PrecipitationShaderConstantLocations& GetShaderProgramInstancedRainLocations() const;

        /** Retrieves the precipitation shader program constant locations for sleet (instanced version). */
        const PrecipitationShaderConstantLocations& GetShaderProgramInstancedSleetLocations() const;

        /** Retrieves the precipitation shader program constant locations for snow (instanced version). */
        const PrecipitationShaderConstantLocations& GetShaderProgramInstancedSnowLocations() const;
    private:
        void InitShaders();
        void DeleteShaders();
    private:
        ThreadCameraStreamData* tcsData;

        ShaderHandle precipShader;
        PrecipitationShaderConstantLocations shaderAndConstantLocationsRain;
        PrecipitationShaderConstantLocations shaderAndConstantLocationsSnow;
        PrecipitationShaderConstantLocations shaderAndConstantLocationsSleet;

    };
}