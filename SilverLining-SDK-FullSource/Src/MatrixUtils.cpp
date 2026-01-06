#include "MatrixUtils.h"

#include "Vector3.h"
#include "Matrix3.h"
#include "Matrix4.h"

namespace SilverLining
{
void MatrixUtils::GetXYZ(Vector3& xAxis, Vector3& yAxis, Vector3& zAxis, const Vector3& upVector, const Vector3& rightVector, bool rightHanded)
{
    zAxis = rightVector.Cross(upVector);
    zAxis.Normalize();
    xAxis = upVector.Cross(zAxis);
    xAxis.Normalize();
    yAxis = zAxis.Cross(xAxis);
    yAxis.Normalize();

    if (!rightHanded) {
        zAxis = zAxis * -1;
    }
}

void MatrixUtils::GetBases(Matrix4& basis, Matrix4& invBasis, const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis)
{
    basis = Matrix4();
    basis.elem[0][0] = xAxis.x;
    basis.elem[0][1] = xAxis.y;
    basis.elem[0][2] = xAxis.z;
    basis.elem[1][0] = yAxis.x;
    basis.elem[1][1] = yAxis.y;
    basis.elem[1][2] = yAxis.z;
    basis.elem[2][0] = zAxis.x;
    basis.elem[2][1] = zAxis.y;
    basis.elem[2][2] = zAxis.z;

    invBasis = Matrix4();
    invBasis.elem[0][0] = xAxis.x;
    invBasis.elem[0][1] = yAxis.x;
    invBasis.elem[0][2] = zAxis.x;
    invBasis.elem[1][0] = xAxis.y;
    invBasis.elem[1][1] = yAxis.y;
    invBasis.elem[1][2] = zAxis.y;
    invBasis.elem[2][0] = xAxis.z;
    invBasis.elem[2][1] = yAxis.z;
    invBasis.elem[2][2] = zAxis.z;
}

void MatrixUtils::GetBases(Matrix3& basis3, Matrix3& invBasis3, const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis)
{
    basis3 = Matrix3();
    basis3.elem[0][0] = xAxis.x;
    basis3.elem[0][1] = xAxis.y;
    basis3.elem[0][2] = xAxis.z;
    basis3.elem[1][0] = yAxis.x;
    basis3.elem[1][1] = yAxis.y;
    basis3.elem[1][2] = yAxis.z;
    basis3.elem[2][0] = zAxis.x;
    basis3.elem[2][1] = zAxis.y;
    basis3.elem[2][2] = zAxis.z;

    invBasis3 = Matrix3();
    invBasis3.elem[0][0] = xAxis.x;
    invBasis3.elem[0][1] = yAxis.x;
    invBasis3.elem[0][2] = zAxis.x;
    invBasis3.elem[1][0] = xAxis.y;
    invBasis3.elem[1][1] = yAxis.y;
    invBasis3.elem[1][2] = zAxis.y;
    invBasis3.elem[2][0] = xAxis.z;
    invBasis3.elem[2][1] = yAxis.z;
    invBasis3.elem[2][2] = zAxis.z;
}
}
