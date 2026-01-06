// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "TextureManager.h"
#include "Renderer.h"
#include "Atmosphere.h"
#include "TGALoader.h"
#include "Configuration.h"
#include "Mutex.h"
#include "ThreadCameraStreamData.h"
#include "Rain.h"
#include "Sleet.h"
#include "SLAssert.h"
#include "MathUtils.h"

#include <string>
#include <sstream>

namespace SilverLining
{
TextureManager::TextureManager(ThreadCameraStreamData* _tcsData)
    : textureDimension(0)
    , wispTexture(0)
    , atlasTexture(0)
    , metaballTexture(0)
    , noiseTexture(0)
    , stratusTexture(0)
    , tcsData(_tcsData)
{
    mutex = SL_NEW Mutex;
}

TextureManager::~TextureManager()
{
    SL_DELETE mutex;
}

static int rainTextureIndex(int i, int j)
{
    return i * NUM_RAIN_TEXTURE_OSCILLATIONS + j;
}

void TextureManager::ReleaseTextures()
{
    ScopedMutex scopedMutex(mutex);
    Renderer* renderer = tcsData->GetRenderer();

    if (wispTexture) {
        renderer->ReleaseTexture(wispTexture);
    }
    wispTexture = 0;

    if (atlasTexture) {
        renderer->ReleaseTexture(atlasTexture);
    }
    atlasTexture = 0;

    if (metaballTexture) {
        renderer->ReleaseTexture(metaballTexture);
    }
    metaballTexture = 0;

    if (noiseTexture) {
        renderer->ReleaseTexture(noiseTexture);
    }
    noiseTexture = 0;

    if (stratusTexture) {
        renderer->ReleaseTexture(stratusTexture);
    }
    stratusTexture = 0;;

    ReleaseNamedAtlasTextures();

    for (unsigned int day = 0; day < moonTextures.size(); ++day) {
        if (moonTextures[day]) {
            renderer->ReleaseTexture(moonTextures[day]);
        }
        moonTextures[day] = 0;
    }

    for (unsigned int i = 0; i < rainTextures.size(); i++) {
        if (rainTextures[i]) {
            renderer->ReleaseTexture(rainTextures[i]);
            rainTextures[i] = 0;
        }
    }

    for (unsigned int j = 0; j < sleetTextures.size(); j++) {
        if (sleetTextures[j]) {
            renderer->ReleaseTexture(sleetTextures[j]);
            sleetTextures[j] = 0;
        }
    }

    if (sunTexture) {
        renderer->ReleaseTexture(sunTexture);
        sunTexture = 0;
        sunTextureLoadAttempted = false;
    }

    if (snowTexture) {
        renderer->ReleaseTexture(snowTexture);
        snowTexture = 0;
        snowTextureLoadAttempted = false;
    }

    if (cirroCumulusTexture) {
        renderer->ReleaseTexture(cirroCumulusTexture);
        cirroCumulusTexture = 0;
        cirroCumulusTextureLoadAttempted = false;
    }

    if (radialTexture) {
        renderer->ReleaseTexture(radialTexture);
        radialTexture = 0;
        radialTextureLoadAttempted = false;
    }

    if (virgaTexture) {
        renderer->ReleaseTexture(virgaTexture);
        virgaTextureLoadAtttempted = false;
    }

    if (cirroStratusTexture) {
        renderer->ReleaseTexture(cirroStratusTexture);
        cirroStratusTextureLoadAttempted = false;
    }

    for (unsigned int i = 0; i < cirrusTextures.size(); i++) {
        if (cirrusTextures[i]) {
            renderer->ReleaseTexture(cirrusTextures[i]);
            cirrusTextures[i] = 0;
        }
    }

    if (cirrusHiResTexture) {
        renderer->ReleaseTexture(cirrusHiResTexture);
        cirrusHiResTextureLoadAttempted = false;
    }
}

bool TextureManager::CreateTextures(const RandomNumberGenerator* rng)
{
    ScopedMutex scopedMutex(mutex);
    typedef unsigned char texel_type;

    textureDimension = 64;
    texel_type *tex;

    bool ok;

    TGALoader tga;

    Renderer* renderer = tcsData->GetRenderer();

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);

    const char *cloudTextureName;
    if (!Configuration::GetStringValue("cumulus-texture-name", cloudTextureName)) {
        cloudTextureName = "wispy-metaball.tga";
    }

