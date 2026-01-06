#pragma once



namespace SilverLining
{
    class Matrix4;
    class Matrix3;
    class Vector3;
    class MatrixUtils
    {
    public:
        static void GetXYZ(Vector3& xAxis, Vector3& yAxis, Vector3& zAxis, const Vector3& upVector, const Vector3& rightVector, bool rightHanded);
        static void GetBases(Matrix4& basis, Matrix4& invBasis, const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);
        static void GetBases(Matrix3& basis3, Matrix3& invBasis3, const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);
    };
}