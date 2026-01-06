// Copyright (c) 2004-2008  Sundog Software, LLC All rights reserved worldwide.

#include "Matrix4.h"

using namespace SilverLining;

Matrix4 Matrix4::operator * (const Matrix4 &mat) const
{
    Matrix4 out;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            out.elem[row][col] =
                elem[row][0] * mat.elem[0][col] +
                elem[row][1] * mat.elem[1][col] +
                elem[row][2] * mat.elem[2][col] +
                elem[row][3] * mat.elem[3][col];
        }
    }

    return out;
}

Vector4 Matrix4::operator * (const Vector4& v) const
{
    Vector4 out;

    out.x = elem[0][0] * v.x + elem[0][1] * v.y + elem[0][2] * v.z + elem[0][3] * v.w;
    out.y = elem[1][0] * v.x + elem[1][1] * v.y + elem[1][2] * v.z + elem[1][3] * v.w;
    out.z = elem[2][0] * v.x + elem[2][1] * v.y + elem[2][2] * v.z + elem[2][3] * v.w;
    out.w = elem[3][0] * v.x + elem[3][1] * v.y + elem[3][2] * v.z + elem[3][3] * v.w;

    return out;
}

Vector3 Matrix4::operator * (const Vector3& v) const
{
    Vector4 out;

    out.x = elem[0][0] * v.x + elem[0][1] * v.y + elem[0][2] * v.z + elem[0][3];
    out.y = elem[1][0] * v.x + elem[1][1] * v.y + elem[1][2] * v.z + elem[1][3];
    out.z = elem[2][0] * v.x + elem[2][1] * v.y + elem[2][2] * v.z + elem[2][3];
    out.w = elem[3][0] * v.x + elem[3][1] * v.y + elem[3][2] * v.z + elem[3][3];

    Vector3 out3;
    out3.x = out.x / out.w;
    out3.y = out.y / out.w;
    out3.z = out.z / out.w;

    return out3;
}

namespace SilverLining
{
Vector4 operator* (const Vector4& rkPoint, const Matrix4& rkMatrix)
{
    Vector4 kProd;

    kProd.x = rkPoint.x * rkMatrix.elem[0][0] +
              rkPoint.y * rkMatrix.elem[1][0] +
              rkPoint.z * rkMatrix.elem[2][0] +
              rkPoint.w * rkMatrix.elem[3][0];

    kProd.y = rkPoint.x * rkMatrix.elem[0][1] +
              rkPoint.y * rkMatrix.elem[1][1] +
              rkPoint.z * rkMatrix.elem[2][1] +
              rkPoint.w * rkMatrix.elem[3][1];

    kProd.z = rkPoint.x * rkMatrix.elem[0][2] +
              rkPoint.y * rkMatrix.elem[1][2] +
              rkPoint.z * rkMatrix.elem[2][2] +
              rkPoint.w * rkMatrix.elem[3][2];

    kProd.w = rkPoint.x * rkMatrix.elem[0][3] +
              rkPoint.y * rkMatrix.elem[1][3] +
              rkPoint.z * rkMatrix.elem[2][3] +
              rkPoint.w * rkMatrix.elem[3][3];

    return kProd;
}

const double* Matrix4::operator[](int row) const
{
    return elem[row];
}

double Matrix4::operator()(int row, int col) const
{
    return elem[row][col];
}

}
