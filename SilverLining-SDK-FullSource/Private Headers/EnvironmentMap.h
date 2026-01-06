// Copyright (c) 2013-2015 Sundog Software LLC. All rights reserved worldwide.

#ifndef ENVIRONMENT_MAP_H
#define ENVIRONMENT_MAP_H

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "Vector3.h"

namespace SilverLining
{
    class Atmosphere;
    class Camera;
    class ThreadCameraStreamData;

    class EnvironmentMap : public MemObject
    {
    public:
        EnvironmentMap(ThreadCameraStreamData* _tcsData, ColorFormat format);

        virtual ~EnvironmentMap();
    public:

        bool Create(int facesToRender, bool floatingPoint, bool drawClouds, bool drawSunAndMoon, bool geocentricMode, Atmosphere& atmosphere);

        void* GetNativeTexture() const;

    protected:
        //! internal
        void GetToAndUpVector(Vector3& to, Vector3& up, const Vector3& from, int face) const;

    protected:
        RenderTextureHandle cubeMap;
        int cubeMapDim;
        int lastFaceRendered;
        bool enableStars;
        bool floatingPoint;
        bool neverDrawClouds;
        bool generateMipmaps;

        ThreadCameraStreamData* tcsData;

        bool isDXAndFlipCubeFaces;

        const std::string name = "SilverLining::EnvironmentMap::CubeMap Texture";
    };
}
#endif
