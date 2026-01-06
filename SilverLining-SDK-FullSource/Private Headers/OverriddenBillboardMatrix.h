// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/** \file OverriddenBillboardMatrix.h
*/

#pragma once

#include "MemAlloc.h"
#include "Matrix4.h"

namespace SilverLining
{
    class Camera;

    class OverriddenBillboardMatrix : public MemObject
    {
    public:
        OverriddenBillboardMatrix(const Camera* camera);
        OverriddenBillboardMatrix& operator=(const Matrix4& rhs);
        OverriddenBillboardMatrix(const Matrix4& matrix);
        const Matrix4& GetBillboardMatrix(void) const;
        bool isSet;

        Matrix4 billboardMatrix;
        Matrix4 billboardMatrixOriginal;
    };
}