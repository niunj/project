#include "MatrixUtils.h"
#include <cmath>


void BuildPerspProjMat(double *m, double fov, double aspect, double znear, double zfar)
{
    double ymax = znear * tan(fov * (3.14159265 / 360.0));
    double ymin = -ymax;
    double xmax = ymax * aspect;
    double xmin = ymin * aspect;
    double width = xmax - xmin;
    double height = ymax - ymin;
    double depth = zfar - znear;
    double q = -(zfar + znear) / depth;
    double qn = -2 * (zfar * znear) / depth;
    double w = 2 * znear / width;
    double h = 2 * znear / height;
    m[0] = w;
    m[1] = 0;
    m[2] = 0;
    m[3] = 0;
    m[4] = 0;
    m[5] = h;
    m[6] = 0;
    m[7] = 0;
    m[8] = 0;
    m[9] = 0;
    m[10] = q;
    m[11] = -1;
    m[12] = 0;
    m[13] = 0;
    m[14] = qn;
    m[15] = 0;
}

void Rotate(double m[16], double angle, double x, double y, double z)
{
    double(*mat)[4] = (double(*)[4]) (m);

    double c = cos(angle);
    double s = sin(angle);

    mat[0][0] = x*x*(1 - c) + c;
    mat[0][1] = x*y*(1 - c) - (z*s);
    mat[0][2] = x*z*(1 - c) + y*s;
    mat[0][3] = 0;

    mat[1][0] = y*x*(1 - c) + z*s;
    mat[1][1] = y*y*(1 - c) + c;
    mat[1][2] = y*z*(1 - c) - x*s;
    mat[1][3] = 0;

    mat[2][0] = x*z*(1 - c) - y*s;
    mat[2][1] = y*z*(1 - c) + (x*s);
    mat[2][2] = z*z*(1 - c) + c;
    mat[2][3] = 0;

    mat[3][0] = 0;
    mat[3][1] = 0;
    mat[3][2] = 0;
    mat[3][3] = 1;

}

void Translate(double *m, double x, double y, double z)
{
    m[12] = m[0] * x + m[4] * y + m[8] * z + m[12];
    m[13] = m[1] * x + m[5] * y + m[9] * z + m[13];
    m[14] = m[2] * x + m[6] * y + m[10] * z + m[14];
    m[15] = m[3] * x + m[7] * y + m[11] * z + m[15];
}

void MatrixMult(double *resultMat, double* lhsMat, double* rhsMat)
{
    double(*result)[4] = (double(*)[4]) (resultMat);
    double(*lhs)[4] = (double(*)[4]) (lhsMat);
    double(*rhs)[4] = (double(*)[4]) (rhsMat);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            result[row][col] =
                lhs[row][0] * rhs[0][col] +
                lhs[row][1] * rhs[1][col] +
                lhs[row][2] * rhs[2][col] +
                lhs[row][3] * rhs[3][col];
        }
    }
}