// Copyright (c) 2004-2015  Sundog Software, LLC. All rights reserved worldwide.

/**
    \file Matrix3.h
    \brief Implements a 3x3 matrix and its operations.
 */

#ifndef MATRIX3_H
#define MATRIX3_H

#include "MemAlloc.h"
#include "Vector3.h"
#include <cstring>

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** A simple 3x3 matrix class and its operations. */
class Matrix3 : public MemObject
{
public:
/** Default contructor; performs no initialization for efficiency. */
    Matrix3() {
    }

/** Constructor that instantiates the 3x3 matrix with initial values. */
    Matrix3(double e11, double e12, double e13,
            double e21, double e22, double e23,
            double e31, double e32, double e33) {
        elem[0][0] = e11;
        elem[0][1] = e12;
        elem[0][2] = e13;
        elem[1][0] = e21;
        elem[1][1] = e22, elem[1][2] = e23;
        elem[2][0] = e31, elem[2][1] = e32, elem[2][2] = e33;
    }

/** Destructor. */
    ~Matrix3() {
    }

/** equality operator */
    bool operator == (const Matrix3& rhs) const;

/** in-equality operator */
    bool operator != (const Matrix3& rhs) const;

/** Populates the matrix to model a rotation about the X axis by a given
   amount, in radians. */
    void SILVERLINING_API FromRx(double rad);

/** Populates the matrix to model a rotation about the Y axis by a given
   amount, in radians. */
    void SILVERLINING_API FromRy(double rad);

/** Populates the matrix to model a rotation about the Z axis by a given
   amount, in radians. */
    void SILVERLINING_API FromRz(double rad);

/** Populates the matrix as a series of rotations about the X, Y, and Z
   axes (in that order) by specified amounts in radians. */
    void SILVERLINING_API FromXYZ(double Rx, double Ry, double Rz);

/** Create a rotation matrix from axis and angle. */
	void SILVERLINING_API FromAxisAngle(const Vector3& axis, double angle);

/** Multiplies two matrices together. */
    Matrix3 SILVERLINING_API operator * (const Matrix3& mat);

/** Multiplies the matrix by a vector, yielding another 3x1 vector. */
    Vector3 SILVERLINING_API operator* (const Vector3& rkVector) const;

/** Multiplies a 1x3 vector by a matrix, yielding a 1x3 vector. */
    friend Vector3 SILVERLINING_API operator * (const Vector3& vec, const Matrix3& mat);

/** Calculate the transpose of the matrix. */
    Matrix3 SILVERLINING_API Transpose() const;

/** Calculate the inverse of the matrix. */
    Matrix3 SILVERLINING_API Inverse() const;

/** Set to identity matrix. */
	void SILVERLINING_API SetIdentity(void);

/// The data members are public for convenience.
    double elem[3][3];
};


inline std::ostream& operator <<
(std::ostream& o, const Matrix3& mat)
{
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            o << mat.elem[i][j] << " ";
        }
        o << std::endl;
    }
    return o;
}
}

#pragma pack(pop)

#endif
