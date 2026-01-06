// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

/** \file Glare.h
    \brief Renders glare effects for bright point lights.
 */

#ifndef GLARE_H
#define GLARE_H

#include "Billboard.h"
#include "Renderer.h"

namespace SilverLining
{
    class BillboardShader;
    class ThreadCameraStreamData;
/** Specifies how to select which glare texture to use, based on the current
    lighting conditions and how your eye responds to them. */
enum GlareTypes {SCOTOPIC, MESOPIC, PHOTOPIC, AUTOSELECT};

/** Renders glare effects for bright point lights. Models different glare effects
   for scotopic, mesopic, and photopic vision. This isn't lens-flare; it's a real
   simulation of what happens inside your eye when a bright light is observed at
   night, dawn, or dusk.

   It extends the ideas presented in:

   Spencer, G., Shirley, P., Zimmerman, K., and Greenberg, D. P. 1995. Physically-based
   glare effects for digital images. In <i>Proceedings of the 22nd Annual Conference on
   Computer Graphics and interactive Techniques</i> S. G. Mair and R. Cook, Eds.
   SIGGRAPH '95. ACM Press, New York, NY, 325-334. DOI= http://doi.acm.org/10.1145/218380.218466

   by computing them in real-time using shaders on the GPU.
 */
class GlareManager : public MemObject
{
public:
/** Default constructor. */
    GlareManager(const Atmosphere& atmosphere, const RandomNumberGenerator* rng, ThreadCameraStreamData* _tcsData);

/** Destructor. */
    ~GlareManager();

/** Retrieves a handle to the glare texture appropriate for the type selected.

    \param type A member of the GlareTypes enumeration, specifying how to select
    the glare texture. Valid values are:

    SCOTOPIC: A glare texture appropriate for night-adapted vision.
    MESOPIC: A glare texture appropriate for dawn or dusk, when color perception is beginning
    to fade.
    PHOTOPIC: A glare texture appropriate for daylight-adapted vision.
    PHOTOPICDISC: A glare texture for photopic conditions, that contains only the light disc
    and no flare effects.
    AUTOSELECT: Automatically selects the appropriate glare texture based on the current
    lighting conditions previously set in the LuminanceMapper class. Requires that EnableShaders()
    was previously called.
 */
    const TextureHandle GetTexture(int type) const;

/** Computes the current lighting conditions based on the LuminanceMapper class, and configures
   and enables the fragment shader for glare effects.

   \return True if the operation succeeded, false if the shader could not be enabled for any reason.

   \sa DisableShaders()
 */
    bool EnableShaders(ThreadCameraStreamData* tcsData);

/** Disables and shaders that were previously enabled via a successful call to EnableShaders().

   \return true if the operation completed successfully. */
    bool DisableShaders(ThreadCameraStreamData* tcsData);

/** Returns the area of the light's perceived disc in texels. */
    double GetDiscArea() const;

/** Returns if glare effects are enabled on this system. */
    bool HasGlare(ThreadCameraStreamData* tcsData) const;

/** Returns a handle to the fragment program for gare. */
    ShaderHandle GetProgram(ThreadCameraStreamData* tcsData) const;

    const Atmosphere& GetAtmosphere(void) const;

private:
    bool LoadShaders();
    bool MakeTextures(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);
    bool BuildFlareTexture(float *&outBuffer, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData);

    TextureHandle mesopic, scotopic, photopic, photopicDisc;

    int visionType;

    double texelSize;

    const Atmosphere& atmosphere;

    ThreadCameraStreamData* tcsData;
};

/** An individual glare, managed by the GlareManager class. It is derived
   from Billboard. */
class Glare : public Billboard
{
public:
/** Constructor; requires an associated GlareManager object which it has
   been submitted to.*/
    Glare(GlareManager *glareMgr, bool useShaderInstancing, const BillboardBuffer& buffer, ThreadCameraStreamData* tcsData);

/** Virtual destructor. */
    virtual ~Glare();

/** Sets the intensity of the light source, in cd/m2. This is then
   subtracted from the tone-mapped saturation point before being passed
   into SetAbsoluteIntensity(). */
    void SetIntensity(double nits, ThreadCameraStreamData* tcsData);

/** Sets the absolute intensity of the glare effect, in cd/m2, without
   applying any tone mapping first. */
    void SetAbsoluteIntensity(double nits);

/** Tests if this glare is visible within the given Frustum.

    \param f The Frustum to test visibility against.

    \return true if the glare is not visible in this frustum, and should
    be culled.
 */
    virtual bool IsCulled(const Frustum& f) const;

/** Draw the glare effect. */
    bool Draw(int pass, const Vector3& outputScale, unsigned long millis, const Camera* glareCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, ThreadCameraStreamData* _tcsData, bool forceDepth);

/** Stores a Vector3 associated with this object. It's not used by this class,
   but you can retrieve it later with GetUserVector(). */
    void SetUserVector(const Vector3& v) {
        userVector = v;
    }

/** Retrieves the Vector3 previously set by SetUserVector(). */
    const Vector3& GetUserVector() const {
        return userVector;
    }

private:
    GlareManager *glareMgr;
    float intensity;
    Vector3 userVector;
    ThreadCameraStreamData* tcsData;
    
};
}

#endif
