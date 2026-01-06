// Copyright (c) 2004-2016  Sundog Software, LLC. All rights reserved worldwide.

#include "Metaball.h"
#include "Configuration.h"
#include "Billboard.h"
#include "Utils.h"
#include "TGALoader.h"
#include "Atmosphere.h"
#include "MetaballRenderingParameters.h"
#include "MathUtils.h"

namespace SilverLining
{
void Metaball::InitializeMetaball(const float _radius, const float _spinRate, const int atlasIndex, const float _rotateAngle, const float _rotateSpeed, bool useShaderInstancing, bool hdr, const BillboardBuffer& buffer)
{

    SetAtlasIndex(atlasIndex);

    Initialize(_rotateAngle, _rotateSpeed, _radius*2.0f, useShaderInstancing, buffer);
    SetMetaballColor(Color(1, 1, 1, 1), hdr);

    radius = (float)_radius;
    spinRate = (float)_spinRate;
}

Metaball::~Metaball()
{
}

void Metaball::ComputeBillboardColor(int pass, double colorRandomness, float ambientScattering, ThreadCameraStreamData* tcsData)
{
    const MetaballRenderingParameters* params = tcsData->GetMetaballRenderingParams();

    if (pass == 0) {
        float phase = (float)params->PhaseFunction(1.0);

        Color c(color.r * phase, color.g * phase,
                color.b * phase, color.a);

        c.ClampToUnitOrLess(tcsData->GetHDREnabled());

        SetColor(c);

    } else {

        const Vector3 offsetPosition = params->GetOffsetPosition();
        const Vector3 cameraPosition = params->GetCameraPosition();
        const Color ambient = params->GetAmbientColor();
        const Vector3 lightPosition = params->GetLightPosition();

        // Todo: move this to a shader, as this gets cached and the camera moves.
        const Vector3f& b = GetWorldPosition();
        Vector3 w = (Vector3(b.x, b.y, b.z) + offsetPosition - cameraPosition);
        w.Normalize();
        double cosTheta = w.Dot(lightPosition);
        float phase = (float)params->PhaseFunction(cosTheta);

        double r = MathUtils::Abs(fmod(b.x + b.y + b.z, 100.0f)) * 0.01;
        r = (1.0 - colorRandomness) + (r * colorRandomness);

        // diffuse + ambient. Fog applied in billboard shader.
        Color c((color.r * phase + ambient.r * ambientScattering),
                (color.g * phase + ambient.g * ambientScattering),
                (color.b * phase + ambient.b * ambientScattering), color.a);

        c.r *= (float)r;
        c.g *= (float)r;
        c.b *= (float)r;
        c.a *= (float)r;

        c.ScaleToValueOrLess(params->GetMaxMetaballColor(), tcsData->GetHDREnabled());

        SetColor(c);

    }
}

void Metaball::SetMetaballColor(const Color& pColor, bool hdr)
{
    color.ClampToUnitOrLess(hdr);

    color.r = pColor.r;
    color.g = pColor.g;
    color.b = pColor.b;
}

void Metaball::SetAlpha(double alpha)
{
    color.a = alpha > 1.0 ? 1.0f : (float)alpha;
}


bool Metaball::IsCulled(const Frustum& f, const Vector3& offset) const
{
    const Vector3f& wp = GetWorldPosition();
    Vector3 center = Vector3(wp.x, wp.y, wp.z) + offset;
    for (int i = 0; i < f.GetNumCullingPlanes(); i++) {
        const Plane& p = f.GetPlane(i);
        double origin = (p.GetNormal()).Dot(center) + p.GetDistance();

        if (origin < -radius) {
            return true;
        }
    }

    return false;
}
}