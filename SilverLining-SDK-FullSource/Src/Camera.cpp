// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#include "Camera.h"
#include "MatrixUtils.h"
#include "MathUtils.h"
#include "Configuration.h"
#include "Renderer.h"

namespace SilverLining
{
Camera::Camera(bool _rightHanded)
    : hasCameraMatrix(false)
    , hasProjectionMatrix(false)
    , hasViewport(false)
    , rightHanded(_rightHanded)
    , upVector(0, 0, 0)
    , rightVector(0, 0, 0)
    , needsFrustum(true)
    , target(0)
    , hasDepthRange(false)
    , nearDepth(0.0f)
    , farDepth(1.0f)
{
    SetUpVector(Vector3(0, 1, 0));
    SetRightVector(Vector3(1, 0, 0));

    useNDC = true;
    Configuration::GetBoolValue("billboard-use-ndc", useNDC);

    screenBlendFactor = 0.3;
    Configuration::GetDoubleValue("billboard-world-screen-blend-factor", screenBlendFactor);

    billboardMatrixOutOfDate = true;

    worldFrustum.InitializePlanesToZero();
}

Camera::Camera(const Camera* camera, bool copyXForms, bool _needsFrustum)
    : hasCameraMatrix(false)
    , hasProjectionMatrix(false)
    , hasViewport(false)
    , rightHanded(camera->IsRightHanded())
    , upVector(0, 0, 0)
    , rightVector(0, 0, 0)
    , needsFrustum(_needsFrustum)
{
    SetUpVector(camera->GetUpVector());
    SetRightVector(camera->GetRightVector());
    if (copyXForms) {
        SetModelViewMatrix(camera->GetModelViewMatrix());
        SetProjectionMatrix(camera->GetProjectionMatrix());
    }

    billboardMatrixOutOfDate = true;

    worldFrustum.InitializePlanesToZero();
}

Camera::~Camera()
{

}

void Camera::SetModelViewMatrix(const Matrix4& _modelViewMatrix)
{
    modelViewMatrix = _modelViewMatrix;
    modelViewProjectionMatrix = projectionMatrix * modelViewMatrix;
    modelViewMatrix.ToFloatArray(fModelViewMatrix);
    modelViewProjectionMatrix.ToFloatArray(fModelViewProjectionMatrix);
    hasCameraMatrix = true;
    UpdateFrustum();
    UpdateCameraPosition();
    billboardMatrixOutOfDate = true;
}

void Camera::SetProjectionMatrix(const Matrix4& _projectionMatrix)
{
    if (memcmp(&projectionMatrix, &_projectionMatrix, sizeof(Matrix4)) != 0) {
        projectionMatrix = _projectionMatrix;
        modelViewProjectionMatrix = projectionMatrix * modelViewMatrix;
        projectionMatrix.ToFloatArray(fProjectionMatrix);
        modelViewProjectionMatrix.ToFloatArray(fModelViewProjectionMatrix);
        hasProjectionMatrix = true;
        UpdateFrustum();
        billboardMatrixOutOfDate = true;
    }
}

void Camera::SetViewport(int x, int y, int w, int h)
{
    viewport[0] = x;
    viewport[1] = y;
    viewport[2] = w;
    viewport[3] = h;

    hasViewport = true;
}

const Matrix4& Camera::GetModelViewMatrix() const
{
    return modelViewMatrix;
}

const Matrix4& Camera::GetProjectionMatrix() const
{
    return projectionMatrix;
}

const Matrix4& Camera::GetModelViewProjectionMatrix() const
{
    return modelViewProjectionMatrix;
}

bool Camera::GetViewport(int _viewport[4]) const
{
    if (hasViewport) {
        _viewport[0] = viewport[0];
        _viewport[1] = viewport[1];
        _viewport[2] = viewport[2];
        _viewport[3] = viewport[3];
        return true;
    } else {
        return false;
    }
}

bool Camera::GetViewport(int& x, int& y, int& w, int& h) const
{
    if (hasViewport) {
        x = viewport[0];
        y = viewport[1];
        w = viewport[2];
        h = viewport[3];
        return true;
    } else {
        x = y = w = h = -1;
        return false;
    }
}

bool Camera::HasCameraMatrix(void) const
{
    return  hasCameraMatrix;
}
bool Camera::HasProjectionMatrix(void) const
{
    return hasProjectionMatrix;
}

bool Camera::HasViewPort(void) const
{
    return hasViewport;
}

void Camera::SetUpVector(const Vector3& _upVector)
{
    if (upVector == _upVector) {
        return;
    }
    upVector = _upVector;
    UpdateBasis();
    billboardMatrixOutOfDate = true;
}
void Camera::SetRightVector(const Vector3& _rightVector)
{
    if (rightVector == _rightVector) {
        return;
    }
    rightVector = _rightVector;
    UpdateBasis();
    billboardMatrixOutOfDate = true;
}

void Camera::SetUpVector(double x, double y, double z)
{
    Vector3 up(x, y, z);
    up.Normalize();
    SetUpVector(up);
}

void Camera::SetRightVector(double x, double y, double z)
{
    Vector3 rt(x, y, z);
    rt.Normalize();
    SetRightVector(rt);
}

const Vector3& Camera::GetUpVector(void) const
{
    return upVector;
}
const Vector3& Camera::GetRightVector(void) const
{
    return rightVector;
}

void Camera::UpdateBasis()
{
    Vector3 zAxis, yAxis, xAxis;

    MatrixUtils::GetXYZ(xAxis, yAxis, zAxis, upVector, rightVector, rightHanded);

    MatrixUtils::GetBases(basis, invBasis, xAxis, yAxis, zAxis);
    MatrixUtils::GetBases(basis3, invBasis3, xAxis, yAxis, zAxis);

    basis.ToFloatArray(fBasis);
}

static Frustum GetFrustum(const Matrix4& m)
{
    Vector3 m0(m[0][0], m[0][1], m[0][2]);
    Vector3 m1(m[1][0], m[1][1], m[1][2]);
    Vector3 m2(m[2][0], m[2][1], m[2][2]);
    Vector3 m3(m[3][0], m[3][1], m[3][2]);

    Vector3 n;
    double d;

    n = m3 + m0;
    d = m(3, 3) + m(0, 3);
    Plane left(n, d);

    n = m3 - m0;
    d = m(3, 3) - m(0, 3);
    Plane right(n, d);

    n = m3 + m1;
    d = m(3, 3) + m(1, 3);
    Plane bottom(n, d);

    n = m3 - m1;
    d = m(3, 3) - m(1, 3);
    Plane top(n, d);

    n = m3 + m2;
    d = m(3, 3) + m(2, 3);
    Plane pnear(n, d);

    n = m3 - m2;
    d = m(3, 3) - m(2, 3);
    Plane pfar(n, d);

    pnear.Normalize();
    pfar.Normalize();
    left.Normalize();
    right.Normalize();
    top.Normalize();
    bottom.Normalize();

    Frustum frustum;
    frustum.SetPlane(Frustum::PNEAR, pnear);
    frustum.SetPlane(Frustum::PBACK, pfar);
    frustum.SetPlane(Frustum::PLEFT, left);
    frustum.SetPlane(Frustum::PRIGHT, right);
    frustum.SetPlane(Frustum::PTOP, top);
    frustum.SetPlane(Frustum::PBOTTOM, bottom);

    return frustum;
}

void Camera::UpdateFrustum()
{
    if (needsFrustum) {
        worldFrustum = GetFrustum(modelViewProjectionMatrix);
    }
}

const Frustum& Camera::GetFrustumWorldSpace(void) const
{
    return worldFrustum;
}

void Camera::UpdateCameraPosition()
{
    Vector3 U(modelViewMatrix.elem[0][0], modelViewMatrix.elem[0][1], modelViewMatrix.elem[0][2]);
    Vector3 V(modelViewMatrix.elem[1][0], modelViewMatrix.elem[1][1], modelViewMatrix.elem[1][2]);
    Vector3 N(modelViewMatrix.elem[2][0], modelViewMatrix.elem[2][1], modelViewMatrix.elem[2][2]);

    Vector4 R(-modelViewMatrix.elem[0][3], -modelViewMatrix.elem[1][3], -modelViewMatrix.elem[2][3], 1);

    Matrix4 M1(U.x, V.x, N.x, 0
               , U.y, V.y, N.y, 0
               , U.z, V.z, N.z, 0
               , 0, 0, 0, 1);

    Vector4 camPos4 = M1 * R;
    cameraPosition = Vector3(camPos4.x, camPos4.y, camPos4.z);
}

const Vector3& Camera::GetPosition(void) const
{
    return cameraPosition;
}
double Camera::GetAltitude(void) const
{
    return (GetPosition()*invBasis3).y;
}
/** Returns a matrix4 defining the basis based on the current "up vector." */
const Matrix4& Camera::GetBasis4x4() const
{
    return basis;
}

/** Returns a matrix4 defining the inverse basis based on the current "up vector." */
const Matrix4& Camera::GetInverseBasis4x4() const
{
    return invBasis;
}

/** Returns a matrix4 defining the basis based on the current "up vector." */
const Matrix3& Camera::GetBasis3x3() const
{
    return basis3;
}

/** Returns a matrix4 defining the inverse basis based on the current "up vector." */
const Matrix3& Camera::GetInverseBasis3x3() const
{
    return invBasis3;
}

bool Camera::IsRightHanded(void) const
{
    return rightHanded;
}

Frustum Camera::GetFrustumClipSpace() const
{
    return GetFrustum(projectionMatrix);
}

void Camera::GetNDCDepthRange(float& zdmin, float& zdmax, const Renderer *renderer) const
{
    if (renderer->GetIsOpenGL()) {
        zdmax = 1.0;
        zdmin = -1.0;
    } else {
        zdmax = 1.0;
        zdmin = 0.0;
    }
}

void Camera::SetProjectionMatrix(double l, double r, double t, double b, double zn, double zf
                                 , float minZ, float maxZ, const Renderer* renderer)
{
    Matrix4 proj;

    float zdmin, zdmax;
    GetNDCDepthRange(zdmin, zdmax, renderer);
    double zScale = zdmax - zdmin;// isDX ? 1.0 : 2.0;

    if (rightHanded) {
        proj.elem[0][0] = (2.0*zn) / (r - l);
        proj.elem[0][1] = 0;
        proj.elem[0][2] = (r + l) / (r - l);
        proj.elem[0][3] = 0;
        proj.elem[1][0] = 0;
        proj.elem[1][1] = (2.0*zn) / (t - b);
        proj.elem[1][2] = (t + b) / (t - b);
        proj.elem[1][3] = 0;
        proj.elem[2][0] = 0;
        proj.elem[2][1] = 0;
        proj.elem[2][2] = -((zf + zn) / (zf - zn));
        proj.elem[2][3] = -((zScale*zn*zf) / (zf - zn));
        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = -1;
        proj.elem[3][3] = 0;
    } else {
        proj.elem[0][0] = (2.0*zn) / (r - l);
        proj.elem[0][1] = 0;
        proj.elem[0][2] = 0;
        proj.elem[0][3] = 0;
        proj.elem[1][0] = 0;
        proj.elem[1][1] = (2.0*zn) / (t - b);
        proj.elem[1][2] = 0;
        proj.elem[1][3] = 0;
        proj.elem[2][0] = (l + r) / (l - r);
        proj.elem[2][1] = (t + b) / (b - t);
        proj.elem[2][2] = zf / (zf - zn);
        proj.elem[2][3] = 1;
        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = (zScale*zn*zf) / (zn - zf);
        proj.elem[3][3] = 0;

        proj.Transpose();
    }

    SetProjectionMatrix(proj);
}

void Camera::AdjustNearFarClip(double znear, double zfar, bool noSkew, float minZ, float maxZ, const Renderer *renderer)
{
    Frustum f = GetFrustumClipSpace();

    double l, r, t, b;
    Vector3 pn = f.GetPlane(Frustum::PLEFT).GetNormal();
    l = (pn.z * znear) / pn.x;
    pn = f.GetPlane(Frustum::PRIGHT).GetNormal();
    r = (pn.z * znear) / pn.x;
    pn = f.GetPlane(Frustum::PTOP).GetNormal();
    t = (pn.z * znear) / pn.y;
    pn = f.GetPlane(Frustum::PBOTTOM).GetNormal();
    b = (pn.z * znear) / pn.y;

    if (noSkew) {
        double w = r - l;
        l = -w * 0.5;
        r = w * 0.5;
        double h = t - b;
        b = -h * 0.5;
        t = h * 0.5;
    }

    if (!rightHanded) {
        l = -l;
        r = -r;
        t = -t;
        b = -b;
    }

    SetProjectionMatrix(l, r, t, b, znear, zfar, minZ, maxZ, renderer);
}

Matrix4 Camera::GetProjectionNoSkew(double znear, double zfar, float minZ, float maxZ, const Renderer *renderer) const
{
    Frustum f = GetFrustumClipSpace();

    double l, r, t, b;
    Vector3 pn = f.GetPlane(Frustum::PLEFT).GetNormal();
    l = (pn.z * znear) / pn.x;
    pn = f.GetPlane(Frustum::PRIGHT).GetNormal();
    r = (pn.z * znear) / pn.x;
    pn = f.GetPlane(Frustum::PTOP).GetNormal();
    t = (pn.z * znear) / pn.y;
    pn = f.GetPlane(Frustum::PBOTTOM).GetNormal();
    b = (pn.z * znear) / pn.y;

    double w = r - l;
    l = -w * 0.5;
    r = w * 0.5;
    double h = t - b;
    b = -h * 0.5;
    t = h * 0.5;

    if (!rightHanded) {
        l = -l;
        r = -r;
        t = -t;
        b = -b;
    }

    Matrix4 proj;

    float zdmin, zdmax;
    GetNDCDepthRange(zdmin, zdmax, renderer);
    double zScale = zdmax - zdmin;// isDX ? 1.0 : 2.0;

    if (rightHanded) {
        proj.elem[0][0] = (2.0*znear) / (r - l);
        proj.elem[0][1] = 0;
        proj.elem[0][2] = (r + l) / (r - l);
        proj.elem[0][3] = 0;
        proj.elem[1][0] = 0;
        proj.elem[1][1] = (2.0*znear) / (t - b);
        proj.elem[1][2] = (t + b) / (t - b);
        proj.elem[1][3] = 0;
        proj.elem[2][0] = 0;
        proj.elem[2][1] = 0;
        proj.elem[2][2] = -((zfar + znear) / (zfar - znear));
        proj.elem[2][3] = -((zScale*znear*zfar) / (zfar - znear));
        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = -1;
        proj.elem[3][3] = 0;
    } else {
        proj.elem[0][0] = (2.0*znear) / (r - l);
        proj.elem[0][1] = 0;
        proj.elem[0][2] = 0;
        proj.elem[0][3] = 0;
        proj.elem[1][0] = 0;
        proj.elem[1][1] = (2.0*znear) / (t - b);
        proj.elem[1][2] = 0;
        proj.elem[1][3] = 0;
        proj.elem[2][0] = (l + r) / (l - r);
        proj.elem[2][1] = (t + b) / (b - t);
        proj.elem[2][2] = zfar / (zfar - znear);
        proj.elem[2][3] = 1;
        proj.elem[3][0] = 0;
        proj.elem[3][1] = 0;
        proj.elem[3][2] = (zScale*znear*zfar) / (znear - zfar);
        proj.elem[3][3] = 0;

        proj.Transpose();
    }

    return proj;
}

void Camera::SetModelviewLookat(const Vector3& eye, const Vector3& at, const Vector3& up)
{
    Vector3 view;

    if (rightHanded)
        view = eye - at;
    else
        view = at - eye;

    view.Normalize();

    Vector3 right = up.Cross(view);
    right.Normalize();

    Vector3 vup = view.Cross(right);

    Matrix4 m;
    m.elem[0][0] = right.x;
    m.elem[0][1] = right.y;
    m.elem[0][2] = right.z;
    m.elem[0][3] = -right.Dot(eye);
    m.elem[1][0] = vup.x;
    m.elem[1][1] = vup.y;
    m.elem[1][2] = vup.z;
    m.elem[1][3] = -vup.Dot(eye);
    m.elem[2][0] = view.x;
    m.elem[2][1] = view.y;
    m.elem[2][2] = view.z;

    m.elem[2][3] = -view.Dot(eye);

    SetModelViewMatrix(m);
}

void Camera::SetProjectionMatrix(double nearClip, double farClip, double fovx, double fovy, float minZ, float maxZ, const Renderer *renderer)
{
    double l, r, t, b, n, f;

    n = nearClip;
    f = farClip;

    double halfAngle = fovx * 0.5;
    r = n * tan(halfAngle);
    l = -r;
    halfAngle = fovy * 0.5;
    t = n * tan(halfAngle);
    b = -t;

    SetProjectionMatrix(l, r, t, b, n, f, minZ, maxZ, renderer);
}

void Camera::GetNearFarClip(double &znear, double &zfar) const
{
    Frustum frustum = GetFrustumClipSpace();
    zfar = frustum.GetPlane(Frustum::PBACK).GetDistance();
    znear = frustum.GetPlane(Frustum::PNEAR).GetDistance() * -1.0;
}

void Camera::MultiplyModelViewMatrix(const Matrix4& m)
{
    SetModelViewMatrix(GetModelViewMatrix()*m);
}

const Matrix4& Camera::GetBillboardMatrix(void) const
{
    if (billboardMatrixOutOfDate) {
        UpdateBillboardMatrix();
    }
    return billboard;
}

void Camera::UpdateBillboardMatrix(void) const
{
    // Figure out the world-oriented billboarding matrix.
    Vector3 Up = GetUpVector();
    Vector3 Normal, camRight;

    if (useNDC) {
        // Vector pointing away from the eyepoint into the screen
        Vector3 ndcIn(0, 0, 1);
        // de-project to world space
        Matrix4 view = modelViewMatrix;
        view.elem[0][3] = view.elem[1][3] = view.elem[2][3] = 0;
        Matrix4 mvp = projectionMatrix * view;
        Matrix4 invMvp = mvp.Inverse();
        Normal = invMvp * ndcIn;
        Normal.Normalize();
        Normal = Normal * -1.0;

        // For some reason this causes NaN's
        //Vector3 ndcRight(1, 0, 0);
        //camRight = invMvp * ndcRight;
        //camRight.Normalize();
    } else {
        Normal = Vector3(modelViewMatrix.elem[2][0], modelViewMatrix.elem[2][1], modelViewMatrix.elem[2][2]);
        //camRight = Vector3(modelViewMatrix.elem[0][0], modelViewMatrix.elem[0][1], modelViewMatrix.elem[0][2]);
    }

    // Things get wonky when the normal vector approaches the fixed world up vector.
    // Blend towards the camera's "up" vector when this happens.

    camRight = Vector3(modelViewMatrix.elem[0][0], modelViewMatrix.elem[0][1], modelViewMatrix.elem[0][2]);

    Vector3 altUp = Normal.Cross(camRight);
    altUp.Normalize();

    double screenBlend = MathUtils::Abs(Up.Dot(Normal));

    screenBlend *= screenBlendFactor;

    Up = (altUp * screenBlend) + (Up * (1.0 - screenBlend));

    Vector3 Right = Up.Cross(Normal);
    Right.Normalize();
    Up = Normal.Cross(Right);
    Up.Normalize();
    //Normal = Right.Cross(Up);
    //Normal.Normalize();

    if (rightHanded) {
        billboard = Matrix4(Right.x, Up.x, Normal.x, 0,
                            Right.y, Up.y, Normal.y, 0,
                            Right.z, Up.z, Normal.z, 0,
                            0, 0, 0, 1);
    } else {
        billboard = Matrix4(Right.x, Up.x, -Normal.x, 0,
                            Right.y, Up.y, -Normal.y, 0,
                            Right.z, Up.z, -Normal.z, 0,
                            0, 0, 0, 1);
    }
}

void Camera::SetName(const char* _name)
{
    name = SL_STRING(_name);
}
const char* Camera::GetName(void) const
{
    return name.c_str();
}
bool Camera::operator == (const Camera& rhs) const
{
    if (IsRightHanded() != rhs.IsRightHanded()) {
        return false;
    }
    if (GetUpVector() != rhs.GetUpVector()) {
        return false;
    }
    if (GetRightVector() != rhs.GetRightVector()) {
        return false;
    }
    if (GetProjectionMatrix() != rhs.GetProjectionMatrix()) {
        return false;
    }
    if (GetModelViewMatrix() != rhs.GetModelViewMatrix()) {
        return false;
    }
    if (GetPosition() != rhs.GetPosition()) {
        return false;
    }
    return true;
}
bool Camera::operator != (const Camera& rhs) const
{
    return !(*this == rhs);
}

bool Camera::GetDepthRange(float& zmin, float& zmax) const
{
    if (hasDepthRange) {
        zmin = nearDepth;
        zmax = farDepth;
        return true;
    } else {
        return false;
    }
}

bool Camera::SetDepthRange(float zmin, float zmax)
{
    nearDepth = zmin;
    farDepth = zmax;

    hasDepthRange = true;

    return true;
}

bool Camera::HasDepthRange(void) const
{
    return hasDepthRange;
}
double Camera::GetFieldOfView(void) const
{
    if (hasProjectionMatrix) {
        double h = projectionMatrix[1][1];

        if (h < 0.01) h = 0.01;

        return 2.0 * atan(1.0 / h);
    }
    return 0;
}
}