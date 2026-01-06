// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
\file TextureManager.h
\brief Manages the textures for:
  -Metaballs. (Metaballs models the "puffs" that cumulus clouds are made of.
  -Sky
  -Rain, Sleet
  -etc.
*/

#pragma once

#include "MemAlloc.h"
#include "SLMap.h"
#include "SilverLiningTypes.h"
#include <vector>

namespace SilverLining
{
    class Mutex;
    class ThreadCameraStreamData;
    class RandomNumberGenerator;

    class TextureManager : public MemObject
    {
    public:
        TextureManager(ThreadCameraStreamData* _tcsData);

        virtual ~TextureManager();
    public:
        /** Loads the gaussian, wisp, atlas & stratus textures
        used for lighting, from disk. */
        bool CreateTextures(const RandomNumberGenerator* rng);

        /** Releases the cloud wisp and gaussian textures; call at shutdown. */
        void ReleaseTextures();

        /** Returns a handle to a gaussian texture representing the metaball,
        used for lighting purposes. */
        TextureHandle GetGaussianTexture() const;

        /** Returns a cloud wisp texture, which is used during rendering for
        better visual results. */
        TextureHandle GetWispTexture() const;

        /** Returns a cloud atlas texture, used for hi-res cumulus clouds. */
        TextureHandle GetAtlasTexture() const;

        /** Returns the texture used for StratocumulusParticleCloudLayers. */
        TextureHandle GetStratusTexture() const;

        /** get the noise texture */
        TextureHandle GetNoiseTexture() const;

        /** Loads the texture atlas specified, load if necessary */
        TextureHandle GetOrCreateNamedAtlasTexture(const SL_STRING& atlasName);

        /** Get the vector of texture handles of all loaded atlas textures */
        const SL_VECTOR(TextureHandle)& GetNamedAtlasTexturesVector(void) const;

        /** Moon texture for a given day (0-29) */
        TextureHandle GetOrCreateMoonTexture(int day);

        /** internal */
        TextureHandle GetOrCreateRainTexture(int angleIndex, int oscillationIndex);

        /** internal */
        TextureHandle GetOrCreateSleetTexture(int oscillationIndex);

        /** internal */
        TextureHandle GetOrCreateSnowTexture();

        /** Sun texture */
        TextureHandle GetOrCreateSunTexture();

        /** Cirrocumulus texture */
        TextureHandle GetOrCreateCirrocumulusTexture();

        /** Radial texture */
        TextureHandle GetOrCreateRadialTexture();

        /** Virga texture */
        TextureHandle GetOrCreateVirgaTexture();

        /** CirroStratus texture */
        TextureHandle GetOrCreateCirroStratusTexture();

        /** Cirrus texture */
        TextureHandle GetOrCreateCirrusTexture(int index);

        /** Cirrous texture (hi-res) */
        TextureHandle GetOrCreateCirrusHiResTexture();

    protected:
        TextureHandle CreateAtlasTexture(const char* atlasName);

        /** Release all named atlas textures that were created*/
        void ReleaseNamedAtlasTextures();

        /** create the noise texture*/
        bool CreateNoiseTexture(const RandomNumberGenerator* rng);

    protected:
        ThreadCameraStreamData* tcsData;
        TextureHandle  metaballTexture;
        TextureHandle  wispTexture;
        TextureHandle  atlasTexture;
        TextureHandle  stratusTexture;

        SL_ORDERED_MAP(SL_STRING, TextureHandle) namedAtlasTextures;

        SL_VECTOR(TextureHandle) namedAtlasTexturesVector;

        TextureHandle noiseTexture;

        int textureDimension;

        Mutex* mutex;

        SL_VECTOR(TextureHandle) moonTextures;

        SL_VECTOR(TextureHandle) rainTextures;

        SL_VECTOR(TextureHandle) sleetTextures;

        TextureHandle sunTexture = 0;
        bool sunTextureLoadAttempted = false;

        TextureHandle snowTexture = 0;
        bool snowTextureLoadAttempted = false;

        TextureHandle cirroCumulusTexture = 0;
        bool cirroCumulusTextureLoadAttempted = false;

        TextureHandle radialTexture = 0;
        bool radialTextureLoadAttempted = false;

        TextureHandle virgaTexture = 0;
        bool virgaTextureLoadAtttempted = false;

        TextureHandle cirroStratusTexture = 0;
        bool cirroStratusTextureLoadAttempted = false;

        SL_VECTOR(TextureHandle) cirrusTextures;

        TextureHandle cirrusHiResTexture = 0;
        bool cirrusHiResTextureLoadAttempted = false;
    };
}