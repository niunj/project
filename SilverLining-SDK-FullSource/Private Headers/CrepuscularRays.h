// Copyright (c) 2013-2015 Sundog Software LLC. All rights reserved worldwide.

/**
\file CrepuscularRays.h
\brief A class that performs a postprocessing effect for shafts of light from clouds.
*/

#ifndef CREPUSCULAR_RAYS_H
#define CREPUSCULAR_RAYS_H

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "Sky.h"
#include "Vector3.h"
#include "Color.h"
#include <vector>

namespace SilverLining
{
    class Atmosphere;
    class VertexBuffer;
    class IndexBuffer;
    class CloudLayer;
    class ThreadCameraStreamData;

	class CrepuscularRaysShaderConstantLocations
	{
	public:
		CrepuscularRaysShaderConstantLocations();
		void Init(Renderer* renderer, ShaderHandle shader);

		int sl_lightPositionOnScreen;
		int sl_parameters;
		int sl_lightColor;
		int sl_outputScale;

	private:
		void Reset(void);
	};


    /** A class to render shafts of light from clouds. */
    class CrepuscularRays : public MemObject
    {
    public:
        /// Constructor; takes the Atmosphere to associate the effect with.
        CrepuscularRays(ThreadCameraStreamData* _tcsData);

        /// Virtual destructor
        virtual ~CrepuscularRays();

        /** Computes the position of the sun in screen space, and scales the intensity of the effect
            based on whether the sun is near the current view and the amount of cloud coverage present
            at the camera position.
            \param ephemeris the Ephemeris used to get the sun position.
            \param sunColor The color of the directional sunlight, used to color the crepuscular rays
            \param cloudCoverage The amount of sky covered by clouds (0-1) above the current camera position
        */
        void UpdateLightSource(const Ephemeris *ephemeris, const Color& sunColor, bool geocentricMode, double cloudCoverage, const Camera* camera, ThreadCameraStreamData* tcsData);

        /** Call to render the current blended object list into our offscreen texture that collects things
            that occlude the sun. */
        bool DrawToRenderTexture(const Atmosphere& atmosphere, const Camera* sceneCamera, const Camera* renderCamera);

        /** Draws the actual 2D effect; UpdateLightSource() and DrawToRenderTexture() must have been called previously. */
        bool DrawRays(const Vector3& outputScale, ThreadCameraStreamData* tcsData);

        /** Flag to indicate DrawToRenderTexture() is currently drawing objects into its offscreen texture.  Can be
            used to prevent drawing of objects that shouldn't contribute to rays (such as cirrus clouds.) */
        bool IsRendering() const {return isRendering;}

        /** Set the overall intensity of the effect at runtime; 1.0 maps to the default value of crepuscular-rays-exposure
            in SilverLining.config, while 0 results in no intensity. */
        void SetExposure(float exp) {exposureScale = exp;}

        /** Retrieves the overall intensity of the effect, as set by CrepuscularRays::SetExposure(). */
        float GetExposure() const {return exposureScale;}

        /** Returns the native texture pointer for the cloud texture against a neutral background. */
        void *GetNativeCloudTexture(ThreadCameraStreamData* tcsData);

    protected:
        Vector3 ComputeSunPosClip(const Vector3& sunWorld, bool geocentricMode, bool& clipped, bool& behind, const Camera* camera, ThreadCameraStreamData* tcsData);

        RenderTextureHandle renderTexture;

        ShaderHandle shader;
		CrepuscularRaysShaderConstantLocations shaderConstantLocations;
        VertexBuffer *vertexBuffer;
        IndexBuffer *indexBuffer;
        bool cleared;
        Vector3 sunPosClip, sunPosTex, lightColor;
        float effectScale;
        float exposure, decay, density, weight, exposureScale;
        int texSize;
        bool isRendering;
        double sunDistance;

        ThreadCameraStreamData* tcsData;
    };
}

#endif
