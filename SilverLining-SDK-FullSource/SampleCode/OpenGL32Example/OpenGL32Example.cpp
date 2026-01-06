// OpenGL32Example.cpp : Demonstrates usage of the SilverLining(tm) atmospheric rendering library.
// Copyright (c) 2010-2014 Sundog Software. All rights reserved worldwide.

#ifdef WIN32
#include "stdafx.h"
#endif
#include <stdlib.h>
#include <math.h>
#include "SilverLining.h"
#ifdef MAC
#include <GLUT/glut.h>
#else
#ifdef USE_FREEGLUT
#include "freeglut.h"
#else
#include "glut.h"
#endif //USE_FREEGLUT
#endif //MAC
#include <assert.h>
#include <sstream>

#include "../SamplesCommon/SampleCommon.h"

#pragma comment(lib, "opengl32.lib")

// All SilverLining objects are in the SilverLining namespace.
using namespace SilverLining;

// Statics and defines for a simple, self-contained demo application
static Atmosphere *atm = 0; // The Atmosphere object is the main interface to SilverLining.
static float aspectRatio = 0;
static float yaw = 0;
static float pitch = -10;
static float translation = 0;

// Simulated visibility in meters, for fog effects.
static const float kVisibility = 20000.0f;
static std::vector<CloudLayer*> layers;

static bool autoTest = false;
static bool autoTestSavingGoldenImages = false;
static float windMultiplier = 1.0f;
static bool drawLightning = true;
static int window_width = 800;
static int window_height = 600;

static int windowHandle = 0;

static bool SaveScreenShot(const std::string& fileName)
{
    //This prevents the images getting padded
    // when the width multiplied by 3 is not a multiple of 4
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2];
    int h = vp[3];

    int nSize = w*h * 3;
    // First let's create our buffer, 3 channels per Pixel
    char* dataBuffer = (char*)malloc(nSize*sizeof(char));

    if (!dataBuffer) return false;

    // Let's fetch them from the backbuffer
    // We request the pixels in GL_BGR format, thanks to Berzeger for the tip
    glReadPixels((GLint)0, (GLint)0,
                 (GLint)w, (GLint)h,
                 GL_BGR_EXT, GL_UNSIGNED_BYTE, dataBuffer);

    //Now the file creation
    FILE *filePtr = fopen(fileName.c_str(), "wb");
    if (!filePtr) return false;


    unsigned char TGAheader[12] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    unsigned char header[6] = { (unsigned char)(w % 256), (unsigned char)(w / 256),
                                (unsigned char)(h % 256), (unsigned char)(h / 256),
                                24, 0
                              };
    // We write the headers
    fwrite(TGAheader, sizeof(unsigned char), 12, filePtr);
    fwrite(header, sizeof(unsigned char), 6, filePtr);
    // And finally our image data
    fwrite(dataBuffer, sizeof(GLubyte), nSize, filePtr);
    fclose(filePtr);

    free(dataBuffer);

    return true;
}

static void NextCloudLayer()
{
    static bool firstTime = true;

    if (firstTime) {
        if (layers.size() > 0) {
            layers[0]->SetEnabled(true);
        }
        if (layers.size() > 1) {
            layers[1]->SetEnabled(true);
        }
        firstTime = false;
        return;
    }

    atm->GetConditions()->SetVisibility(kVisibility * 10);

    static int currLayer = -1;

    if (currLayer == -1) {
        for (size_t i = 0; i < layers.size(); ++i) {
            layers[i]->SetEnabled(false);
        }
        if (layers.size()>0) {
            currLayer = 0;
        }
    } else {
        layers[currLayer]->SetEnabled(false);
        currLayer = (currLayer + 1) % layers.size();
    }
    if (currLayer != -1) {
        layers[currLayer]->SetEnabled(true);
    }
}

static void NextPrecipitationType()
{
    static CloudLayer::PrecipitationTypes precipitationType = CloudLayer::NONE;
    precipitationType = (CloudLayer::PrecipitationTypes)((precipitationType + 1) % CloudLayer::NUM_PRECIP_TYPES);

    // clear any existing ones
    atm->GetConditions()->SetPrecipitation(CloudLayer::NONE, 0);
    // set the current one
    atm->GetConditions()->SetPrecipitation(precipitationType, 15.0);
}