    if (tga.Load(cloudTextureName, Atmosphere::GetResourceLoader())) {
        textureDimension = tga.GetWidth();
        tex = SL_NEW texel_type[textureDimension * textureDimension * 2];

        unsigned char *src = tga.GetPixels();
        unsigned char *dst = tex;


        for (int i = 0; i < textureDimension * textureDimension; i++) {
            unsigned char p = *src;
            *dst++ = p;
            *dst++ = p;
            src += 4;
        }

        const char* name = (enableObjectLabeling) ? "SilverLining::Metaball Wisp Texture" : (const char *)0;

        SL_ASSERT(wispTexture == 0);
        ok = renderer->LoadTexture(tex, textureDimension, textureDimension,
                                   &wispTexture, false, false, name);

        SL_DELETE[] tex;
    }

    const char *stratusTextureName;
    if (!Configuration::GetStringValue("stratocumulus-particles-texture-name", stratusTextureName)) {
        stratusTextureName = "puff-alpha.tga";
    }

    if (tga.Load(stratusTextureName, Atmosphere::GetResourceLoader())) {
        textureDimension = tga.GetWidth();
        tex = SL_NEW texel_type[textureDimension * textureDimension * 2];

        unsigned char *src = tga.GetPixels();
        unsigned char *dst = tex;

        for (int i = 0; i < textureDimension * textureDimension; i++) {
            unsigned char r = *src++;
            unsigned char g = *src++;
            unsigned char b = *src++;
            unsigned char a = *src++;
            *dst++ = a == 0 ? 255 : g;
            *dst++ = a;
        }

        const char* name = (enableObjectLabeling) ? "SilverLining::Metaball Stratus Texture" : (const char *)0;

        SL_ASSERT(stratusTexture == 0);
        ok = ok && renderer->LoadTexture(tex, textureDimension, textureDimension,
                                         &stratusTexture, false, false, name);

        SL_DELETE[] tex;
    }

    const char *atlasName;
    if (!Configuration::GetStringValue("cloud-atlas-texture-name", atlasName)) {
        atlasName = "CloudAtlas.tga";
    }

    SL_ASSERT(atlasTexture == 0);
    atlasTexture = CreateAtlasTexture(atlasName);
    if (!atlasTexture) {
        ok = false;
    }

    Configuration::GetIntValue("metaball-texture-dimension", textureDimension);

    tex = SL_NEW texel_type[textureDimension * textureDimension * 2];

    double midpoint = (double)textureDimension * 0.5;
    texel_type *tp = tex;

    for (int row = 0; row < textureDimension; row++) {
        double dy = midpoint - (double)row;
        for (int col = 0; col < textureDimension; col++) {
            double dx = midpoint - (double)col;
            double dist = sqrt(dx*dx + dy*dy) / midpoint;

            if (dist <= 1.0) {
                double a2 = dist * dist;
                double a4 = a2 * a2;
                double a6 = a4 * a2;

                double texel = ((-4.0 / 9.0) * a6 + (17.0 / 9.0) * a4 - (22.0 / 9.0) * a2 + 1.0) * 255.0;
                texel_type tt = (texel_type)texel;

                *tp++ = tt;
                *tp++ = tt;
            } else {
                *tp++ = 0;
                *tp++ = 0;
            }
        }
    }

    const char* name = (enableObjectLabeling) ? "SilverLining::Metaball Texture" : (const char *)0;

    SL_ASSERT(metaballTexture == 0);
    ok = renderer->LoadTexture(tex, textureDimension, textureDimension,
                               &metaballTexture, false, false, name);

    SL_DELETE[] tex;

    ok = CreateNoiseTexture(rng);

    return ok;
}

TextureHandle TextureManager::CreateAtlasTexture(const char* atlasName)
{
    typedef unsigned char texel_type;

    TGALoader tga;
    texel_type *tex;
    bool ok = false;

    if (tga.Load(atlasName, Atmosphere::GetResourceLoader())) {
        textureDimension = tga.GetWidth();
        tex = SL_NEW texel_type[textureDimension * textureDimension * 2];

        bool atlasBlend = false;
        Configuration::GetBoolValue("cloud-atlas-normal-blend", atlasBlend);

        unsigned char *src = tga.GetPixels();
        unsigned char *dst = tex;

        if (atlasBlend) {
            for (int i = 0; i < textureDimension * textureDimension; i++) {
                unsigned char r = *src++;
                unsigned char g = *src++;
                unsigned char b = *src++;
                unsigned char a = *src++;
                *dst++ = g;
                *dst++ = a;
            }
        } else {
            for (int i = 0; i < textureDimension * textureDimension; i++) {
                unsigned char p = *src;
                *dst++ = p;
                *dst++ = p;
                src += tga.GetBytesPerPixel();
            }
        }

        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling) ? "SilverLining::Metaball Atlas Texture" : (const char *)0;

        Renderer* renderer = tcsData->GetRenderer();
        TextureHandle atlasTexture;
        ok = renderer->LoadTexture(tex, textureDimension, textureDimension,
                                   &atlasTexture, false, false, name);

        SL_DELETE[] tex;

        return ok ? atlasTexture : 0;
    }

    return 0;
}

