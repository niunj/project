// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

#include "Glare.h"
#include "Utils.h"
#include "Renderer.h"
#include "LuminanceMapper.h"
#include "Atmosphere.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "Camera.h"
#include "GlareShader.h"
#include "SLAssert.h"

using namespace SilverLining;

#define TEXSIZE 512

static double glareDegrees;

GlareManager::GlareManager(const Atmosphere& _atmosphere, const RandomNumberGenerator* rng, ThreadCameraStreamData* _tcsData)
    : atmosphere(_atmosphere)
    , mesopic(0), scotopic(0), photopic(0)
    , photopicDisc(0), visionType(PHOTOPIC)
    , tcsData(_tcsData)
{
    Configuration::GetDoubleValue("max-glare-diameter", glareDegrees);

    bool sunGlare = false, moonGlare = false, starGlare = false;
    Configuration::GetBoolValue("disable-sun-glare", sunGlare);
    Configuration::GetBoolValue("disable-moon-glare", moonGlare);
    Configuration::GetBoolValue("disable-star-glare", starGlare);
    sunGlare = !sunGlare;
    moonGlare = !moonGlare;
    starGlare = !starGlare;

    texelSize = glareDegrees / (double)TEXSIZE;

    if (sunGlare || moonGlare || starGlare) {
        MakeTextures(rng, tcsData);
    }
}

GlareManager::~GlareManager()
{
    Renderer* renderer = tcsData->GetRenderer();
    if (renderer) {
        if (mesopic) {
            renderer->ReleaseTexture(mesopic);
        }
        if (scotopic) {
            renderer->ReleaseTexture(scotopic);
        }
        if (photopic) {
            renderer->ReleaseTexture(photopic);
        }
        if (photopicDisc) {
            renderer->ReleaseTexture(photopicDisc);
        }
    }
}

const Atmosphere& GlareManager::GetAtmosphere(void) const
{
    return atmosphere;
}

bool GlareManager::BuildFlareTexture(float *&outBuffer, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    bool ok = false;
    int i;
    unsigned char *tmp = SL_NEW unsigned char[TEXSIZE * TEXSIZE * 4 * sizeof(unsigned char)];

    Renderer* renderer = tcsData->GetRenderer();
    Vector3 origin((double)TEXSIZE * 0.5, (double)TEXSIZE * 0.5, 0);
    double radius = (double)TEXSIZE * 0.5; // * sqrt(2.0);

    int x, y, vpW, vpH;

    bool offscreen = false;
    RenderTextureHandle target = 0;
    Configuration::GetBoolValue("render-offscreen", offscreen);
    if (offscreen) {
        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling) ? "SilverLining::Glare::OffscreenTexture" : (const char *)0;

        offscreen = (renderer->InitRenderTexture(TEXSIZE, TEXSIZE, &target, name));
        if (offscreen) {
            renderer->MakeRenderTextureCurrent(target, true);
        }
    }

    if (renderer->GetViewport(x, y, vpW, vpH)) {

        if (offscreen) vpW = vpH = TEXSIZE;

        renderer->Set2DOrthoMatrix(vpW, vpH);

        renderer->EnableDepthReads(false);
        renderer->EnableDepthWrites(false);

        renderer->ClearScreen(Color(0,0,0,1));

        int flareLines = 360;
        Configuration::GetIntValue("flare-lines", flareLines);
        double flareFalloff = 1.2;
        Configuration::GetDoubleValue("flare-falloff", flareFalloff);

        for (i = 0; i < flareLines; i++) {
            double dangle = rng->UniformRandomDouble();
            double angle = dangle * 2.0 * MathUtils::Pi();
            Vector3 random(radius * sin(angle), radius * cos(angle), 0);

            double intensity = rng->UniformRandomDouble();

            Color c(intensity, intensity, intensity);

            renderer->DrawAALine(c, 1.0, origin, origin + random);
        }

        renderer->EnableDepthReads(true);
        renderer->EnableDepthWrites(true);

        renderer->GetPixels(0, 0, TEXSIZE, TEXSIZE, tmp, true);

        if (offscreen) {
            //ren->RestoreRenderTexture(target);
            renderer->BindRenderTexture(target, 0);
            renderer->ReleaseRenderTexture(target);
        }

        outBuffer = SL_NEW float[TEXSIZE * TEXSIZE * sizeof(float)];

        float *out = outBuffer;
        float totalIntensity = 0;

        int stride = TEXSIZE * 4;
        float cx = (float)floor(TEXSIZE * 0.5);
        float cy = (float)floor(TEXSIZE * 0.5);
        float unit = (float)(TEXSIZE) * 0.5;

        for (int row = 0; row < TEXSIZE; row++) {
            float ny = (float)(row - cy) / unit;
            for (int col = 0; col < TEXSIZE; col++) {
                float nx = (float)(col - cx) / unit;
                float dist = sqrtf(ny*ny + nx*nx);
                if (dist > 1.0) dist = 1.0;
                float intensity = (float)tmp[row*stride + col*4] / 255.0f;
                intensity *= ::pow(dist, (float)flareFalloff);
                totalIntensity += intensity;
                *out++ = intensity;
            }
        }

        // Make avg 1.0

        if (totalIntensity > 0) {
            float scale = (float)(TEXSIZE * TEXSIZE) / totalIntensity;
            for (i = 0; i < TEXSIZE * TEXSIZE; i++) {
                outBuffer[i] *= scale;
            }
        }

        ok = true;
    }

    SL_DELETE[] tmp;

    return ok;
}

