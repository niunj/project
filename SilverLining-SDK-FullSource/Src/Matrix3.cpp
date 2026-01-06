// Copyright (c) 2004-2015  Sundog Software, LLC. All rights reserved worldwide.

#include "Matrix3.h"

using namespace SilverLining;

void Matrix3::FromAxisAngle(const Vector3& u, double theta)
{
    double sinTheta = sin(theta);
    double cosTheta = cos(theta);

    elem[0][0] = cosTheta + u.x*u.x*(1.0 - cosTheta);
    elem[0][1] = u.y*u.x*(1.0 - cosTheta) + u.z * sinTheta;
    elem[0][2] = u.z*u.x*(1.0 - cosTheta) + u.y * sinTheta;
    elem[1][0] = u.x*u.y*(1.0 - cosTheta) - u.z * sinTheta;
    elem[1][1] = cosTheta + u.y*u.y*(1.0 - cosTheta);
    elem[1][2] = u.z*u.y*(1.0 - cosTheta) + u.x * sinTheta;
    elem[2][0] = u.x*u.z*(1.0 - cosTheta) + u.y * sinTheta;
    elem[2][1] = u.y*u.z*(1.0 - cosTheta) - u.x * sinTheta;
    elem[2][2] = cosTheta + u.z*u.z*(1.0 - cosTheta);
}

void Matrix3::FromRx(double rad)
{
    double sinr = sin(-rad);
    double cosr = cos(-rad);

    elem[0][0] = 1.0;
    elem[0][1] = 0;
    elem[0][2] = 0;
    elem[1][0] = 0;
    elem[1][1] = cosr;
    elem[1][2] = sinr;
    elem[2][0] = 0;
    elem[2][1] = -sinr;
    elem[2][2] = cosr;
}

void Matrix3::FromRy(double rad)
{
    double sinr = sin(-rad);
    double cosr = cos(-rad);

    elem[0][0] = cosr;
    elem[0][1] = 0;
    elem[0][2] = -sinr;
    elem[1][0] = 0;
    elem[1][1] = 1.0;
    elem[1][2] = 0;
    elem[2][0] = sinr;
    elem[2][1] = 0;
    elem[2][2] = cosr;
}

void Matrix3::FromRz(double rad)
{
    double sinr = sin(-rad);
    double cosr = cos(-rad);

    elem[0][0] = cosr;
    elem[0][1] = sinr;
    elem[0][2] = 0;
    elem[1][0] = -sinr;
    elem[1][1] = cosr;
    elem[1][2] = 0;
    elem[2][0] = 0;
    elem[2][1] = 0;
    elem[2][2] = 1;
}

void Matrix3::FromXYZ(double Rx, double Ry, double Rz)
{
    Matrix3 rx, ry, rz;
    rx.FromRx(Rx);
    ry.FromRy(Ry);
    rz.FromRz(Rz);

    *this = rx * (ry * rz);
}

Vector3 Matrix3::operator* (const Vector3& rkPoint) const
{
    Vector3 kProd;
    kProd.x =
        elem[0][0]*rkPoint.x +
        elem[0][1]*rkPoint.y +
        elem[0][2]*rkPoint.z;

    kProd.y =
        elem[1][0]*rkPoint.x +
        elem[1][1]*rkPoint.y +
        elem[1][2]*rkPoint.z;

    kProd.z =
        elem[2][0]*rkPoint.x +
        elem[2][1]*rkPoint.y +
        elem[2][2]*rkPoint.z;

    return kProd;
}

Matrix3 Matrix3::operator * (const Matrix3 &mat)
{
    Matrix3 out;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            out.elem[row][col] =
                elem[row][0] * mat.elem[0][col] +
                elem[row][1] * mat.elem[1][col] +
                elem[row][2] * mat.elem[2][col];
        }
    }

    return out;
}

Matrix3 Matrix3::Transpose() const
{
    Matrix3 out;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            out.elem[row][col] = elem[col][row];
        }
    }

    return out;
}

Matrix3 Matrix3::Inverse() const
{
    Matrix3 out;

    Matrix3 in = Transpose();

    double det=in.elem[0][0]*(in.elem[1][1]*in.elem[2][2]-in.elem[2][1]*in.elem[1][2])-in.elem[0][1]*(in.elem[1][0]*in.elem[2][2]-in.elem[1][2]*in.elem[2][0])
               +in.elem[0][2]*(in.elem[1][0]*in.elem[2][1]-in.elem[1][1]*in.elem[2][0]);

    out.elem[0][0]=(in.elem[1][1]*in.elem[2][2]-in.elem[2][1]*in.elem[1][2])/det;
    out.elem[0][1]=-(in.elem[1][0]*in.elem[2][2]-in.elem[1][2]*in.elem[2][0])/det;
    out.elem[0][2]=(in.elem[1][0]*in.elem[2][1]-in.elem[2][0]*in.elem[1][1])/det;
    out.elem[1][0]=-(in.elem[0][1]*in.elem[2][2]-in.elem[0][2]*in.elem[2][1])/det;
    out.elem[1][1]=(in.elem[0][0]*in.elem[2][2]-in.elem[0][2]*in.elem[2][0])/det;
    out.elem[1][2]=-(in.elem[0][0]*in.elem[2][1]-in.elem[2][0]*in.elem[0][1])/det;
    out.elem[2][0]=(in.elem[0][1]*in.elem[1][2]-in.elem[0][2]*in.elem[1][1])/det;
    out.elem[2][1]=-(in.elem[0][0]*in.elem[1][2]-in.elem[1][0]*in.elem[0][2])/det;
    out.elem[2][2]=(in.elem[0][0]*in.elem[1][1]-in.elem[1][0]*in.elem[0][1])/det;

    return out;
}

void Matrix3::SetIdentity(void)
{
    for (int m = 0; m < 3; m++) {
        for (int n = 0; n < 3; n++) {
            if (n == m)
                elem[m][n] = 1.0;
            else
                elem[m][n] = 0.0;
        }
    }
}

namespace SilverLining
{
Vector3 operator* (const Vector3& rkPoint, const Matrix3& rkMatrix)
{
    Vector3 kProd;

    kProd.x = rkPoint.x * rkMatrix.elem[0][0] +
              rkPoint.y * rkMatrix.elem[1][0] +
              rkPoint.z * rkMatrix.elem[2][0];

    kProd.y = rkPoint.x * rkMatrix.elem[0][1] +
              rkPoint.y * rkMatrix.elem[1][1] +
              rkPoint.z * rkMatrix.elem[2][1];

    kProd.z = rkPoint.x * rkMatrix.elem[0][2] +
              rkPoint.y * rkMatrix.elem[1][2] +
              rkPoint.z * rkMatrix.elem[2][2];

    return kProd;
}


bool Matrix3::operator == (const Matrix3& rhs) const
{
    return memcmp(elem, rhs.elem, 9 * sizeof(double)) == 0;
}

bool Matrix3::operator != (const Matrix3& rhs) const
{
    return !this->operator==(rhs);
}

}