TextureHandle TextureManager::GetGaussianTexture() const
{
    return metaballTexture;
}

TextureHandle TextureManager::GetWispTexture() const
{
    return wispTexture;
}

TextureHandle TextureManager::GetAtlasTexture() const
{
    return atlasTexture;
}

TextureHandle TextureManager::GetStratusTexture() const
{
    return stratusTexture;
}

TextureHandle TextureManager::GetNoiseTexture() const
{
    return noiseTexture;
}

TextureHandle TextureManager::GetOrCreateNamedAtlasTexture(const SL_STRING& atlasName)
{
    if (atlasName.empty()) {
        return 0;
    }
    ScopedMutex scopedMutex(mutex);
    SL_ORDERED_MAP(SL_STRING, TextureHandle)::iterator texIt = namedAtlasTextures.find(atlasName);
    if (texIt != namedAtlasTextures.end()) {
        return texIt->second;
    }
    TextureHandle atlasTexture = CreateAtlasTexture(atlasName.c_str());
    namedAtlasTextures[atlasName] = atlasTexture;
    namedAtlasTexturesVector.push_back(atlasTexture);
    return atlasTexture;
}

const SL_VECTOR(TextureHandle)& TextureManager::GetNamedAtlasTexturesVector(void) const
{
    return namedAtlasTexturesVector;
}

void TextureManager::ReleaseNamedAtlasTextures()
{
    Renderer* renderer = tcsData->GetRenderer();

    for (SL_ORDERED_MAP(SL_STRING, TextureHandle)::const_iterator it = namedAtlasTextures.begin(); it != namedAtlasTextures.end(); ++it) {
        TextureHandle tex = it->second;
        renderer->ReleaseTexture(tex);
    }
    namedAtlasTextures.clear();
    namedAtlasTexturesVector.clear();
}


bool TextureManager::CreateNoiseTexture(const RandomNumberGenerator* rng)
{
    static const int NOISE_DIM = 32;

    unsigned int texBytes = (NOISE_DIM * NOISE_DIM * NOISE_DIM * 3) * sizeof(unsigned char);
    unsigned char *noiseTexData = SL_NEW unsigned char[texBytes];

    Vector3 colors[NOISE_DIM][NOISE_DIM][NOISE_DIM];

    int w, h, d;

    for (w = 0; w < NOISE_DIM; w++) {
        for (h = 0; h < NOISE_DIM; h++) {
            for (d = 0; d < NOISE_DIM; d++) {
                colors[w][h][d].x = rng->UniformRandomDouble();
                colors[w][h][d].y = rng->UniformRandomDouble();
                colors[w][h][d].z = rng->UniformRandomDouble();
            }
        }
    }

    int blurWidth = 1;
    unsigned char *p = noiseTexData;
    for (w = 0; w < NOISE_DIM; w++) {
        for (h = 0; h < NOISE_DIM; h++) {
            for (d = 0; d < NOISE_DIM; d++) {
                Vector3 avg;
                int samples = 0;
                for (int bw = w - blurWidth; bw <= w + blurWidth; bw++) {
                    for (int bh = h - blurWidth; bh <= h + blurWidth; bh++) {
                        for (int bd = d - blurWidth; bd <= d + blurWidth; bd++) {
                            if (bw >= 0 && bw < NOISE_DIM && bh >= 0 && bh < NOISE_DIM && bd >= 0 && bd < NOISE_DIM) {
                                samples++;
                                avg = avg + colors[bw][bh][bd];
                            }
                        }
                    }
                }
                avg = avg * (1.0 / (float)samples);
                *p++ = (unsigned char)(avg.x * 255.0);
                *p++ = (unsigned char)(avg.y * 255.0);
                *p++ = (unsigned char)(avg.z * 255.0);
            }
        }
    }

    SL_ASSERT(noiseTexture == 0);

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling) ? "SilverLining::Stratocumulus Cloud Layer Noise Volume Texture" : (const char *)0;

    Renderer* renderer = tcsData->GetRenderer();
    renderer->Load3DTextureRGB(noiseTexData, NOISE_DIM, NOISE_DIM, NOISE_DIM, &noiseTexture, true, true, true, name);

    SL_DELETE [] noiseTexData;

    return noiseTexture != 0;
}

