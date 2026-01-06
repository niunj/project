// Copyright (c) 2004-2015  Sundog Software, LLC All rights reserved worldwide.

/**
    \file Matrix4.h
    \brief An implementation of a 4x4 matrix and some simple operations on it.
 */

#ifndef MATRIX4_H
#define MATRIX4_H

#include "MemAlloc.h"
#include "Vector4.h"
#include "Matrix3.h"
#include <cstring>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** An implementation of a 4x4 matrix and some simple operations on it. */
class Matrix4 : public MemObject
{
public:
/** Default constructor; initializes the matrix to an identity transform. */
    Matrix4()
    {
        elem[0][0] = 1;
        elem[0][1] = 0;
        elem[0][2] = 0;
        elem[0][3] = 0;
        elem[1][0] = 0;
        elem[1][1] = 1;
        elem[1][2] = 0;
        elem[1][3] = 0;
        elem[2][0] = 0;
        elem[2][1] = 0;
        elem[2][2] = 1;
        elem[2][3] = 0;
        elem[3][0] = 0;
        elem[3][1] = 0;
        elem[3][2] = 0;
        elem[3][3] = 1;
    }

/** Construct from a Matrix3. */
	Matrix4(const Matrix3& m3)
	{
		elem[0][0] = m3.elem[0][0];
		elem[0][1] = m3.elem[0][1];
		elem[0][2] = m3.elem[0][2];
		elem[0][3] = 0;
		elem[1][0] = m3.elem[1][0];
		elem[1][1] = m3.elem[1][1];
		elem[1][2] = m3.elem[1][2];
		elem[1][3] = 0;
		elem[2][0] = m3.elem[2][0];
		elem[2][1] = m3.elem[2][1];
		elem[2][2] = m3.elem[2][2];
		elem[2][3] = 0;
		elem[3][0] = 0;
		elem[3][1] = 0;
		elem[3][2] = 0;
		elem[3][3] = 1;
	}

/** This constructor allows you to initialize the matrix as you please. */
    Matrix4(double e11, double e12, double e13, double e14,
            double e21, double e22, double e23, double e24,
            double e31, double e32, double e33, double e34,
            double e41, double e42, double e43, double e44)
    {
        elem[0][0] = e11;
        elem[0][1] = e12;
        elem[0][2] = e13;
        elem[0][3] = e14;
        elem[1][0] = e21;
        elem[1][1] = e22;
        elem[1][2] = e23;
        elem[1][3] = e24;
        elem[2][0] = e31;
        elem[2][1] = e32;
        elem[2][2] = e33;
        elem[2][3] = e34;
        elem[3][0] = e41;
        elem[3][1] = e42;
        elem[3][2] = e43;
        elem[3][3] = e44;
    }

/** Initializes the matrix from an array of 16 double-precision values (row-major). */
    Matrix4(const double *m)
    {
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                elem[row][col] = *m++;
            }
        }
    }

/** Destructor. */
    ~Matrix4() {
    }

    bool operator == (const Matrix4& rhs) const {
        return memcmp(elem, rhs.elem, 16 * sizeof(double))==0;
    }

    bool operator != (const Matrix4& rhs) const {
        return !this->operator==(rhs);
    }

    void ToArray(double m[16]) const
    {
        int idx = 0;
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                m[idx++] = elem[col][row];
            }
        }
    }

	void ToFloatArray(float m[16]) const
	{
		int idx = 0;
		for (int row = 0; row < 4; row++)
		{
			for (int col = 0; col < 4; col++)
			{
				m[idx++] = (float)elem[row][col];
			}
		}
	}

   void SetTranslation(const Vector3& translation)
   {
      elem[0][3] = translation.x;
      elem[1][3] = translation.y;
      elem[2][3] = translation.z;
   }

    const double* SILVERLINING_API operator[](int row) const;
    double operator()(int row, int col) const;

/** Multiplies two matrices together. */
    Matrix4 SILVERLINING_API operator * (const Matrix4& mat) const;

/** Transform a point by the matrix. */
    Vector4 SILVERLINING_API operator * (const Vector4& vec) const;

/** Transform a point by the matrix. */
    Vector3 SILVERLINING_API operator * (const Vector3& vec) const;

/** Multiplies a 1x3 vector by a matrix, yielding a 1x3 vector. */
    friend Vector4 SILVERLINING_API operator * (const Vector4& vec, const Matrix4& mat);

