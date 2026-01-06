// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

#include "OverriddenBillboardMatrix.h"
#include "Camera.h"
#include "SLAssert.h"

namespace SilverLining
{
OverriddenBillboardMatrix::OverriddenBillboardMatrix(const Camera* camera)
    : isSet(false)
{
    SL_ASSERT(camera);
    billboardMatrixOriginal = camera->GetBillboardMatrix();
}

OverriddenBillboardMatrix::OverriddenBillboardMatrix(const Matrix4& matrix)
    : billboardMatrix(matrix)
    , isSet(true)
{

}

const Matrix4& OverriddenBillboardMatrix::GetBillboardMatrix(void) const
{
    if (isSet) {
        return billboardMatrix;
    } else {
        return billboardMatrixOriginal;
    }
}

OverriddenBillboardMatrix& OverriddenBillboardMatrix::operator = (const Matrix4& rhs)
{
    isSet = true;
    billboardMatrix = rhs;

    return *this;
}
}