TextureHandle TextureManager::GetOrCreateMoonTexture(int dayRequested)
{
    if (dayRequested >= 30) {
        return 0;
    }

    ScopedMutex scopedMutex(mutex);
    if (moonTextures.size() > 0) {
        return moonTextures[dayRequested];
    }

    Renderer* renderer = tcsData->GetRenderer();

    bool blendSunMoon = false;
    Configuration::GetBoolValue("sun-moon-alpha-blend", blendSunMoon);

    for (int day = 0; day < 30; day++) {
        std::stringstream ss;
        ss << "moonday" << day + 1;
        if (blendSunMoon) {
            ss << "-rgba";
        }
        ss << ".tga";

        TextureHandle moonTexture = 0;
        bool ok = renderer->LoadTextureFromFile(ss.str().c_str(), &moonTexture, false, false, ss.str().c_str());
        if (!ok) {
            std::cerr << "Error loading moon texture: " << ss.str();
        }

        moonTextures.push_back(moonTexture);
    }
    SL_ASSERT(moonTextures.size() == 30);

    return moonTextures[dayRequested];
}

TextureHandle TextureManager::GetOrCreateRainTexture(int angleIndex, int oscillationIndex)
{
    if (angleIndex >= NUM_RAIN_TEXTURE_ANGLES || oscillationIndex >= NUM_RAIN_TEXTURE_OSCILLATIONS) {
        return 0;
    }

    ScopedMutex scopedMutex(mutex);
    if (rainTextures.size() > 0) {
        return rainTextures[rainTextureIndex(angleIndex, oscillationIndex)];
    }

    rainTextures.resize(NUM_RAIN_TEXTURE_ANGLES * NUM_RAIN_TEXTURE_OSCILLATIONS, 0);

    Renderer* renderer = tcsData->GetRenderer();

    for (int i = 0; i < NUM_RAIN_TEXTURE_ANGLES; i++) {
        for (int j = 0; j < NUM_RAIN_TEXTURE_OSCILLATIONS; j++) {

            std::stringstream ss;
            ss << "rain/cv" << i * RAIN_TEXTURE_ANGLE_INCREMENT << "_osc" << j << ".TGA";

            rainTextures[rainTextureIndex(i, j)] = 0;
            renderer->LoadTextureFromFile(ss.str().c_str(), &rainTextures[rainTextureIndex(i, j)], true, true, ss.str().c_str());
        }
    }
    return rainTextures[rainTextureIndex(angleIndex, oscillationIndex)];
}

TextureHandle TextureManager::GetOrCreateSleetTexture(int oscillationIndex)
{
    if (oscillationIndex >= NUM_SLEET_TEXTURE_OSCILLATIONS) {
        return 0;
    }

    ScopedMutex scopedMutex(mutex);
    if (sleetTextures.size() > 0) {
        return sleetTextures[oscillationIndex];
    }

    sleetTextures.resize(NUM_SLEET_TEXTURE_OSCILLATIONS, 0);

    Renderer* renderer = tcsData->GetRenderer();
    for (int j = 0; j < NUM_SLEET_TEXTURE_OSCILLATIONS; j++) {

        std::stringstream ss;
        ss << "sleet/cv0_osc" << j << ".TGA";

        sleetTextures[j] = 0;
        renderer->LoadTextureFromFile(ss.str().c_str(), &sleetTextures[j], true, true, ss.str().c_str());
    }

    return sleetTextures[oscillationIndex];
}

TextureHandle TextureManager::GetOrCreateSunTexture()
{
    ScopedMutex scopedMutex(mutex);

    if (sunTextureLoadAttempted) {
        return sunTexture;
    }

    bool blendSunMoon = false;
    Configuration::GetBoolValue("sun-moon-alpha-blend", blendSunMoon);

    const std::string fileName = blendSunMoon ? "sun-RGBA.tga" : "sun.tga";

    sunTexture = 0;
    tcsData->GetRenderer()->LoadTextureFromFile(fileName.c_str(), &sunTexture, false, false, fileName.c_str());

    sunTextureLoadAttempted = true;

    return sunTexture;
}