static std::string typeToString(int type)
{
    if (type == CIRROCUMULUS) return "CirroCumulusCloudLayer";
    if (type == CUMULUS_MEDIOCRIS) return "CumulusMediocrisCloudLayer";
    if (type == CUMULUS_CONGESTUS) return "CumulusCongestusCloudLayer";
    if (type == CUMULUS_CONGESTUS_HI_RES) return "CumulusCongestusCloudLayer";
    if (type == SANDSTORM) return "SANDSTORM(CumulusCongestusCloudLayer)";
    if (type == CUMULONIMBUS_CAPPILATUS) return "CumulonimbusCloudLayer";
    if (type == CIRRUS_FIBRATUS) return "CirrusCloudLayer";
    if (type == STRATUS) return "StratusCloudLayer";
    if (type == STRATOCUMULUS_PARTICLES) return "StratocumulusParticleCloudLayer";
    if (type == STRATOCUMULUS) return "StratocumulusCloudLayer";
    if (type == TOWERING_CUMULUS) return "ToweringCumulusCloudLayer";
    return "UNKNOWN";
}
static void AddLayer(CloudLayer* layer)
{
    layers.push_back(layer);
    atm->GetConditions()->AddCloudLayer(layer);
    layer->SetName(typeToString(layer->GetType()).c_str());
    layer->SetEnabled(false);
}

std::string makeStringEnabledLayers(void)
{
    std::string enabledLayersString;
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->GetEnabled()) {
            if (enabledLayersString.empty()==false) {
                enabledLayersString += "And";
            }
            enabledLayersString += std::string(layers[i]->GetName());
        }
    }
    return enabledLayersString;
}

// Configure high cirrus clouds.
static void SetupCirrusClouds()
{
    CloudLayer *cirrusCloudLayer;

    cirrusCloudLayer = CloudLayerFactory::Create(CIRRUS_FIBRATUS, *atm);
    cirrusCloudLayer->SetBaseAltitude(6000);
    cirrusCloudLayer->SetThickness(0);
    cirrusCloudLayer->SetBaseLength(100000);
    cirrusCloudLayer->SetBaseWidth(100000);
    cirrusCloudLayer->SetLayerPosition(0, 0);
    cirrusCloudLayer->SeedClouds(*atm);

    AddLayer(cirrusCloudLayer);
}

// Add a cumulus congestus deck with 80% sky coverage.
static void SetupCumulusCongestusClouds()
{
    CloudLayer *cumulusCongestusLayer;

    cumulusCongestusLayer = CloudLayerFactory::Create(CUMULUS_CONGESTUS_HI_RES, *atm);
    cumulusCongestusLayer->SetIsInfinite(true);
    cumulusCongestusLayer->SetBaseAltitude(1500);
    cumulusCongestusLayer->SetThickness(100);
    cumulusCongestusLayer->SetBaseLength(30000);
    cumulusCongestusLayer->SetBaseWidth(30000);
    cumulusCongestusLayer->SetDensity(0.8);
    cumulusCongestusLayer->SetLayerPosition(0, 0);
    cumulusCongestusLayer->SetCloudAnimationEffects(0.2, false);
    cumulusCongestusLayer->SeedClouds(*atm);
    cumulusCongestusLayer->SetAlpha(0.5);
    cumulusCongestusLayer->SetFadeTowardEdges(true);

    AddLayer(cumulusCongestusLayer);
}

// Sets up a solid stratus deck.
static void SetupStratusClouds()
{
    CloudLayer *stratusLayer;

    stratusLayer = CloudLayerFactory::Create(STRATUS, *atm);
    stratusLayer->SetIsInfinite(true);
    stratusLayer->SetBaseAltitude(1000);
    stratusLayer->SetThickness(600);
    stratusLayer->SetDensity(1.0);
    stratusLayer->SetLayerPosition(0, 0);
    stratusLayer->SeedClouds(*atm);

    AddLayer(stratusLayer);
}

// A thunderhead; note a Cumulonimbus cloud layer contains a single cloud.
static void SetupCumulonimbusClouds()
{
    CloudLayer *cumulonimbusLayer;

    cumulonimbusLayer = CloudLayerFactory::Create(CUMULONIMBUS_CAPPILATUS, *atm);
    cumulonimbusLayer->SetBaseAltitude(1000);
    cumulonimbusLayer->SetThickness(3000);
    cumulonimbusLayer->SetBaseLength(3000);
    cumulonimbusLayer->SetBaseWidth(5000);
    cumulonimbusLayer->SetLayerPosition(0, -10000);
    cumulonimbusLayer->SeedClouds(*atm);

    AddLayer(cumulonimbusLayer);
}

