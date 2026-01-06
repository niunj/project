// Copyright (c) 2010-2016 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file LensFlare.h
    \brief A big, flashy lens flare effect on the sun, useful for games.
 */

#ifndef LENSFLARE_H
#define LENSFLARE_H

#if defined(WIN32) || defined(WIN64)
#pragma warning (disable: 4786)
#endif

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "Vector3.h"
#include "Color.h"
#include "Matrix4.h"
#include "FlareShaderConstantLocations.h"
#include <vector>

#define NUM_FLARE_TEXTURES 6
#define NUM_SHINE_TEXTURES 10

namespace SilverLining
{
class Ephemeris;
class Billboard;
class BillboardBuffer;
class BillboardShaderConstantLocations;
class VertexBuffer;
class IndexBuffer;
class OverriddenBillboardMatrix;
class Renderer;
class RandomNumberGenerator;
class Atmosphere;
class Camera;
class ThreadCameraStreamData;
class BillboardShader;


/** Describes an individual lens flare component. Allows us to re-use lens flare
    textures with different sizes, colors, and relative positions along the axis
    defined by the sun and the center of the screen. */
class Flare : public MemObject
{
public:
    Flare(int pTexture, float pPosition, float pSize, Color pColor) :
        size(pSize), position(pPosition), color(pColor), texture(pTexture) {
    }

    float size;
    float position;
    Color color;
    int texture;
};

/** A class for rendering lens flare effects, with proper handling of occlusion of the sun. */
class LensFlare : public MemObject
{
public:
/** Constructor. Pass in the Ephemeris so we know where the sun is. */
    LensFlare(const Atmosphere& atmosphere, const Ephemeris *ephemeris, const RandomNumberGenerator* rng, ThreadCameraStreamData* _tcsData);

/** Virtual destructor. */
    virtual ~LensFlare();

/** Must be called at the end of the frame; this is where occlusion tests are performed
   against the sun. The occlusion test performed here is twofold: first, we do a conventional
   occlusion query to see if anything is in front of the sun as defined by the depth buffer's
   contents, which will catch occlusion from objects in your scene. However, since the clouds are
   translucent and do not write to the depth buffer, an occlusion query won't catch occlusion by
   clouds. So, we also read back a single pixel from the frame buffer at the sun's location and
   see how far from white it is. This isn't perfect, but works well enough. Why don't we draw
   something more distinctive like a magenta pixel at the start of the frame and just test for that here,
   and then draw the sun at the end of the frame? Well, again, clouds don't draw to the depth buffer,
   and we need to test for occlusion after the clouds are drawn, so we can't get away with drawing
   the sun last unfortunately. If anyone has any better ideas let me (fkane@sundog-soft.com) know...

   The occlusion test results computed here are exposed via Atmosphere::GetSunOcclusion() as well.

   This should be called prior to calling LensFlare::Draw().
 */
	void Update(bool geocentricMode, const Vector3& outputScale, unsigned long millis, const Camera* camera, const OverriddenBillboardMatrix& overriddenBillboardMatrix);

/** Draws the lens flare effects, if the sun is in the frame and not competely occluded (as
   determined by the occlusion tests performed in Update(), which should be called before Draw()). */
    void Draw(const Vector3& outputScale, const Camera* camera);

/** Return our best guess as to how much of the sun is occluded by objects in the sun and by the
   clouds. The return value is between 0.0 (no occlusion) and 1.0 (fully occluded.) For apps that manage
   their own lens flare, this can be useful. In the case of the sun not being in the frame at all, 1.0
   is returned. (An implication of that is you can't use this as a way to cheat and figure out if the
   camera is in a shadow, unless the sun is in the viewport.) */
    float GetSunOcclusion() const {
        return occlusion;
    }

/** Called in response to a D3D9 device loss. */
    void DeviceLost();

private:
    bool InitFlares();

    TextureHandle flareTextures[NUM_FLARE_TEXTURES];
    TextureHandle shineTextures[NUM_SHINE_TEXTURES];
    TextureHandle sunTexture;
    Billboard *sunQuad;
    const Ephemeris *ephemeris;
    double sunWidth, sunDistance;
    float occlusion;
    Vector3 sunPos, sunClip;
    VertexBuffer *vb;
    IndexBuffer *ib;
    SL_VECTOR(Flare) flares;


    bool lensFlareDisabled;
    float flareSize;
    float flareFalloff;
    float flareBrightness;
    bool disableOcclusion;
    bool doShines;
    QueryHandle queryHandle;
    bool usePBO;
    bool alwaysOn;

    const Atmosphere& atmosphere;
    ThreadCameraStreamData* tcsData;
};
}

#endif
