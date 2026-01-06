// Copyright 2006-2023 Sundog Software, LLC. All rights reserved worldwide.

/** \file Camera.h
\brief A class for describing and managing a camera view.
*/
#pragma once

#ifdef SWIG
#define SILVERLINING_API
%module SilverLiningCamera
%include "Matrix4.h"
%include "Frustum.h"
%{
#include "Camera.h"
using namespace SilverLining;
%}
#endif

#include "MemAlloc.h"
#include "Matrix4.h"
#include "Frustum.h"

namespace SilverLining
{
    class Renderer;

	/**
	A Camera describes a given view, along with its associated view, projection, and viewport.
	*/
    class Camera : public MemObject
    {
    public:
		/** Constructor; pass in whether this camera is within a right-handed or left-handed coordinate system. */
        Camera(bool rightHanded);

		/** Copy this Camera from another one. If the resulting copy does not require transformation matrices or
		    the camera's frustum to be computed, these may be disabled to gain speed. */
        Camera(const Camera* camera, bool copyXForms, bool needsFrustum);

		/** Virtual destructor. */
        virtual ~Camera();

		/** Associate a 4x4 view matrix with this camera. This should be updated as often as necessary.*/
        void SetModelViewMatrix(const Matrix4& modelViewMatrix);
		/** Associate a 4x4 projection matrix with this camera. */
        void SetProjectionMatrix(const Matrix4& projectionMatrix);
		/** Set the viewport origin and size for this camera. */
        void SetViewport(int x, int y, int w, int h);
		
		/** Retrieve the camera's view matrix. */
        const Matrix4& GetModelViewMatrix() const;
		/** Retrieve the camera's projection matrix. */
        const Matrix4& GetProjectionMatrix() const;
		/** Retrieve the camera's combined view X projection matrix. */
        const Matrix4& GetModelViewProjectionMatrix() const;

		/** Retrieve the viewport origin and size. */
        bool GetViewport(int& x, int& y, int& w, int& h) const;
		/** Retrieve the viewport in a format useful for OpenGL. (x, y, w, h) */
        bool GetViewport(int viewport[4]) const;

		/** Returns if this Camera has a view matrix associated with it yet. */
        bool HasCameraMatrix(void) const;
		/** Returns if this Camera has a projection matrix associated with it yet. */
        bool HasProjectionMatrix(void) const;
		/** Returns if this Camera has a viewport associated with it yet. */
        bool HasViewPort(void) const;

		/** Sets the assumption of what direction is "up". Cannot be called prior to
		Atmosphere::Initialize(). Must be called in conjunction with SetRightVector().

		Be sure to call this prior to positioning any clouds.

		In a geocentric / ECEF coordinate system, this should be the normalized camera position vector pointing
		from the center of the Earth, not the local normal vector of the Earth's ellipsoid. There is a
		subtle difference between the two that can lead to cloud positions being a bit off.
		The incoming vector is used as is (without normalization).
		*/
        void SetUpVector(const Vector3& upVector);

        /** This is same as the SetUpVector() above, but normalizes the incoming values.
        That is, if the incoming vector (x, y, z) is not a unit vector, it is normalized before being stored.
        */
        void SetUpVector(double x, double y, double z);

		/** Sets the assumption of what direction is "right". If the vector (x, y, z)
		is not a unit vector, it is normalized before being stored. Cannot be called prior to
		Atmosphere::Initialize(). Must be called in conjunction with SetUpVector().

		Be sure to call this prior to positioning any clouds. 
		The incoming vector is used as is (without normalization).
		*/
        void SetRightVector(const Vector3& rightVector);

        /** This is same as the SetRightVector() above, but normalizes the incoming values.
        That is, if the incoming vector (x, y, z) is not a unit vector, it is normalized before being stored.
        */
        void SetRightVector(double x, double y, double z);

		/** Retrieve the "up" direction for this camera. */
        const Vector3& GetUpVector(void) const;
		/** Retrieve the "right" direction for this camera. */
        const Vector3& GetRightVector(void) const;

		/** Retrieve the camera's frustum, in world space. */
        const Frustum& GetFrustumWorldSpace(void) const;

		/** Retrieve the camera's position in world space. */
        const Vector3& GetPosition(void) const;
		/** Retrieve the camera's altitude in world space. */
        double         GetAltitude(void) const;

        /** Returns a matrix4 defining the basis based on the current "up vector." */
        const Matrix4& GetBasis4x4() const;

        /** Returns a matrix4 defining the inverse basis based on the current "up vector." */
        const Matrix4& GetInverseBasis4x4() const;

