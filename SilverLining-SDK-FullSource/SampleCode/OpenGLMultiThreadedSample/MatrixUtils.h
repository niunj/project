// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

void BuildPerspProjMat(double *m, double fov, double aspect, double znear, double zfar);


template <class T>
void BuildIdentityMat(T *m)
{
    m[0] = 1;
    m[1] = 0;
    m[2] = 0;
    m[3] = 0;
    m[4] = 0;
    m[5] = 1;
    m[6] = 0;
    m[7] = 0;
    m[8] = 0;
    m[9] = 0;
    m[10] = 1;
    m[11] = 0;
    m[12] = 0;
    m[13] = 0;
    m[14] = 0;
    m[15] = 1;
}

void Rotate(double m[16], double angle, double x, double y, double z);
void Translate(double *m, double x, double y, double z);
void MatrixMult(double *resultMat, double* lhsMat, double* rhsMat);


inline double toRadians(double x)
{
    return x * (3.14159265 / 180.0);
}
