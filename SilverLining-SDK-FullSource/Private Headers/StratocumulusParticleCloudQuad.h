// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/** \file StratocumulusParticleCloudQuad.h
\brief
*/

#pragma once

#include "MemAlloc.h"
#include "Vector3.h"
#include "Matrix3.h"
#include "Color.h"
#include "Billboard.h"

namespace SilverLining
{
    class StratocumulusParticleCloudLayer;
    class StratocumulusPuff;
    class Camera;
    class TextureManager;
    class QuadBatches;
	class StratocumulusParticleCloudQuadCompilationOperation;

    class StratocumulusParticleCloudQuad
    {
    public:
        StratocumulusParticleCloudQuad(StratocumulusParticleCloudLayer *pParent, const Vector3& pCenter, double pSize, double pThickness, double pVoxelSize, ThreadCameraStreamData* _tcsData);

        ~StratocumulusParticleCloudQuad();

        void Invalidate();

        bool operator < (const StratocumulusParticleCloudQuad& q);

        bool Draw(double fade, const Matrix3& basis, const Color& lightColor, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
            , ThreadCameraStreamData* tcsData);

        void UpdateBounds(const Matrix3& basis);

        void UpdateBounds();

        const Vector3& GetWorldCenter() const;

        void SetLocalCenter(const Vector3& pos);

        const Vector3& GetLocalCenter() const;

        void SetLocalOrigin(const Vector3& pos);

        const Vector3& GetLocalOrigin() const;

        bool Cull(const Camera* camera, const Camera* cullCamera);

        void ClearMetaballs();

        SL_VECTOR(StratocumulusPuff*)& GetMetaballs();

        double GetSize() const;

        bool GetNeedsGeocentricPlacement() const;

        void SetNeedsGeocentricPlacement(bool b);

        void SetCurvatureDisplacement(double d);

        double GetCurvatureDisplacement() const;

    private:
        Vector3 center, localOrigin;
        double width, thickness, voxelSize;
        Vector3 bounds[8];
        SL_VECTOR(StratocumulusPuff*) metaballs;
        QuadBatches *compiledObject;
        StratocumulusParticleCloudLayer *parentLayer;
        Vector3 lastWorldPos, centerWorldCoords;
        bool culled;
        Vector3 lastCameraPos;
        static double sortAngleThreshold;
        static int recompileFrameLimit;
        bool needsGeo;
        double curvatureDisplacement;
        Matrix3 lastBasis;
        bool hasLastBasis;
        double randomness;
        float ambientScattering;
        double heightVariation;

        friend class StratocumulusParticleCloudQuadCompilationOperation;
        mutable StratocumulusParticleCloudQuadCompilationOperation* compilationParameters;

        ThreadCameraStreamData* tcsData;
        TextureHandle cloudTexture;

    };
}