// Cumulus mediocris are little, puffy clouds. Keep the density low for realism, otherwise
// you'll have a LOT of clouds because they are small.
static void SetupCumulusMediocrisClouds()
{
    CloudLayer *cumulusMediocrisLayer;

    cumulusMediocrisLayer = CloudLayerFactory::Create(CUMULUS_MEDIOCRIS, *atm);
    cumulusMediocrisLayer->SetIsInfinite(true);
    cumulusMediocrisLayer->SetBaseAltitude(1000);
    cumulusMediocrisLayer->SetThickness(100);
    cumulusMediocrisLayer->SetBaseLength(20000);
    cumulusMediocrisLayer->SetBaseWidth(20000);
    cumulusMediocrisLayer->SetDensity(0.2);
    cumulusMediocrisLayer->SetLayerPosition(0, 0);
    cumulusMediocrisLayer->SeedClouds(*atm);

    AddLayer(cumulusMediocrisLayer);
}

// Stratocumulus clouds are rendered with GPU ray-casting. On systems that can support it
// (Shader model 3.0+) this enables very dense cloud layers with per-fragment lighting.
static void SetupStratocumulusClouds()
{
    CloudLayer *stratocumulusLayer;

    stratocumulusLayer = CloudLayerFactory::Create(STRATOCUMULUS, *atm);
    stratocumulusLayer->SetBaseAltitude(1000);
    stratocumulusLayer->SetThickness(3000);
    stratocumulusLayer->SetBaseLength(30000);
    stratocumulusLayer->SetBaseWidth(30000);
    stratocumulusLayer->SetDensity(0.5);
    stratocumulusLayer->SetIsInfinite(true);
    stratocumulusLayer->SetAlpha(1.0);
    stratocumulusLayer->SetFadeTowardEdges(true);
    stratocumulusLayer->SetLayerPosition(0, 0);
    stratocumulusLayer->SeedClouds(*atm);

    AddLayer(stratocumulusLayer);
}

static void SetupStratocumulusParticles()
{
    CloudLayer* stratocumulusParticlesLayer = CloudLayerFactory::Create(STRATOCUMULUS_PARTICLES, *atm);
    stratocumulusParticlesLayer->SetBaseAltitude(1000);
    stratocumulusParticlesLayer->SetThickness(3000);
    stratocumulusParticlesLayer->SetBaseLength(30000);
    stratocumulusParticlesLayer->SetBaseWidth(30000);
    stratocumulusParticlesLayer->SetDensity(0.5);
    stratocumulusParticlesLayer->SetIsInfinite(true);
    stratocumulusParticlesLayer->SetAlpha(1.0);
    stratocumulusParticlesLayer->SetFadeTowardEdges(true);
    stratocumulusParticlesLayer->SetLayerPosition(0, 0);
    stratocumulusParticlesLayer->SeedClouds(*atm);

    AddLayer(stratocumulusParticlesLayer);
}

// Sandstorms should be positioned at ground level. There is no need to set their
// density or thickness.
static void SetupSandstorm()
{
    CloudLayer *sandstormLayer;

    sandstormLayer = CloudLayerFactory::Create(SANDSTORM, *atm);
    sandstormLayer->SetIsInfinite(false);
    sandstormLayer->SetLayerPosition(0, -25000);
    sandstormLayer->SetBaseAltitude(0);
    sandstormLayer->SetBaseLength(50000);
    sandstormLayer->SetBaseWidth(50000);
    sandstormLayer->SeedClouds(*atm);

    AddLayer(sandstormLayer);
}

static void SetupCirroCumulusClouds()
{
    CloudLayer *cirroCumulusCloudsLayer;

    cirroCumulusCloudsLayer = CloudLayerFactory::Create(CIRROCUMULUS, *atm);
    cirroCumulusCloudsLayer->SetBaseAltitude(6000);
    cirroCumulusCloudsLayer->SetThickness(0);
    cirroCumulusCloudsLayer->SetBaseLength(100000);
    cirroCumulusCloudsLayer->SetBaseWidth(100000);
    cirroCumulusCloudsLayer->SetLayerPosition(0, 0);
    cirroCumulusCloudsLayer->SeedClouds(*atm);
    cirroCumulusCloudsLayer->SetCloudAnimationEffects(0.1, true);
    cirroCumulusCloudsLayer->SetFadeTowardEdges(true);
    cirroCumulusCloudsLayer->SetIsInfinite(true);

    AddLayer(cirroCumulusCloudsLayer);
}