TextureHandle TextureManager::GetOrCreateSnowTexture()
{
    ScopedMutex scopedMutex(mutex);

    if (snowTextureLoadAttempted) {
        return snowTexture;
    }

    const std::string fileName = "snow/Snowflake.TGA";

    snowTexture = 0;
    tcsData->GetRenderer()->LoadTextureFromFile(fileName.c_str(), &snowTexture, false, false, fileName.c_str());

    snowTextureLoadAttempted = true;

    return snowTexture;
}

SilverLining::TextureHandle TextureManager::GetOrCreateCirrocumulusTexture()
{
    ScopedMutex scopedMutex(mutex);

    if (cirroCumulusTextureLoadAttempted) {
        return cirroCumulusTexture;
    }

    const std::string fileName = "cirrocumulus.tga";

    cirroCumulusTexture = 0;
    tcsData->GetRenderer()->LoadTextureFromFile(fileName.c_str(), &cirroCumulusTexture, false, false, fileName.c_str());

    cirroCumulusTextureLoadAttempted = true;

    return cirroCumulusTexture;
}

SilverLining::TextureHandle TextureManager::GetOrCreateRadialTexture()
{
    ScopedMutex scopedMutex(mutex);

    if (radialTextureLoadAttempted) {
        return radialTexture;
    }

    bool blendSunMoon = false;
    Configuration::GetBoolValue("sun-moon-alpha-blend", blendSunMoon);

    const std::string fileName = blendSunMoon ? "radial-glow-RGBA.tga" : "radial-glow.tga";

    radialTexture = 0;
    tcsData->GetRenderer()->LoadTextureFromFile(fileName.c_str(), &radialTexture, false, false, fileName.c_str());

    radialTextureLoadAttempted = true;

    return radialTexture;
}

SilverLining::TextureHandle TextureManager::GetOrCreateVirgaTexture()
{
    ScopedMutex scopedMutex(mutex);

    if (virgaTextureLoadAtttempted) {
        return virgaTexture;
    }

    const char* fileName = nullptr;
    Configuration::GetStringValue("cumulonimbus-rain-texture", fileName);

    virgaTexture = 0;
    tcsData->GetRenderer()->LoadTextureFromFile(fileName, &virgaTexture, true, false, fileName);

    virgaTextureLoadAtttempted = true;

    return virgaTexture;
}

SilverLining::TextureHandle TextureManager::GetOrCreateCirroStratusTexture()
{
    ScopedMutex scopedMutex(mutex);

    if (cirroStratusTextureLoadAttempted) {
        return cirroStratusTexture;
    }

    const char* fileName = "cirrostratus.tga";

    cirroStratusTexture = 0;
    tcsData->GetRenderer()->LoadTextureFromFile(fileName, &cirroStratusTexture, true, false, fileName);

    cirroStratusTextureLoadAttempted = true;

    return cirroStratusTexture;
}

TextureHandle TextureManager::GetOrCreateCirrusTexture(int index)
{
    if (index >= 2) {
        return 0;
    }
    ScopedMutex scopedMutex(mutex);

    if (cirrusTextures.size() > 0) {
        return cirrusTextures[index];
    }

    cirrusTextures.resize(2, 0);

    Renderer* renderer = tcsData->GetRenderer();

    {
        const char* fileName = "cirrus.tga";
        tcsData->GetRenderer()->LoadTextureFromFile(fileName, &cirrusTextures[0], true, false, fileName);
    }

    {
        const char* fileName = "cirrus2.tga";
        tcsData->GetRenderer()->LoadTextureFromFile(fileName, &cirrusTextures[1], true, false, fileName);
    }

    return cirrusTextures[index];
}

TextureHandle TextureManager::GetOrCreateCirrusHiResTexture()
{
    ScopedMutex scopedMutex(mutex);

    if (cirrusHiResTextureLoadAttempted) {
        return cirrusHiResTexture;
    }

    const char* fileName = "cirrus-hires.tga";

    cirrusHiResTexture = 0;
    tcsData->GetRenderer()->LoadTextureFromFile(fileName, &cirrusHiResTexture, true, false, fileName);

    cirrusHiResTextureLoadAttempted = true;

    return cirrusHiResTexture;
}

}