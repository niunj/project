// Copyright (c) 2013-2017 Sundog Software, LLC. All rights reserved worldwide.

#ifndef SHADOW_MAP_H
#define SHADOW_MAP_H

#include "SilverLiningTypes.h"
#include "Color.h"
#include "MemAlloc.h"
#include "Matrix4.h"
#include <vector>

namespace SilverLining
{
    class Atmosphere;
    class CloudLayer;
    class Camera;
	class CloudLayerSorterProvider;
	class CloudLayerSorter;
    class ThreadCameraStreamData;

    class ShadowMap : public MemObject
    {
    public:
        ShadowMap(const Atmosphere& atmosphere, int texDim, ThreadCameraStreamData* _tcsData);

        virtual ~ShadowMap();

        bool UpdateShadowCamera(const Vector3& lightDirection, bool wholeLayers, double maxShadowDistance, const Camera* camera, ThreadCameraStreamData* tcsData);

        bool StartDrawing(const Color& shadowColor, bool moonShadows, double shadowFactor, const Camera* camera, ThreadCameraStreamData* tcsData);

        bool DrawToRenderTexture(const Camera* sceneCamera, const Camera* renderCamera, bool fadeOverride, ThreadCameraStreamData* tcsData);

        bool EndDrawing(ThreadCameraStreamData* tcsData);

        bool IsRendering() const {return isRendering;}

        TextureHandle GetTextureHandle(ThreadCameraStreamData* tcsData);

        const Camera* GetShadowCamera(void) const;

        const Vector3& GetLightWorldPos(void) const;

        void SortClouds(const Camera* camera, bool enforceStrictWeak, ThreadCameraStreamData* tcsData);

		const SL_VECTOR(CloudLayer*)& GetSortedCloudLayers(void) const;

    private:

        void UpdateFrustumWorldSpace(const SL_VECTOR(CloudLayer *)& cloudLayers, bool wholeLayers, double maxShadowDistance, const Camera* camera, ThreadCameraStreamData* tcsData);

        void UpdateLightMatricesAndThenShadowCamera(const Vector3& lightDir, bool wholeLayers, const Camera* camera, ThreadCameraStreamData* tcsData);

        Matrix4 billboard;
        Vector3 frustumCornersWorld[8];
        Vector3 frustumCentroidWorld;
        double frustumBoundRadius;
        Vector3 lightWorldPos, savedCamPos;
        RenderTextureHandle texture;
        int texSize;
        bool cleared;
        bool hasShadows;
        bool isRendering;
        int savedX, savedY, savedW, savedH;
        double savedDensity;
        float shadowBlend;

        Camera* shadowCamera;

		CloudLayerSorterProvider* cloudLayerSorterProvider;

		CloudLayerSorter* sorter;

        const Atmosphere& atmosphere;

        ThreadCameraStreamData* tcsData;

        //bool justReset;
    };
}

#endif