#ifdef _WIN32
#include <windows.h>
# ifndef WGL_EXT_swap_control
typedef int (APIENTRY * PFNWGLSWAPINTERVALEXTPROC)(int);
# endif
#endif

// Enable / disable vsync for framerate measurements (use the novsync command line argument.)
void SetVSync(int interval)
{
#ifdef _WIN32
    printf("Disabling vsync...\n");
#if defined(__APPLE__)
    return;
#elif defined(_WIN32)
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT =
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(interval);
    }
#else
    PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI =
        (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalSGI");
    if (glXSwapIntervalSGI) {
        glXSwapIntervalSGI(interval);
    }
#endif
#endif
}

// Configure SilverLining for the desired wind, clouds, and visibility.
static void SetupAtmosphericConditions()
{
    assert(atm);

    // Set up the desired cloud types.
    SetupCirrusClouds();
    SetupCumulusCongestusClouds();
    SetupStratusClouds();
    SetupCumulonimbusClouds();
    SetupCumulusMediocrisClouds();
    SetupStratocumulusClouds();
    SetupStratocumulusParticles();
    SetupCirroCumulusClouds();
    //SetupSandstorm();

    NextCloudLayer();

    // Set up wind blowing northeast at 50 meters/sec
    WindVolume wv;
    wv.SetDirection(225);
    wv.SetMinAltitude(0);
    wv.SetMaxAltitude(10000);
    wv.SetWindSpeed(50 * windMultiplier);
    atm->GetConditions()->SetWind(wv);

    // Set visibility
    atm->GetConditions()->SetVisibility(kVisibility);

    SetVSync(0);
}

// For consistently lit scenes, query SilverLining for lighting information based on its
// internal lighting model. This will give you tone-mapped lighting based on the
// current simulated sun and moon positions and cloud cover.
void SetSceneLighting()
{
    float x, y, z, r, g, b, ra, ga, ba;
    atm->GetSunOrMoonPosition(&x, &y, &z);
    atm->GetSunOrMoonColor(&r, &g, &b);
    atm->GetAmbientColor(&ra, &ga, &ba);

    // Pass these values into your own shaders for lighting your scene.
}

// For effects inside stratus decks, it's important to honor any requests from SilverLining
// for fog. SilverLining can also provide guidance for a good fog color to use to blend
// distant terrain into the sky.
void SetSceneFog()
{
    // Need to light the fog, or it will glow in the dark...
    float ar, ag, ab;
    atm->GetSunOrMoonColor(&ar, &ag, &ab);

    if (atm->GetFogEnabled()) {
        float density, r, g, b;
        atm->GetFogSettings(&density, &r, &g, &b);

        // Tell your shaders to fog the scene accordingly...
    } else {
        GLfloat fogColor[4];
        atm->GetHorizonColor(yaw, &fogColor[0], &fogColor[1], &fogColor[2]);

        float density = 1.0f / kVisibility;

        // Decrease fog density with altitude, to avoid fog effects through the vacuum of space.
        static const double H = 8435.0; // Pressure scale height of Earth's atmosphere
        double isothermalEffect = exp(-(atm->GetConditions()->GetLocation().GetAltitude() / H));
        if (isothermalEffect <= 0) isothermalEffect = 1E-9;
        if (isothermalEffect > 1.0) isothermalEffect = 1.0;
        density *= (float)isothermalEffect;

        // Tell your shaders to fog the scene accordingly...
    }
}

static void ShutDown()
{
    atm->GetConditions()->RemoveAllCloudLayers();
    SL_DELETE atm;
}

// Various matrix manipulation utilities to avoid use of fixed-function OpenGL matrix code
void BuildPerspProjMat(double* m, double fov, double aspect, double znear, double zfar)
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

void BuildIdentityMat(double* m)
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

static void translate(GLdouble* m, GLdouble x, GLdouble y, GLdouble z)
{
    m[12] = m[0] * x + m[4] * y + m[8] * z + m[12];
    m[13] = m[1] * x + m[5] * y + m[9] * z + m[13];
    m[14] = m[2] * x + m[6] * y + m[10] * z + m[14];
    m[15] = m[3] * x + m[7] * y + m[11] * z + m[15];
}