/** Transposes the matrix in-place. */
    void SILVERLINING_API Transpose()
    {
        Matrix4 m = *this;
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                elem[row][col] = m.elem[col][row];
            }
        }
    }

    Matrix4 SILVERLINING_API Inverse()
    {
        const double epsilon = 1E-6;
        Matrix4 inverse;

        double a0 = elem[0][0]*elem[1][1] - elem[0][1]*elem[1][0];
        double a1 = elem[0][0]*elem[1][2] - elem[0][2]*elem[1][0];
        double a2 = elem[0][0]*elem[1][3] - elem[0][3]*elem[1][0];
        double a3 = elem[0][1]*elem[1][2] - elem[0][2]*elem[1][1];
        double a4 = elem[0][1]*elem[1][3] - elem[0][3]*elem[1][1];
        double a5 = elem[0][2]*elem[1][3] - elem[0][3]*elem[1][2];
        double b0 = elem[2][0]*elem[3][1] - elem[2][1]*elem[3][0];
        double b1 = elem[2][0]*elem[3][2] - elem[2][2]*elem[3][0];
        double b2 = elem[2][0]*elem[3][3] - elem[2][3]*elem[3][0];
        double b3 = elem[2][1]*elem[3][2] - elem[2][2]*elem[3][1];
        double b4 = elem[2][1]*elem[3][3] - elem[2][3]*elem[3][1];
        double b5 = elem[2][2]*elem[3][3] - elem[2][3]*elem[3][2];

        double det = a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
        if (fabs(det) > epsilon)
        {
            inverse.elem[0][0] = +elem[1][1]*b5 - elem[1][2]*b4 + elem[1][3]*b3;
            inverse.elem[1][0] = -elem[1][0]*b5 + elem[1][2]*b2 - elem[1][3]*b1;
            inverse.elem[2][0] = +elem[1][0]*b4 - elem[1][1]*b2 + elem[1][3]*b0;
            inverse.elem[3][0] = -elem[1][0]*b3 + elem[1][1]*b1 - elem[1][2]*b0;
            inverse.elem[0][1] = -elem[0][1]*b5 + elem[0][2]*b4 - elem[0][3]*b3;
            inverse.elem[1][1] = +elem[0][0]*b5 - elem[0][2]*b2 + elem[0][3]*b1;
            inverse.elem[2][1] = -elem[0][0]*b4 + elem[0][1]*b2 - elem[0][3]*b0;
            inverse.elem[3][1] = +elem[0][0]*b3 - elem[0][1]*b1 + elem[0][2]*b0;
            inverse.elem[0][2] = +elem[3][1]*a5 - elem[3][2]*a4 + elem[3][3]*a3;
            inverse.elem[1][2] = -elem[3][0]*a5 + elem[3][2]*a2 - elem[3][3]*a1;
            inverse.elem[2][2] = +elem[3][0]*a4 - elem[3][1]*a2 + elem[3][3]*a0;
            inverse.elem[3][2] = -elem[3][0]*a3 + elem[3][1]*a1 - elem[3][2]*a0;
            inverse.elem[0][3] = -elem[2][1]*a5 + elem[2][2]*a4 - elem[2][3]*a3;
            inverse.elem[1][3] = +elem[2][0]*a5 - elem[2][2]*a2 + elem[2][3]*a1;
            inverse.elem[2][3] = -elem[2][0]*a4 + elem[2][1]*a2 - elem[2][3]*a0;
            inverse.elem[3][3] = +elem[2][0]*a3 - elem[2][1]*a1 + elem[2][2]*a0;

            double invDet = ((double)1)/det;
            inverse.elem[0][0] *= invDet;
            inverse.elem[0][1] *= invDet;
            inverse.elem[0][2] *= invDet;
            inverse.elem[0][3] *= invDet;
            inverse.elem[1][0] *= invDet;
            inverse.elem[1][1] *= invDet;
            inverse.elem[1][2] *= invDet;
            inverse.elem[1][3] *= invDet;
            inverse.elem[2][0] *= invDet;
            inverse.elem[2][1] *= invDet;
            inverse.elem[2][2] *= invDet;
            inverse.elem[2][3] *= invDet;
            inverse.elem[3][0] *= invDet;
            inverse.elem[3][1] *= invDet;
            inverse.elem[3][2] *= invDet;
            inverse.elem[3][3] *= invDet;

        }

        return inverse;
    }

/** Retrieves a pointer into the requested row of the matrix. */
    double * SILVERLINING_API GetRow(int row)
    {
        if (row < 4)
        {
            return elem[row];
        }
        else
        {
            return 0;
        }
    }

/// Data members are public for convenience.
    double elem[4][4];
};


inline std::ostream& operator <<
(std::ostream& o, const Matrix4& mat)
{
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            o << mat[i][j] << " ";
        }
        o << std::endl;
    }
    return o;
}

}

#pragma pack(pop)

#endif