        /** Returns a matrix4 defining the basis based on the current "up vector." */
        const Matrix3& GetBasis3x3() const;

        /** Returns a matrix4 defining the inverse basis based on the current "up vector." */
        const Matrix3& GetInverseBasis3x3() const;

		const float *GetModelViewMatrixFloatArray() const {
			return fModelViewMatrix;
		}

		const float *GetModelViewProjectionMatrixFloatArray() const {
			return fModelViewProjectionMatrix;
		}

		const float *GetProjectionMatrixFloatArray() const {
			return fProjectionMatrix;
		}

		const float *GetBasis4x4FloatArray() const {
			return fBasis;
		}

        bool IsRightHanded(void) const;

        void AdjustNearFarClip(double znear, double zfar, bool noSkew, float minZ, float maxZ, const Renderer *renderer);
        Matrix4 GetProjectionNoSkew(double znear, double zfar, float minZ, float maxZ, const Renderer *renderer) const;
        void SetModelviewLookat(const Vector3& from, const Vector3& to, const Vector3& up);
        void SetProjectionMatrix(double nearClip, double farClip, double fovX, double fovY, float minZ, float maxZ, const Renderer *renderer);
        void GetNearFarClip(double &znear, double &zfar) const;
        void MultiplyModelViewMatrix(const Matrix4& m);
        void GetNDCDepthRange(float& zdmin, float& zdmax, const Renderer *renderer) const;

        const Matrix4& GetBillboardMatrix(void) const;
        
        /** Set the name of the camera. */
        void SILVERLINING_API SetName(const char* name);

        /** Get the name of the camera. */
        const char* SILVERLINING_API GetName(void) const;
        
        /** Equality operator */
        bool operator==(const Camera& rhs) const;

        /** In-Equality operator */
        bool operator!=(const Camera& rhs) const;

        /** Set the renderer specific render target. This is the frame buffer
        object for OpenGL/OpenGL32 renderers.*/
        void SetRenderTarget(int _target){ target = _target; }

        /** Get the renderer specific render target. This is the frame buffer
        object for OpenGL/OpenGL32 renderers.*/
        int GetRenderTarget(void) const { return target; }

        /** Get viewport without any checks as to whether viewport has been set or not*/
        void GetViewPortFast(int& x, int& y, int& w, int& h) const
        { 
            x = viewport[0]; 
            y = viewport[1]; 
            w = viewport[2]; 
            h = viewport[3];
        }

        /** Retrieves the depth range for rendering.
        \param zmin The depth buffer range min value, float [0.0 .. 1.0].
        \param zmax The depth buffer range max value, float [0.0 .. 1.0].
        \sa GetViewport() SetViewport() SetDepthRange()
        */
        bool GetDepthRange(float& zmin, float& zmax) const;

        /** Sets the depth range for rendering.
        \param zmin The depth buffer range min value, float [0.0 .. 1.0].
        \param zmax The depth buffer range max value, float [0.0 .. 1.0].
        \sa SetViewport() GetViewport() GetDepthRange()
        */
        bool SetDepthRange(float zmin, float zmax);

		/** Whether the camera has a depth range set on it*/
        bool HasDepthRange(void) const;

		/** Get the field of view given the set projection matrix*/
        double GetFieldOfView(void) const;
    private:
        void UpdateBasis();
        void UpdateFrustum();
        void UpdateCameraPosition();

        Frustum GetFrustumClipSpace() const;
        void SetProjectionMatrix(double l, double r, double t, double b, double zn, double zf, float minZ, float maxZ, const Renderer *renderer);

        void UpdateBillboardMatrix(void) const;

        Matrix4 modelViewMatrix;
        Matrix4 projectionMatrix;
        Matrix4 modelViewProjectionMatrix;

		float fModelViewMatrix[16];
		float fProjectionMatrix[16];
		float fModelViewProjectionMatrix[16];
		float fBasis[16];

        int viewport[4];

        bool hasCameraMatrix, hasProjectionMatrix;
        bool hasViewport;

        Vector3 upVector, rightVector;
        Matrix4 basis, invBasis;
        Matrix3 basis3, invBasis3;
        bool rightHanded;

        Frustum worldFrustum;

        Vector3 cameraPosition;

        mutable Matrix4 billboard;
        bool useNDC;
        double screenBlendFactor;
        bool billboardMatrixOutOfDate;

		bool needsFrustum;

        SL_STRING name;

        int target;

        float nearDepth, farDepth;
        bool hasDepthRange;
    };

}