bool GlareManager::MakeTextures(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{

    Renderer* renderer = tcsData->GetRenderer();

    if (!renderer->HasFloatTextures()) {
        return false;
    }

    renderer->PushAllState();

    float *g, *g1;
    if (BuildFlareTexture(g, rng, tcsData)) {
        if (BuildFlareTexture(g1, rng, tcsData)) {

            double *f0 = SL_NEW double[TEXSIZE * TEXSIZE];
            double *f1 = SL_NEW double[TEXSIZE * TEXSIZE];
            double *f2 = SL_NEW double[TEXSIZE * TEXSIZE];
            double **f3 = SL_NEW double*[3]; // rgba
            for (int i = 0; i < 3; i++) {
                f3[i] = SL_NEW double[TEXSIZE * TEXSIZE];
            }

            double midpoint = (double)TEXSIZE * 0.5;
            // make sure the midpoint falls on a pixel.
            midpoint = floor(midpoint);

            double scale = 1.0;
            Configuration::GetDoubleValue("glare-brightness", scale);

            for (int r = 0; r < TEXSIZE; r++) {
                for (int c = 0; c < TEXSIZE; c++) {
                    int idx = r * TEXSIZE + c;

                    double du = (double)r - midpoint;
                    double dv = (double)c - midpoint;
                    double theta = texelSize * sqrt(du * du + dv * dv);

                    double f0exp = theta / 0.02;
                    f0exp *= f0exp;
                    f0[idx] = 2.61E6 * exp(-f0exp) * scale;

                    double f1den = theta + 0.02;
                    double f2den = f1den * f1den;
                    f1den = f2den * f1den;
                    f1[idx] = (20.91 / f1den) * scale;

                    f2[idx] = (72.37 / f2den) * scale;

                    double wavelengths[3] = {650, 530, 460};
                    for (int i = 0; i < 3; i++) {
                        double f3exp = theta - 3.0 * (wavelengths[i] / 568.0);
                        f3exp *= f3exp;
                        f3[i][idx] = (436.9 * (568.0 / wavelengths[i]) * exp(-f3exp)) * scale;
                    }
                }
            }

            float *mesopicTex = SL_NEW float[TEXSIZE * TEXSIZE * 3];
            float *scotopicTex = SL_NEW float[TEXSIZE * TEXSIZE * 3];
            float *photopicTex = SL_NEW float[TEXSIZE * TEXSIZE * 3];
            float *photopicDiscTex = SL_NEW float[TEXSIZE * TEXSIZE * 3];

            float *ms = mesopicTex;
            float *sc = scotopicTex;
            float *ph = photopicTex;

            bool photopicFlare = true, halos = true;
            Configuration::GetBoolValue("photopic-disc-flare-lines", photopicFlare);
            Configuration::GetBoolValue("lenticular-halos", halos);

            for (int p = 0; p < TEXSIZE * TEXSIZE; p++) {
                const double f0Sco = 0.282;
                const double f1Sco = 0.478;
                const double f2Sco = 0.207;
                const double f3Sco = 0.033;

                const double f0Mes = 0.368;
                const double f1Mes = 0.478;
                const double f2Mes = 0.138;
                const double f3Mes = 0.016;

                if (halos) {
                    *sc++ = (float)(f0Sco * f0[p] + g1[p] * (f1Sco * f1[p] + f2Sco * f2[p]) + f3Sco * g[p] * f3[0][p]);
                    *sc++ = (float)(f0Sco * f0[p] + g1[p] * (f1Sco * f1[p] + f2Sco * f2[p]) + f3Sco * g[p] * f3[1][p]);
                    *sc++ = (float)(f0Sco * f0[p] + g1[p] * (f1Sco * f1[p] + f2Sco * f2[p]) + f3Sco * g[p] * f3[2][p]);

                    *ms++ = (float)(f0Mes * f0[p] + g1[p] * (f1Mes * f1[p] + f2Mes * f2[p]) + f3Mes * g[p] * f3[0][p]);
                    *ms++ = (float)(f0Mes * f0[p] + g1[p] * (f1Mes * f1[p] + f2Mes * f2[p]) + f3Mes * g[p] * f3[1][p]);
                    *ms++ = (float)(f0Mes * f0[p] + g1[p] * (f1Mes * f1[p] + f2Mes * f2[p]) + f3Mes * g[p] * f3[2][p]);
                } else {
                    *sc++ = (float)(f0Sco * f0[p] + g1[p] * (f1Sco * f1[p] + f2Sco * f2[p]));
                    *sc++ = (float)(f0Sco * f0[p] + g1[p] * (f1Sco * f1[p] + f2Sco * f2[p]));
                    *sc++ = (float)(f0Sco * f0[p] + g1[p] * (f1Sco * f1[p] + f2Sco * f2[p]));

                    *ms++ = (float)(f0Mes * f0[p] + g1[p] * (f1Mes * f1[p] + f2Mes * f2[p]));
                    *ms++ = (float)(f0Mes * f0[p] + g1[p] * (f1Mes * f1[p] + f2Mes * f2[p]));
                    *ms++ = (float)(f0Mes * f0[p] + g1[p] * (f1Mes * f1[p] + f2Mes * f2[p]));
                }

                float i;
                if (photopicFlare) {
                    i = (float)(0.384 * f0[p] + g1[p] * (0.478 * f1[p] + 0.138 * f2[p]));
                } else {
                    i = (float)(0.384 * f0[p] + /*g1[p] * */ (0.478 * f1[p] + 0.138 * f2[p]));
                }

                *ph++ = i;
                *ph++ = i;
                *ph++ = i;
            }

            bool enableObjectLabeling = false;
            Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
            const char* name = 0;

            name = (enableObjectLabeling)? "SilverLining::Glare Mesopic Texture" : (const char *)0;
            renderer->LoadFloatTextureRGB(mesopicTex, TEXSIZE, TEXSIZE, &mesopic, name);

            name = (enableObjectLabeling)? "SilverLining::Glare Scotopic Texture" : (const char *)0;
            renderer->LoadFloatTextureRGB(scotopicTex, TEXSIZE, TEXSIZE, &scotopic, name);

            name = (enableObjectLabeling)? "SilverLining::Glare Photopic Texture" : (const char *)0;
            renderer->LoadFloatTextureRGB(photopicTex, TEXSIZE, TEXSIZE, &photopic, name);

            SL_DELETE[] f0;
            SL_DELETE[] f1;
            SL_DELETE[] f2;
            for (int i = 0 ; i < 3; i++) {
                SL_DELETE[] f3[i];
            }
            SL_DELETE[] f3;
            SL_DELETE[] mesopicTex;
            SL_DELETE[] scotopicTex;
            SL_DELETE[] photopicTex;
            SL_DELETE[] photopicDiscTex;

            SL_DELETE[] g1;
            SL_DELETE[] g;
        }
    }

    renderer->PopAllState();
    return true;
}

const TextureHandle GlareManager::GetTexture(int type) const
{
    if (type == AUTOSELECT) type = visionType;

    switch (type) {
    case PHOTOPIC:
        return photopic;
        break;

    case MESOPIC:
        return mesopic;
        break;

    case SCOTOPIC:
        return scotopic;
        break;

    default:
        return 0;
        break;
    }
}

bool GlareManager::HasGlare(ThreadCameraStreamData* tcsData) const
{
    return tcsData->GetGlareShader()->GetShader()!=0;
}

ShaderHandle GlareManager::GetProgram(ThreadCameraStreamData* tcsData) const
{
    return tcsData->GetGlareShader()->GetShader();
}

bool GlareManager::EnableShaders(ThreadCameraStreamData* tcsData)
{
    double rodNits, coneNits;

    tcsData->GetLuminanceMappper()->GetSceneLogAvg(&rodNits, &coneNits);

    if (coneNits < 0.01) {
        visionType = SCOTOPIC;
    } else if (coneNits < 3.0) {
        visionType = MESOPIC;
    } else {
        visionType = PHOTOPIC;
    }

    return tcsData->GetGlareShader()->Bind();

}

double GlareManager::GetDiscArea() const
{
    double r = (0.5 * 0.5) / texelSize;
    double area = MathUtils::Pi() * r * r;

    return area;
}

bool GlareManager::DisableShaders(ThreadCameraStreamData* tcsData)
{
    tcsData->GetGlareShader()->UnBind();
    return true;
}

Glare::Glare(GlareManager *mgr, bool useShaderInstancing, const BillboardBuffer& buffer, ThreadCameraStreamData* _tcsData) : Billboard(), intensity(0)
    , tcsData(_tcsData)
{
    glareMgr = mgr;

    Initialize(MathUtilsFloat::Epsilon(), MathUtilsFloat::Epsilon(), 0, useShaderInstancing, buffer);
}

Glare::~Glare()
{
}

void Glare::SetIntensity(double nits, ThreadCameraStreamData* tcsData)
{
    // D65 white point
    double x = 0.312727, y = 0.329024;
    tcsData->GetLuminanceMappper()->DurandMapper(&x, &y, &nits);

    double maxNits = 1.5;
    Configuration::GetDoubleValue("glare-luminance-cap", maxNits);
    if (nits > maxNits) nits = maxNits;

    nits -= 1.0;

    if (nits < 0) nits = 0;

    SetAbsoluteIntensity(nits);

}

void Glare::SetAbsoluteIntensity(double nits)
{
    intensity = (float)nits;
}

bool Glare::IsCulled(const Frustum& f) const
{
    const Vector3f& fCenter = GetWorldPosition();
    Vector3 center(fCenter.x, fCenter.y, fCenter.z);

    for (int i = 0; i < 6; i++) {
        const Plane& p = f.GetPlane(i);
        Vector3 N = p.GetNormal();
        double origin = N.Dot(center) + p.GetDistance();

        if (origin < 0) {
            return true;
        }
    }

    return false;
}

bool Glare::Draw(int pass, const Vector3& outputScale, unsigned long millis, const Camera* glareCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, ThreadCameraStreamData* _tcsData, bool forceDepth)
{
    SL_ASSERT(tcsData == _tcsData);
    if (sharedVb && pass == 0 && intensity > 0) {
        TextureHandle tex = glareMgr->GetTexture(AUTOSELECT);

        if (tex) {
            const Camera* camera = tcsData->GetCamera();
            Renderer* renderer = tcsData->GetRenderer();

            double minAlt = 0;
            Configuration::GetDoubleValue("glare-minimum-altitude", minAlt);
            if (minAlt != 0) {
                Vector3f dir = GetWorldPosition();
                Vector3 dDir(dir.x, dir.y, dir.z);
                dDir.Normalize();
                dDir = dDir * camera->GetInverseBasis3x3();
                if (dDir.y < minAlt) {
                    return true;
                }
            }

            Vector3 v;
            v.x = v.y = v.z = intensity;
            SetAngularSize(glareDegrees);
            renderer->SetConstantMatrix(glareMgr->GetProgram(tcsData), "sl_modelViewProj", glareCamera->GetModelViewProjectionMatrix());
            renderer->SetConstantVector(glareMgr->GetProgram(tcsData), "sl_intensity", v);
            renderer->EnableBlending(SRCCOLOR, ONE);
            //renderer->DisableBlending();
            bool ok = Billboard::Draw(tex, 0, 1.0, outputScale, false, millis, glareCamera, overriddenBillboardMatrix, tcsData, forceDepth);
            return ok;
        }

        return true;
    }

    return false;
}