#define RADIANS(x) ( (x) * (3.14159265 / 180.0) )
static void applyRot(GLdouble* m, GLdouble pitch, GLdouble yaw, GLdouble roll)
{
    double A = RADIANS(pitch);
    double B = RADIANS(yaw);
    double C = RADIANS(roll);

    m[0] = cos(B) * cos(C);
    m[1] = -cos(B) * sin(C);
    m[2] = sin(B);
    m[3] = 0;
    m[4] = sin(A) * sin(B) * cos(C) + cos(A) * sin(C);
    m[5] = -sin(A) * sin(B) * sin(C) + cos(A) * cos(C);
    m[6] = -sin(A) * cos(B);
    m[7] = 0;
    m[8] = -cos(A) * sin(B) * cos(C) + sin(A) * sin(C);
    m[9] = cos(A) * sin(B) * sin(C) + sin(A) * cos(C);
    m[10] = cos(A) * cos(B);
    m[11] = 0;
    m[12] = 0;
    m[13] = 0;
    m[14] = 0;
    m[15] = 1;
}

// The main rendering loop! Note how SilverLining is integrated here.
void Display()
{
    if (autoTest) {
        static int frameNumber = 0;
        if (frameNumber < 300) {
            ++frameNumber;
        } else {
            NextCloudLayer();
            frameNumber = 0;
        }
    }

    // Pass in the view and projection matrices to SilverLining.
    double mv[16], proj[16];
    BuildPerspProjMat(proj, 30.0, aspectRatio, 2, 100000);
    BuildIdentityMat(mv);
    applyRot(mv, -pitch, -yaw, 0);
    translate(mv, 0, -100, translation);

    // Pass in the view and projection matrices to SilverLining.
    atm->SetCameraMatrix(mv);
    atm->SetProjectionMatrix(proj);

    // After setting up your projection and modelview matrices to reflect the current
    // camera position, call Atmosphere::DrawSky() to draw the sky and do the lighting
    // pass on the clouds, if necessary.
    atm->DrawSky(true);

    // Now, do all your own drawing...
    SetSceneLighting();
    SetSceneFog();

    // When you're done, call Atmosphere::DrawObjects() to draw all the clouds from back to front.
    atm->DrawObjects(true, true
                     , true, 0.9f, false
                     , 0, true, true
                     , drawLightning, false);

    if (autoTest) {
        static int screenshotId = -1;
        static int framesFinishedForCurrentScreenShot = 0;
        const int waitForFrames = 10;
        const int numOfScreenShots = 10;
        if (screenshotId != -1) {
            if (screenshotId < numOfScreenShots && framesFinishedForCurrentScreenShot == waitForFrames) {
                std::string dir = "..\\..\\autotest\\gl32\\";
                std::stringstream ss;
                ss << (screenshotId)+1;
                if (autoTestSavingGoldenImages) {
                    ss << ".1";
                }
                ss << ".";
                ss << makeStringEnabledLayers();
                ss << ".tga";
                SaveScreenShot(dir + ss.str());
                NextCloudLayer();
                framesFinishedForCurrentScreenShot = 0;
                ++screenshotId;
            }
            if (screenshotId == numOfScreenShots) {
                ShutDown();
                exit(1);
            }
        } else {
            ++screenshotId;
        }

        ++framesFinishedForCurrentScreenShot;
    }

    // Now swap the back and front buffers.
    glutSwapBuffers();
    glutPostRedisplay();
}

// Handle the user resizing the window.
void Reshape(int x, int y)
{
    aspectRatio = (float)x / (float)y;

    glViewport(0, 0, x, y);
}

void AdvanceTime(long delta)
{
    SilverLining::LocalTime lt = atm->GetConditions()->GetTime();
    lt.AddSeconds(delta);
    atm->GetConditions()->SetTime(lt);
}

void DoAction(Action action)
{
    if (action == PITCH_UP) {
        pitch = pitch - 1;
    } else if (action == PITCH_DOWN) {
        pitch = pitch + 1;
    } else if (action == YAW_LEFT) {
        yaw = yaw - 1;
    } else if (action == YAW_RIGHT) {
        yaw = yaw + 1;
    } else if (action == MOVE_FORWARD) {
        translation += 100;
    } else if (action == MOVE_BACK) {
        translation -= 100;
    } else if (action == ADVANCE_TIME) {
        AdvanceTime(600);
    } else if (action == UNADVANCE_TIME) {
        AdvanceTime(-600);
    } else if (action == NEXT_CLOUD_LAYER) {
        NextCloudLayer();
    } else if (action == NEXT_PRECIPITATION_TYPE) {
        NextPrecipitationType();
    }
}

