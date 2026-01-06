// Copyright (c) 2004-2022  Sundog Software, LLC. All rights reserved worldwide.

/*
Sample app illustrating multi-threaded/multiple views rendering in SilverLining

There are 3 views:
-One 'main' view.
-Two smaller views drawn to a render to texture (e.g. a VR application
would render a left and right view into an rtt).

There are 3 different 'Draw Strategies':

-DrawImmediateSingleThreaded:
All views drawn, immediately, in a single thread.

-DrawDeferredSingleThreaded:
Each view has a corresponding deferred stream.

Each view is drawn into the deferred stream, in a single thread.

All streams are executed(i.e. the view drawn), in a single thread
, and hence, in a serialized fashion.

-DrawDeferredMultiThreaded
Each view has a corresponding deferred stream.

Each view is drawn into the deferred stream, in its own thread.

These threads don't have (or require) any OpenGL context.

All streams are executed(i.e. the view drawn), in the main thread, which is the only one
that has the OpenGL context.

*/

//-----------------------------------------------
//GLEW: Must be included first
//-----------------------------------------------
#include "GL/glew.h"

#include "SampleApp.h"
#include "SetUpLayers.h"
#include "SampleAssert.h"
#include "RenderToTexture.h"
#include "QuadRendering.h"
#include "MatrixUtils.h"

// includes for the drawing strategies
#include "DrawImmediateSingleThreaded.h"
#include "DrawDeferredSingleThreaded.h"
#include "DrawDeferredMultiThreaded.h"

#include "../SamplesCommon/Licenses.h"

#include "SampleAssert.h"

#include "SilverLining.h"

#include "GltfModel.h"
#include "Buffer.h"

#include <sstream>
#include <fstream>

static bool singleLayerOnly = false;

// For leak checking
//#define VLD_FORCE_ENABLE 1
// https://marketplace.visualstudio.com/items?itemName=ArkadyShapkin.VisualLeakDetectorforVisualC
//#include <vld.h>

using namespace SilverLining;

namespace Sample
{
void SampleApp::InitQuadStuff()
{
    myQuadRenderer = new QuadRenderer();
}

void SampleApp::ResetViews(void)
{
    for (int i = 0; i < myNumViews + 1; ++i) {
        myYaw[i] = 0;
        myPitch[i] = myInitialPitch;
        myTranslation[i] = 0;
    }

    myViewBeingControlled = -1;
}

SampleApp::SampleApp()
    : myWindMultiplier(1.0f)
    , myDrawLightning(true)
    , myDrawStrategyType(Draw_ImmediateSingleThreaded)
    , quit(false)
{
    myWindowWidth = 1424;
    myWindowHeight = 704;

    myViewWidth = 256;
    myViewHeight = 256;

    myCrepuscularIntensity = 0.9f;

    myTcsDatas.resize(myNumViews + 1);

    ResetViews();

    myRenderSmallViews = true;
    myDrawObjects = true;
    myDrawSky = true;

    myLayersContainer = new LayersContainer();

}

SampleApp::~SampleApp()
{
    delete myLayersContainer;
}

bool SampleApp::RenderSmallViews(void) const
{
    return myRenderSmallViews;
}
bool SampleApp::DrawSky(void) const
{
    return myDrawSky;
}
bool SampleApp::DrawObjects(void) const
{
    return myDrawObjects;
}


void SampleApp::Draw(double millis, int frame)
{
    myDrawStrategy->Draw(millis, frame);
}

void SampleApp::RenderSmallViewBegin(int view, double millis) const
{
    myRenderToTextures[view]->bind();

    glClearColor(0.0f, 0.1f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDepthFunc(GL_LEQUAL);
}

void SampleApp::RenderSmallViewEnd(int view, double millis) const
{
    myRenderToTextures[view]->unbind();
}

void SampleApp::RenderQuadWithShadowTexture(int x, int y, int width, int height, unsigned int shadowMap)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowMap);

