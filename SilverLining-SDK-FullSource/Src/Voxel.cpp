// Copyright (c) 2004-2012  Sundog Software, LLC. All rights reserved worldwide.

#include "Voxel.h"
#include "Metaball.h"
#include "MathUtils.h"
#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif
#include <stdio.h>

namespace SilverLining
{
Voxel::Voxel(double dimension, double particleSize, double maxDensity, double spinRate, int atlasIdx, const float _rotateAngle, const float _rotateSpeed, bool shaderInstancing, bool hdr, const BillboardBuffer& buffer)
{
    float ballRadius = (float)ComputeVoxelRadius(dimension);

    InitializeMetaball(ballRadius, (float)spinRate, atlasIdx, _rotateAngle, _rotateSpeed, shaderInstancing, hdr, buffer);

    double T = dimension;
    opticalDepth = (float)(MathUtils::Pi() * (particleSize * particleSize) * T * maxDensity);

    double extinction = exp(-opticalDepth);
    SetAlpha(1.0 - extinction);

    SetHasLightning(false);

#ifndef USE_BITSET
    flags = 0;
#endif
}

Voxel::Voxel(std::istream& s, const RandomNumberGenerator* rng, bool shaderInstancing, bool hdr, const BillboardBuffer& buffer)
{
#ifndef USE_BITSET
    flags = 0;
#endif

    s.read((char *)&opticalDepth, sizeof(float));
    bool flag;

    s.read((char *)&flag, sizeof(bool));
    SetHasCloud(flag);
    s.read((char *)&flag, sizeof(bool));
    SetVapor(flag);
    s.read((char *)&flag, sizeof(bool));
    SetPhaseTransition(flag);
    s.read((char *)&flag, sizeof(bool));
    SetHasLightning(flag);

    float ballRadius;
    s.read((char *)&ballRadius, sizeof(float));

    float spinRate;
    s.read((char *)&spinRate, sizeof(float));

    Vector3 pos;
    pos.Unserialize(s);

    Color col;
    col.Unserialize(s);

    Color billboardCol;
    billboardCol.Unserialize(s);

    s.read((char *)&atlasIndex, sizeof(unsigned char));

    bool randomRotate = atlasIndex == 9 || atlasIndex == 13 || atlasIndex == 0;

    float _rotateAngle = 0;
    float _rotateSpeed = 0;
    if (randomRotate) {
        Billboard::GetRandomRotateAngleAndSpeed(rng, spinRate, _rotateAngle, _rotateSpeed);
    } else {
        _rotateAngle = MathUtilsFloat::Epsilon();
        _rotateSpeed = MathUtilsFloat::Epsilon();
    }

    InitializeMetaball(ballRadius, spinRate, atlasIndex, _rotateAngle, _rotateSpeed, shaderInstancing, hdr, buffer);
    SetWorldPosition(pos);
    SetMetaballColor(col, hdr);
    SetColor(billboardCol);

    SetCreated(true);
}

void Voxel::AllocateGraphics(double dimension, double spinRate, int atlasIdx, const bool randomRotate, const RandomNumberGenerator* rng, bool shaderInstancing, bool hdr, const BillboardBuffer& buffer)
{
    if (!sharedVb) {
        float ballRadius = (float)ComputeVoxelRadius(dimension);

        float _rotateAngle = 0;
        float _rotateSpeed = 0;
        if (randomRotate) {
            float _spinRate = (float)(spinRate);
            Billboard::GetRandomRotateAngleAndSpeed(rng, _spinRate, _rotateAngle, _rotateSpeed);
        } else {
            _rotateAngle = MathUtilsFloat::Epsilon();
            _rotateSpeed = MathUtilsFloat::Epsilon();
        }

        InitializeMetaball(ballRadius, (float)spinRate, atlasIdx, _rotateAngle, _rotateSpeed, shaderInstancing, hdr, buffer);
    }
}

Voxel::Voxel(double dimension, double particleSize, double maxDensity)
{
    double T = dimension;
    opticalDepth = (float)(MathUtils::Pi() * (particleSize * particleSize) * T * maxDensity);

    double extinction = exp(-opticalDepth);
    SetAlpha(1.0 - extinction);

    SetHasLightning(false);

#ifndef USE_BITSET
    flags = 0;
#endif
}


bool Voxel::Serialize(std::ostream& s)
{
    s.write((char *)&opticalDepth, sizeof(float));

    bool flag;

    flag = GetHasCloud();
    s.write((char *)&flag, sizeof(bool));
    flag = GetVapor();
    s.write((char *)&flag, sizeof(bool));
    flag = GetPhaseTransition();
    s.write((char *)&flag, sizeof(bool));
    flag = GetHasLightning();
    s.write((char *)&flag, sizeof(bool));


    float ballRadius = (float)GetRadius();
    s.write((char *)&ballRadius, sizeof(float));

    float spinRate = (float)GetSpinRate();
    s.write((char *)&spinRate, sizeof(float));

    const Vector3f& fPos = GetWorldPosition();
    Vector3 pos(fPos.x, fPos.y, fPos.z);
    pos.Serialize(s);

    Color col = GetMetaballColor();
    col.Serialize(s);

    col = GetColor();
    col.Serialize(s);

    s.write((char *)&atlasIndex, sizeof(unsigned char));

    return true;
}

Voxel::~Voxel()
{
}

}