void KeyBoardSpecial(int key, int x, int y)
{
    if (key == GLUT_KEY_UP) {
        pitch = pitch - 1;
    }
    if (key == GLUT_KEY_DOWN) {
        pitch = pitch + 1;
    }
    if (key == GLUT_KEY_LEFT) {
        yaw = yaw - 1;
    }
    if (key == GLUT_KEY_RIGHT) {
        yaw = yaw + 1;
    }
}

void KeyBoard(unsigned char key, int x, int y)
{
    if (key == 'n') {
        NextCloudLayer();
    }
    if (key == 'p') {
        NextPrecipitationType();
    }
    if (key == 'a') {
        AdvanceTime(600);
    }
    if (key == 'd') {
        AdvanceTime(-600);
    }

    if (key == 'f') {
        translation += 100;
    }
    if (key == 'b') {
        translation -= 100;
    }
    if (key == 27) {
        ShutDown();
        glutDestroyWindow(windowHandle);
        exit(0);
    }
}

// Initialize glut and the ground texture.
void SetupOpenGL()
{
#ifdef USE_FREEGLUT
    glutInitContextVersion(4,6);
    glutInitContextProfile(GLUT_CORE_PROFILE);
#endif
    glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(window_width, window_height);
    aspectRatio = 4.0 / 3.0;

    windowHandle = glutCreateWindow("SilverLining OpenGL 32 Example");

    glutDisplayFunc(Display);
    glutReshapeFunc(Reshape);
    glutKeyboardFunc(KeyBoard);
    glutSpecialFunc(KeyBoardSpecial);
}

// Sets the simulated location and local time.
// Note, it's important that your longitude in the Location agrees with
// the time zone in the LocalTime.
void SetTimeAndLocation()
{
    Location loc;
    loc.SetLatitude(45);
    loc.SetLongitude(-122);

    LocalTime tm;
    tm.SetYear(1971);
    tm.SetMonth(8);
    tm.SetDay(5);
    tm.SetHour(6);
    tm.SetMinutes(30);
    tm.SetSeconds(0);
    tm.SetObservingDaylightSavingsTime(true);
    tm.SetTimeZone(PST);

    atm->GetConditions()->SetTime(tm);
    atm->GetConditions()->SetLocation(loc);
}

// Main entry point:
#ifdef WIN32
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char *argv[])
#endif
{
    if (argc >= 2) {
        std::string strArg1 = argv[1];
        if (strArg1 == "-autoTest") {
            autoTest = true;
            windMultiplier = 0.0f;
            drawLightning = false;
            window_width = 1424;
            window_height = 704;
        }
    }
    if (argc == 3) {
        std::string strArg2 = argv[2];
        if (strArg2 == "-autoTestSavingGoldenImages") {
            autoTestSavingGoldenImages = true;
        }
    }

    // If you want different clouds to be generated every time, remember to seed the
    // random number generator.
    if (autoTest) {
        srand(0);
    } else {
        srand((unsigned int)(time(NULL)));
    }

    glutInit(&argc, argv);

    // Set up GLUT
    SetupOpenGL();

    // Instantiate an Atmosphere object. Substitute your own purchased license name and code here.
    atm = new Atmosphere(SILVERLINING_LICENSE_USER, SILVERLINING_LICENSE_CODE);

    // Tell SilverLining we're rendering in OpenGL and the Resources directory is 2 directories
    // above the working directory.
    int err;

#ifdef WIN32
    err = atm->Initialize(Atmosphere::OPENGL32CORE, "..\\..\\Resources\\", true, 0);
#else
    err = atm->Initialize(Atmosphere::OPENGL32CORE, "../../Resources/", true, 0);
#endif

    if (autoTest) {
        std::stringstream ss;
        ss << 0.0;
        atm->SetConfigOption("virga-drift-speed", ss.str().c_str());
        atm->SetConfigOption("billboard-spin-enabled-override-disabled", "yes");
        atm->SetConfigOption("cumulonimbus-rain-texture", "none");
    }

    if (err == Atmosphere::E_NOERROR) {
        // Set your frame of reference (call this before setting up clouds!)
        atm->SetUpVector(0, 1, 0);
        atm->SetRightVector(1, 0, 0);
        atm->SetViewport(0, 0, window_width, window_height);

        // Set up all the clouds
        SetupAtmosphericConditions();

        // Configure where and when we want to be
        SetTimeAndLocation();

        // Start rendering.
        glutMainLoop();
    } else {
        printf("Error was %d\n", err);
    }
    return 0;
}