    myQuadRenderer->RenderQuad(x, y, width, height, myWindowWidth, myWindowHeight);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void SampleApp::RenderQuad(int x, int y, int width, int height, int view)
{
    //assert(window_params_index!=-1&&viewport_width_index!=-1&&viewport_height_index!=-1);

    RenderToTexture* rtt = myRenderToTextures[view];

    glActiveTexture(GL_TEXTURE0);
    rtt->bindColorTex();

    myQuadRenderer->RenderQuad(x, y, width, height, myWindowWidth, myWindowHeight);

    rtt->unbindColorTex();
}

std::vector<RenderToTexture*>& SampleApp::RenderToTextures(void)
{
    return myRenderToTextures;
}

// For consistently lit scenes, query SilverLining for lighting information based on its
// internal lighting model. This will give you tone-mapped lighting based on the
// current simulated sun and moon positions and cloud cover.
void SampleApp::SetSceneLighting(int view, const SilverLining::ThreadCameraStreamData* tcsData)
{
    float x, y, z, r, g, b, ra, ga, ba;
    myAtmosphere->GetSunOrMoonPosition(&x, &y, &z);
    myAtmosphere->GetSunOrMoonColor(&r, &g, &b);
    myAtmosphere->GetAmbientColor(&ra, &ga, &ba);

    // Pass these values into your own shaders for lighting your scene.
}


// For effects inside stratus decks, it's important to honor any requests from SilverLining
// for fog. SilverLining can also provide guidance for a good fog color to use to blend
// distant terrain into the sky.
void SampleApp::SetSceneFog(int view, const SilverLining::ThreadCameraStreamData* tcsData)
{
    // Need to light the fog, or it will glow in the dark...
    float ar, ag, ab;
    myAtmosphere->GetSunOrMoonColor(&ar, &ag, &ab);

    if (myAtmosphere->GetFogEnabled()) {
        float density, r, g, b;
        myAtmosphere->GetFogSettings(&density, &r, &g, &b);

        // Tell your shaders to fog the scene accordingly...
    } else {
        GLfloat fogColor[4];
        myAtmosphere->GetHorizonColor(myYaw[view], &fogColor[0], &fogColor[1], &fogColor[2]);

        float density = 1.0f / myKVisibility;

        // Decrease fog density with altitude, to avoid fog effects through the vacuum of space.
        static const double H = 8435.0; // Pressure scale height of Earth's atmosphere
        double isothermalEffect = exp(-(myAtmosphere->GetConditions()->GetLocation((void*)tcsData).GetAltitude() / H));
        if (isothermalEffect <= 0) isothermalEffect = 1E-9;
        if (isothermalEffect > 1.0) isothermalEffect = 1.0;
        density *= (float)isothermalEffect;

        // Tell your shaders to fog the scene accordingly...
    }
}

void SampleApp::NextCloudLayer()
{
    static bool firstTime = true;

    const std::vector<SilverLining::CloudLayer*>& layers = myLayersContainer->GetLayers();

    if (firstTime && !singleLayerOnly) {

        for (int i = 0; i < myNumViews + 1; ++i) {
            const Camera* camera = myTcsDatas[i]->GetCamera();
            if (layers.size() > 0) {
                layers[0]->SetEnabled(true, 0, myTcsDatas[i]);
            }
            if (layers.size() > 1) {
                layers[1]->SetEnabled(true, 0, myTcsDatas[i]);
            }
        }

        firstTime = false;
        return;
    }

    static int currLayer = -1;

    if (currLayer == -1) {
        for (int i = 0; i < myNumViews + 1; ++i) {
            const Camera* camera = myTcsDatas[i]->GetCamera();;
            for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
                layers[layerIndex]->SetEnabled(false, 0, myTcsDatas[i]);
            }
        }
        if (layers.size() > 0) {
            currLayer = 0;
        }
    } else {
        for (int i = 0; i < myNumViews + 1; ++i) {
            layers[currLayer]->SetEnabled(false, 0, myTcsDatas[i]);
        }

        currLayer = (currLayer + 1) % layers.size();
    }

    if (currLayer != -1) {
        for (int i = 0; i < myNumViews + 1; ++i) {
            layers[currLayer]->SetEnabled(true, 0, myTcsDatas[i]);
        }
    }
}

std::string toString(CloudLayer::PrecipitationTypes precipitationType)
{
    if (precipitationType == CloudLayer::NONE) return "none";
    if (precipitationType == CloudLayer::RAIN) return "rain";
    if (precipitationType == CloudLayer::DRY_SNOW) return "dry snow";
    if (precipitationType == CloudLayer::WET_SNOW) return "wet snow";
    if (precipitationType == CloudLayer::SLEET) return "sleet";

    return "unknown";
}

void SampleApp::NextPrecipitationType()
{
    static CloudLayer::PrecipitationTypes precipitationType = CloudLayer::NONE;
    precipitationType = (CloudLayer::PrecipitationTypes)((precipitationType + 1) % CloudLayer::NUM_PRECIP_TYPES);

    std::cout << "precipitation: " << toString(precipitationType) << std::endl;

    // clear any existing ones
    if (myViewBeingControlled == -1) {
        for (int i = 0; i < myNumViews + 1; ++i) {
            myAtmosphere->GetConditions()->SetPrecipitation(CloudLayer::NONE, 0, -1, -1, false, myTcsDatas[i]);
            myAtmosphere->GetConditions()->SetPrecipitation(precipitationType, 15.0, -1, -1, false, myTcsDatas[i]);
        }
    } else {
        myAtmosphere->GetConditions()->SetPrecipitation(CloudLayer::NONE, 0, -1, -1, false, myTcsDatas[myViewBeingControlled]);
        myAtmosphere->GetConditions()->SetPrecipitation(precipitationType, 15.0, -1, -1, false, myTcsDatas[myViewBeingControlled]);
    }
}

void SampleApp::SetupAtmosphericConditions()
{
    ASSERT_PREDICATE(myAtmosphere);


    for (int i = myNumViews; i >= 0; --i) {
        int reindex = (i == myNumViews) ? i : myNumViews - i - 1;
        bool create = (i == myNumViews);

        int layerIndex = 0;
        myAtmosphere->GetRandomNumberGenerator()->Reset();

        const std::vector<SilverLining::CloudLayer*>& layers = myLayersContainer->GetLayers();

        // Set up the desired cloud types.
        myLayersContainer->SetupCirrusFibratusClouds(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupCumulusCongestusClouds(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupStratusClouds(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupCumulonimbusClouds(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupCumulusMediocrisClouds(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupStratocumulusClouds(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupStratocumulusParticles(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupCirroCumulusClouds(myAtmosphere, create, myTcsDatas[reindex]);
        myLayersContainer->SetupSandstorm(myAtmosphere, create, myTcsDatas[reindex]);
    }


    NextCloudLayer();

    // Set up wind blowing NE at 50 meters/sec
    WindVolume wv;
    wv.SetDirection(225);
    wv.SetMinAltitude(0);
    wv.SetMaxAltitude(10000);
    wv.SetWindSpeed(50 * myWindMultiplier);
    myAtmosphere->GetConditions()->SetWind(wv);

    //NextPrecipitationType();
    // Set visibility
    myAtmosphere->GetConditions()->SetVisibility(myKVisibility);
}

static bool compiled(GLuint shader)
{
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLsizei log_length = 0;
        GLchar message[1024];
        glGetShaderInfoLog(shader, 1024, &log_length, message);

        std::string errMessage(message);
        std::cout << "Compilation Error:" << std::endl;
        std::cout << errMessage << std::endl;
        return false;
    }
    return true;
}

static bool linked(GLuint programId)
{
    GLint linked;
    glGetProgramiv(programId, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        int infologLength = 0;
        glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &infologLength);

        if (infologLength > 0) {
            char* infoLog = (char*)SL_MALLOC(infologLength);
            int charsWritten = 0;
            glGetProgramInfoLog(programId, infologLength, &charsWritten, infoLog);
            std::cout << infoLog << std::endl;
            SL_FREE(infoLog);
        }
        return false;
    }
    return true;
}

void SampleApp::Initialize(bool autoTest, DrawStrategyType drawStrategyType, int rendererType, bool renderSmallViews, const std::string& modelFileName
                           , bool generateEnvMap
                           , bool generateShadowMap
                           , float yOffset
                           , float initialPitch
                           , int initialHour
                           , int initialMinute)
{

    myInitialHour = initialHour;
    myInitialMinutes = initialMinute;

    InitQuadStuff();
    myDrawStrategyType = drawStrategyType;
    if (myDrawStrategyType == Draw_ImmediateSingleThreaded) {
        myDrawStrategy = new DrawImmediateSingleThreaded(*this);
    } else if (myDrawStrategyType == Draw_DeferredSingleThreaded) {
        myDrawStrategy = new DrawDeferredSingleThreaded(*this);
    } else if (myDrawStrategyType == Draw_DeferredMutiThreaded) {
        myDrawStrategy = new DrawDeferredMultiThreaded(*this);
    }

    if (autoTest) {
        myWindMultiplier = 0;
        myDrawLightning = false;
    }

    // Instantiate an Atmosphere object. Substitute your own purchased license name and code here.
    //Atmosphere::EnableHDR(true);
    myAtmosphere = SL_NEW Atmosphere(SILVERLINING_LICENSE_USER, SILVERLINING_LICENSE_CODE);

    // Tell SilverLining we're rendering in OpenGL and the Resources directory is 2 directories
    // above the working directory.
    int err;

#ifdef WIN32
    err = myAtmosphere->Initialize(rendererType, "..\\..\\Resources\\", true, 0);
#else
    err = myAtmosphere->Initialize(rendererType, "../../Resources/", true, 0);
#endif

#if 0
    stream = atmosphere->CreateStream(IMMEDIATE_STREAM);
#endif

    if (autoTest) {
        std::stringstream ss;
        ss << 0.0;
        myAtmosphere->SetConfigOption("virga-drift-speed", ss.str().c_str());
        myAtmosphere->SetConfigOption("billboard-spin-enabled-override-disabled", "yes");
        myAtmosphere->SetConfigOption("cumulonimbus-rain-texture", "none");
    }
    // when using any combination of deferred + multi-threaded, we set this to yes
    // so that OpenGL state is not pushed or popped.
    myAtmosphere->SetConfigOption("avoid-opengl-stalls", "yes");


    if (myDrawStrategyType != Draw_ImmediateSingleThreaded) {
        // occlusion query does not work yet with deferred
        myAtmosphere->SetConfigOption("lens-flare-disable-occlusion", "yes");

        // bindless does not work yet with deferred
        myAtmosphere->SetConfigOption("cumulus-draw-bindless-indirect", "no");
    }

    const bool deferred = myDrawStrategyType != Draw_ImmediateSingleThreaded;
    for (int i = 0; i < myNumViews; ++i) {
        myTcsDatas[i] = SL_NEW ThreadCameraStreamData(true, myAtmosphere->GetUnitScale(), deferred);
        Camera* camera = myTcsDatas[i]->GetCamera();
        camera->SetViewport(0, 0, myViewWidth, myViewHeight);
        camera->SetUpVector(0, 1, 0);
        camera->SetRightVector(1, 0, 0);
        camera->SetDepthRange(0, 1);
        std::stringstream ss;
        ss << "cam_view_" << i;
        camera->SetName(ss.str().c_str());

        bool avoidStalls = myAtmosphere->GetConfigOptionBoolean("avoid-opengl-stalls");

        myTcsDatas[i]->Initialize(rendererType, true, 0, avoidStalls);
        myTcsDatas[i]->Initialize(*myAtmosphere);
    }

    myTcsDatas[myNumViews] = SL_NEW ThreadCameraStreamData(true, myAtmosphere->GetUnitScale(), deferred);
    Camera* camera = myTcsDatas[myNumViews]->GetCamera();

    bool avoidStalls = myAtmosphere->GetConfigOptionBoolean("avoid-opengl-stalls");
    myTcsDatas[myNumViews]->Initialize(rendererType, true, 0, avoidStalls);
    myTcsDatas[myNumViews]->Initialize(*myAtmosphere);

    camera->SetName("Main View");
    camera->SetViewport(0, 0, myWindowWidth, myWindowHeight);
    camera->SetUpVector(0, 1, 0);
    camera->SetRightVector(1, 0, 0);
    camera->SetDepthRange(0, 1);
    int mainRT = 0;

    if (err == Atmosphere::E_NOERROR) {
        // Set your frame of reference (call this before setting up clouds!)

        // Set up all the clouds
        SetupAtmosphericConditions();

        // Configure where and when we want to be
        SetTimeAndLocation();

    } else {
        printf("Error was %d\n", err);
    }

    myRenderToTextures.resize(myNumViews);

    for (int i = 0; i < myNumViews; ++i) {
        // These rtts' fbos belong the main window context
        myRenderToTextures[i] = new RenderToTexture(myViewWidth, myViewHeight);

        SilverLining::Camera* camera = myTcsDatas[i]->GetCamera();
        camera->SetRenderTarget((myRenderToTextures[i]->GetFBO()));
    }

    myRenderSmallViews = renderSmallViews;

    if (!modelFileName.empty()) {
        myModel = new GltfModel();
        myModel->loadFromFile(modelFileName, GltfModel::PreTransformVertices);

        const int sizeOfVertexBuffer = (int)(myModel->vertexBuffer().size() * sizeof(Vertex));
        myModelVertexBufferGpu = new Buffer(sizeOfVertexBuffer, GL_ARRAY_BUFFER, myModel->vertexBuffer().data());

        const int sizeOfIndexBuffer = (int)(myModel->indexBuffer().size() * sizeof(uint32_t));
        myModelIndexBufferGpu = new Buffer(sizeOfIndexBuffer, GL_ELEMENT_ARRAY_BUFFER, myModel->indexBuffer().data());

        glGenVertexArrays(1, &myModelVao);
        glBindVertexArray(myModelVao);

        glBindBuffer(GL_ARRAY_BUFFER, myModelVertexBufferGpu->BufferId());

        glVertexAttribPointer(0, 3, GL_FLOAT, false, 48, (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, false, 48, (void*)(12));

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);

        std::ifstream vsStream("shaders\\rasterizationPath_vs.glsl");
        std::string srcVS((std::istreambuf_iterator<char>(vsStream)), std::istreambuf_iterator<char>());

        std::ifstream psStream("shaders\\rasterizationPath_ps.glsl");
        std::string srcPS((std::istreambuf_iterator<char>(psStream)), std::istreambuf_iterator<char>());

        const char* vs_src[1] = { srcVS.c_str() };
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, vs_src, NULL);
        glCompileShader(vs);
        ASSERT_PREDICATE(compiled(vs));

        const char* ps_src[1] = { srcPS.c_str() };
        GLuint ps = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(ps, 1, ps_src, NULL);
        glCompileShader(ps);
        ASSERT_PREDICATE(compiled(ps));

        myModelProgramId = glCreateProgram();
        glAttachShader(myModelProgramId, vs);
        glAttachShader(myModelProgramId, ps);

        glLinkProgram(myModelProgramId);

        GLint program_linked;
        glGetProgramiv(myModelProgramId, GL_LINK_STATUS, &program_linked);
        ASSERT_PREDICATE(linked(myModelProgramId));

        myModelMatrixIndex = glGetUniformLocation(myModelProgramId, "modelMatrix");;
        myViewMatrixIndex = glGetUniformLocation(myModelProgramId, "viewMatrix");;
        myProjectionMatrixIndex = glGetUniformLocation(myModelProgramId, "projectionMatrix");
        myEnvironmentMapIndex = glGetUniformLocation(myModelProgramId, "environmentMap");
        myCameraPositionIndex = glGetUniformLocation(myModelProgramId, "cameraPosition");
    }

    myGenerateEnvMap = generateEnvMap;
    myGenerateShadowMap = generateShadowMap;

    myYOffset = yOffset;
    myInitialPitch = initialPitch;
    ResetViews();
}

void SampleApp::ShutDown()
{
    for (int i = 0; i < myNumViews; ++i) {
        delete myRenderToTextures[i];
    }
    myRenderToTextures.clear();
    delete myDrawStrategy;

    delete myQuadRenderer;

    for (int i = 0; i <= myNumViews; i++) {
        myAtmosphere->GetConditions()->RemoveAllCloudLayers(myTcsDatas[i], false, true);
    }
    for (int i = 0; i <= myNumViews; ++i) {
        myTcsDatas[i]->DeInitialize(*myAtmosphere);
        SL_DELETE myTcsDatas[i];
    }
    myAtmosphere->GetConditions()->RemoveAllCloudLayers();
    SL_DELETE myAtmosphere;

    delete myModel;

    if (myModelProgramId != 0) {
        glDeleteProgram(myModelProgramId);
    }

    delete myModelVertexBufferGpu;
    delete myModelIndexBufferGpu;
    glDeleteVertexArrays(1, &myModelVao);
}

void SampleApp::DeleteLayer(int index)
{
    auto& layers = myLayersContainer->GetLayers();
    if (layers.empty() || index >= (int)layers.size()) {
        return;
    }
    auto layer = layers[index];
    for (int i = 0; i <= myNumViews; i++) {
        myAtmosphere->GetConditions()->RemoveCloudLayer(layer->GetHandle(), myTcsDatas[i], false, true);
    }

    myAtmosphere->GetConditions()->RemoveCloudLayer(layer->GetHandle());
    myLayersContainer->EraseLayerEntry(index);
}

void SampleApp::AdvanceTime(long delta)
{
    for (auto& tcsData : myTcsDatas) {
        SilverLining::LocalTime lt = myAtmosphere->GetConditions()->GetTime(tcsData);
        lt.AddSeconds(delta);
        std::cout << "Time is: " << lt << std::endl;
        myAtmosphere->GetConditions()->SetTime(lt, tcsData);
    }

}

// Sets the simulated location and local time.
// Note, it's important that your longitude in the Location agrees with
// the time zone in the LocalTime.
void SampleApp::SetTimeAndLocation()
{
    Location loc;
    loc.SetLatitude(45);
    loc.SetLongitude(-122);

    LocalTime tm;
    tm.SetYear(1971);
    tm.SetMonth(8);
    tm.SetDay(5);
    tm.SetHour(myInitialHour);
    tm.SetMinutes(myInitialMinutes);
    tm.SetSeconds(0);
    tm.SetObservingDaylightSavingsTime(true);
    tm.SetTimeZone(PST);

    myAtmosphere->GetConditions()->SetTime(tm);
    myAtmosphere->GetConditions()->SetLocation(loc);
    for (auto& tcsData : myTcsDatas) {
        myAtmosphere->GetConditions()->SetTime(tm, tcsData);
        myAtmosphere->GetConditions()->SetLocation(loc, tcsData);
    }
}

bool SampleApp::SaveScreenShot(const std::string& fileName)
{
    //This prevents the images getting padded
    // when the width multiplied by 3 is not a multiple of 4
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2];
    int h = vp[3];

    int nSize = w * h * 3;
    // First let's create our buffer, 3 channels per Pixel
    char* dataBuffer = (char*)malloc(nSize * sizeof(char));

    if (!dataBuffer) return false;

    // Let's fetch them from the backbuffer
    // We request the pixels in GL_BGR format, thanks to Berzeger for the tip
    glReadPixels((GLint)0, (GLint)0,
                 (GLint)w, (GLint)h,
                 GL_BGR_EXT, GL_UNSIGNED_BYTE, dataBuffer);

    //Now the file creation
    FILE* filePtr = fopen(fileName.c_str(), "wb");
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

std::string SampleApp::MakeStringEnabledLayers(void) const
{
    std::string enabledLayersString;
    const std::vector<SilverLining::CloudLayer*>& layers = myLayersContainer->GetLayers();
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->GetEnabled()) {
            if (enabledLayersString.empty() == false) {
                enabledLayersString += "And";
            }
            enabledLayersString += std::string(layers[i]->GetName());
        }
    }
    return enabledLayersString;
}

float SampleApp::GetYaw(int view) const
{
    return myYaw[view];
}

float SampleApp::GetPitch(int view) const
{
    return myPitch[view];
}

float SampleApp::GetRoll(int view) const
{
    return 0;
}


void SampleApp::GetModelViewMatrix(int view, double modelViewMatrix[16]) const
{
    double pitchMat[16];
    Rotate(pitchMat, toRadians(-myPitch[view]), 1, 0, 0);

    double yawMat[16];
    Rotate(yawMat, toRadians(-myYaw[view]), 0, 1, 0);

    MatrixMult(modelViewMatrix, yawMat, pitchMat);

    Translate(modelViewMatrix, 0, myYOffset, 0);
    Translate(modelViewMatrix, 0, 0, myTranslation[view]);
}

void SampleApp::GetProjectionMatrix(int view, double projectionMatrix[16]) const
{
    BuildPerspProjMat(projectionMatrix, 60.0, (float)myWindowWidth / (float)myWindowHeight, 2, 100000);
}

int SampleApp::GetWindowWidth(void) const
{
    return myWindowWidth;
}

int SampleApp::GetWindowHeight(void) const
{
    return myWindowHeight;
}

void SampleApp::DoAction(Action action)
{
    if (action == PITCH_UP) {
        if (myViewBeingControlled == -1) {
            for (int i = 0; i < myNumViews + 1; ++i) {
                myPitch[i] = myPitch[i] - 1;
            }
        } else {
            myPitch[myViewBeingControlled] = myPitch[myViewBeingControlled] - 1;
        }
    } else if (action == PITCH_DOWN) {

        if (myViewBeingControlled == -1) {
            for (int i = 0; i < myNumViews + 1; ++i) {
                myPitch[i] = myPitch[i] + 1;
            }
        } else {
            myPitch[myViewBeingControlled] = myPitch[myViewBeingControlled] + 1;
        }

    }

    else if (action == YAW_LEFT) {

        if (myViewBeingControlled == -1) {
            for (int i = 0; i < myNumViews + 1; ++i) {
                myYaw[i] = myYaw[i] - 1;
            }
        } else {
            myYaw[myViewBeingControlled] = myYaw[myViewBeingControlled] - 1;
        }
    } else if (action == YAW_RIGHT) {

        if (myViewBeingControlled == -1) {
            for (int i = 0; i < myNumViews + 1; ++i) {
                myYaw[i] = myYaw[i] + 1;
            }
        } else {
            myYaw[myViewBeingControlled] = myYaw[myViewBeingControlled] + 1;
        }
    } else if (action == MOVE_FORWARD) {

        if (myViewBeingControlled == -1) {
            for (int i = 0; i < myNumViews + 1; ++i) {
                myTranslation[i] += 100;
            }
        } else {
            myTranslation[myViewBeingControlled] += 100;
        }
    } else if (action == MOVE_BACK) {

        if (myViewBeingControlled == -1) {
            for (int i = 0; i < myNumViews + 1; ++i) {
                myTranslation[i] -= 100;
            }
        } else {
            myTranslation[myViewBeingControlled] -= 100;
        }

    } else if (action == ADVANCE_TIME) {
        AdvanceTime(600);
    } else if (action == UNADVANCE_TIME) {
        AdvanceTime(-600);
    } else if (action == NEXT_CLOUD_LAYER) {
        NextCloudLayer();
        std::cout << MakeStringEnabledLayers() << std::endl;
    } else if (action == NEXT_PRECIPITATION_TYPE) {
        NextPrecipitationType();
    } else if (action == CONTROL_LEFT_SMALL_VIEW) {
        myViewBeingControlled = 0;
        std::cout << "controlling left small view..." << std::endl << std::endl;
    } else if (action == CONTROL_RIGHT_SMALL_VIEW) {
        myViewBeingControlled = 1;
        std::cout << "controlling right small view..." << std::endl << std::endl;
    } else if (action == CONTROL_MAIN_VIEW) {
        myViewBeingControlled = 2;
        std::cout << "controlling main view..." << std::endl << std::endl;
    } else if (action == RESET_VIEWS) {
        ResetViews();
        std::cout << "reset all views!" << std::endl << std::endl;
    } else if (action == DELETE_LAYER) {
        DeleteLayer(0);
    }
}

void SampleApp::SetWindowWidthAndHeight(int w, int h)
{
    myWindowWidth = w;
    myWindowHeight = h;
}

void SampleApp::SetSmallViewWidthAndHeight(int w, int h)
{
    myViewWidth = w;
    myViewHeight = h;
}

void SampleApp::DrawModel(unsigned int environmentMap) const
{
    if (myModel == nullptr) {
        return;
    }
    bool transpose = false;

    glm::mat4 modelMatrix = glm::mat4(1);
    if (myModelMatrixIndex != -1) {
        glProgramUniformMatrix4fv(myModelProgramId, myModelMatrixIndex, 1, transpose, &modelMatrix[0][0]);
    }

    glm::vec3 cameraPosition = glm::vec3(0, 0, 2.5);

    glm::mat4 viewMatrixglm = glm::lookAt(
                                  cameraPosition
                                  , glm::vec3(0, 0, 0)
                                  , glm::vec3(0, 1, 0)
                              );
    if (myViewMatrixIndex != -1) {
        glProgramUniformMatrix4fv(myModelProgramId, myViewMatrixIndex, 1, transpose, &viewMatrixglm[0][0]);
    }

    float aspect = (float)myWindowWidth / (float)myWindowHeight;
    glm::mat4 projectionMatrixglm = glm::perspective(glm::radians(60.0f), aspect, 2.0f, 100000.0f);
    if (myProjectionMatrixIndex != -1) {
        glProgramUniformMatrix4fv(myModelProgramId, myProjectionMatrixIndex, 1, transpose, &projectionMatrixglm[0][0]);
    }

    if (environmentMap != 0) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap);
        glProgramUniform1i(myModelProgramId, myEnvironmentMapIndex, 0);
    }

    if (myCameraPositionIndex != -1) {
        glProgramUniform3fv(myModelProgramId, myCameraPositionIndex, 1, &cameraPosition.x);
    }

    glUseProgram(myModelProgramId);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glBindVertexArray(myModelVao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, myModelIndexBufferGpu->BufferId());

    glDrawElements(GL_TRIANGLES, (uint32_t)myModel->indexBuffer().size(), GL_UNSIGNED_INT, NULL);

    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
}

