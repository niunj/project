// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

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

These are the keys for controlling the app:
-w -> move forward
-s -> move backward
-arrow up -> pitch up
-arrow down -> pitch down
-arrow left -> yaw left
-arrow right -> yaw right
-key 1, key 2 and key 3 when pressed, the subsequent movement keys above affect the main view, left small view and right small view respectively
-key r resets the views
-a & d -> move time forward and backward
-n -> cycles between various layers
-p -> cycles between precipitation types (rain, sleet, snow)

*/

// assert headers
#undef NDEBUG
#include <cassert>

//-----------------------------------------------
//GLEW: Must be included first
//-----------------------------------------------
#include "GL/glew.h"
//-----------------------------------------------

//-----------------------------------------------
//GLFW: Various platform-specific headers:
//-----------------------------------------------
#if defined(__APPLE__)
#include "GLFW/glfw3.h"
# include <sys/timeb.h>
#elif defined(_WIN32)
# include <windows.h>
#include "GLFW/glfw3.h"
# ifndef WGL_EXT_swap_control
typedef int (APIENTRY* PFNWGLSWAPINTERVALEXTPROC)(int);
# endif
#else
#include "GLFW/glfw3.h"
# include <GL/glx.h>
# include <sys/timeb.h>
# ifndef GLX_SGI_swap_control
typedef int(*PFNGLXSWAPINTERVALSGIPROC) (int interval);
# endif
#endif
//-----------------------------------------------


//-----------------------------------------------
// SilverLining includes
//-----------------------------------------------
#include "SilverLining.h"
//-----------------------------------------------

//-----------------------------------------------
// Sample includes
//-----------------------------------------------
#include "../SamplesCommon/SampleCommon.h"


#include "SampleApp.h"
#include "QuadRendering.h"
//-----------------------------------------------

#include <sstream>

#pragma comment(lib, "opengl32.lib")

// All SilverLining objects are in the SilverLining namespace.
using namespace SilverLining;

Sample::SampleApp sampleApp;

static bool autoTest = false;
static bool autoTestSavingGoldenImages = false;
static bool autoTestConditionsOnly = false;

// The main rendering loop! Note how SilverLining is integrated here.
bool DrawFrame(long int frameNumber)
{

#ifdef _WIN32
    static DWORD startMillis = 0;
    DWORD millis = timeGetTime();
    if (startMillis == 0) {
        startMillis = millis;
    }
    millis = millis - startMillis;
#else
    unsigned long millis;
    struct timeb tp;
    ftime(&tp);
    static unsigned long startMillis = 0;
    millis = tp.time * 1000 + tp.millitm;
    millis = millis - startMillis;
    if (startMillis == 0) {
        startMillis = millis;
        millis = 0;
    }
#endif

    sampleApp.Draw(millis, frameNumber);

    return true;
}

// Handle a window resize
void Resize(GLFWwindow* window, GLint width, GLint height)
{
    glViewport(0, 0, width, height);
}

Action KeyToAction(bool pressed, int key)
{
    if (pressed) {
        if (key == GLFW_KEY_UP) {
            return PITCH_UP;
        }
        if (key == GLFW_KEY_DOWN) {
            return PITCH_DOWN;
        }
        if (key == GLFW_KEY_LEFT) {
            return YAW_LEFT;
        }
        if (key == GLFW_KEY_RIGHT) {
            return YAW_RIGHT;
        }
        if (key == GLFW_KEY_W) {
            return MOVE_FORWARD;
        }
        if (key == GLFW_KEY_S) {
            return MOVE_BACK;
        }
        if (key == GLFW_KEY_N) {
            return NEXT_CLOUD_LAYER;
        }
        if (key == GLFW_KEY_T) {
            return ADVANCE_TIME;
        }
        if (key == GLFW_KEY_G) {
            return UNADVANCE_TIME;
        }
        if (key == GLFW_KEY_P) {
            return NEXT_PRECIPITATION_TYPE;
        }
        if (key == GLFW_KEY_F5) {
            sampleApp.SaveScreenShot("c:\\temp\\1.tga");
        }
        if (key == GLFW_KEY_1) {
            return CONTROL_MAIN_VIEW;
        }
        if (key == GLFW_KEY_2) {
            return CONTROL_LEFT_SMALL_VIEW;
        }
        if (key == GLFW_KEY_3) {
            return CONTROL_RIGHT_SMALL_VIEW;
        }
        if (key == GLFW_KEY_R) {
            return RESET_VIEWS;
        }
        if (key == GLFW_KEY_DELETE) {
            return DELETE_LAYER;
        }
    }

    return UNKNOWN;
}

void Keyboard(GLFWwindow* window, int key, int, int action, int)
{
    sampleApp.DoAction(KeyToAction(action == GLFW_PRESS || action == GLFW_REPEAT, key));
}


bool filterMessage(const GLchar* message)
{
    std::string strMessage(message);
    if (strMessage.find("Buffer detailed info") != std::string::npos) {
        return true;
    }
    return false;
}

class OpenGLDebugger
{
public:
    virtual void onDebugMessage(GLenum source,
                                GLenum type,
                                GLuint id,
                                GLenum severity,
                                GLsizei length,
                                const GLchar* message)
    {
        if (filterMessage(message)) {
            return;
        }

        std::cout << message << std::endl;
#ifdef _WIN32
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
#endif
    }
};

void APIENTRY debug_callback(GLenum source,
                             GLenum type,
                             GLuint id,
                             GLenum severity,
                             GLsizei length,
                             const GLchar* message,
                             GLvoid* userParam)
{
    reinterpret_cast<OpenGLDebugger*>(userParam)->onDebugMessage(source, type, id, severity, length, message);
}

// initialize glfw and glew
GLFWwindow* main_window_and_context = 0;

static OpenGLDebugger opengGLDebugger;
static const bool debugOpenGL = true;
int SetupOpenGL(const std::string& drawStrategy, const std::string& renderer)
{
    if (!glfwInit()) {
        std::cerr << "Unable to initialize glfw!" << std::endl;
        return 0;
    }

    std::string title = "SilverLining OpenGL Multi Threaded/Multiple Views Sample, " + drawStrategy + ", renderer: " + renderer;

    main_window_and_context = glfwCreateWindow(sampleApp.GetWindowWidth(), sampleApp.GetWindowHeight(), title.c_str(), NULL, NULL);
    if (!main_window_and_context) {
        std::cerr << "Failed to create GLFWwindow!" << std::endl;
        return 0;
    }

    glfwMakeContextCurrent(main_window_and_context);
    int32_t major = glfwGetWindowAttrib(main_window_and_context, GLFW_CONTEXT_VERSION_MAJOR);
    int32_t minor = glfwGetWindowAttrib(main_window_and_context, GLFW_CONTEXT_VERSION_MINOR);

    std::cout << "OpenGL version that GLFW set up is: OpenGL " << major << "." << minor << std::endl;

    glfwSetWindowSizeCallback(main_window_and_context, Resize);
    glfwSetKeyCallback(main_window_and_context, Keyboard);

    GLenum glewInitialized = glewInit();
    if (glewInitialized != GLEW_OK) {
        std::cerr << "Could not initialize GLEW!" << std::endl;
        return 0;
    }

    if (debugOpenGL) {
        glDebugMessageCallback((GLDEBUGPROC)debug_callback, &opengGLDebugger);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }

    return 1;
}

// Enable / disable vsync for framerate measurements (use the novsync command line argument.)
void SetVSync(int interval)
{
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
}

static bool isParam(int argc, char* argv[], const std::string& param)
{
    for (int i = 1; i < argc; ++i) {
        std::string strArg = argv[i];
        if (strArg == param) {
            return true;
        }
    }
    return false;
}

// Main entry point:

static int GetRendererType(int argc, char* argv[], const std::string& rendererParam)
{
    for (int i = 1; i < argc; ++i) {
        std::string strArg = argv[i];
        if (strArg == rendererParam && i + 1 < argc) {
            std::string renderer(argv[i + 1]);
            if (renderer.find("32") == std::string::npos) {
                return Atmosphere::OPENGL;
            }
        }
    }
    return Atmosphere::OPENGL32CORE;
}

static void GetWindowWidthAndHeight(int& windowWidth, int& windowHeight, int argc, char* argv[], const std::string& widthParam, const std::string& heightParam)
{
    windowWidth = -1;
    windowHeight = -1;

    bool widthSet = false;
    bool heightSet = false;
    for (int i = 1; i < argc; ++i) {
        std::string strArg = argv[i];
        if (strArg == widthParam && i + 1 < argc) {
            std::stringstream ss(argv[i + 1]);
            ss >> windowWidth;
        }
        if (strArg == heightParam && i + 1 < argc) {
            std::stringstream ss(argv[i + 1]);
            ss >> windowHeight;
        }
    }
}

bool GetBoolean(int argc, char* argv[], const std::string& param, bool defaultVal)
{
    bool val = defaultVal;
    for (int i = 1; i < argc; ++i) {
        std::string strArg = argv[i];
        if (strArg == param && i + 1 < argc) {
            std::stringstream ss(argv[i + 1]);
            ss >> val;
        }
    }

    return val;
}

template <class T>
T GetVal(int argc, char* argv[], const std::string& param, T defaultValue)
{
    T val = defaultValue;
    for (int i = 1; i < argc; ++i) {
        std::string strArg = argv[i];
        if (strArg == param && i + 1 < argc) {
            std::stringstream ss(argv[i + 1]);
            ss >> val;
        }
    }

    return val;
}

float GetFloat(int argc, char* argv[], const std::string& param, float defaultValue)
{
    return GetVal<float>(argc, argv, param, defaultValue);
}

int GetInt(int argc, char* argv[], const std::string& param, int defaultValue)
{
    return GetVal<int>(argc, argv, param, defaultValue);
}

std::string GetModelFileName(int argc, char* argv[])
{
    std::string fileName;
    for (int i = 1; i < argc; ++i) {
        std::string strArg = argv[i];
        if (strArg == "--model" && i + 1 < argc) {
            std::stringstream ss(argv[i + 1]);
            ss >> fileName;
        }
    }

    fileName = "models\\" + fileName;

    return fileName;
}

// command line arguments examples:

// example 1:
// -1 -w 2848 -h 1408 -sw 512 -sh 512
// will draw deferred single threaded with window width of 2848 x 1408 and small view window width, height of 512x512 (resized intrinsically
// by the aspect ratio of the main window), using the OpenGL 32 renderer

// example 2:
// -2 - w 2848 - h 1408 - sw 512 - sh 512 - renderer gl
// will draw deferred multi threaded threaded with window width of 2848 x 1408 and small view window width, height of 512x512 (resized intrinsically
// by the aspect ratio of the main window), using the OpenGL renderer

int main(int argc, char* argv[])
{
    // -autoTest is for internal testing/generating images
    autoTest = isParam(argc, argv, "--autoTest");

    // -autoTestSavingGoldenImages is for internal testing/generating golden images
    autoTestSavingGoldenImages = isParam(argc, argv, "--autoTestSavingGoldenImages");

    autoTestConditionsOnly = isParam(argc, argv, "--autoTestConditionsOnly");

    Sample::DrawStrategyType drawStrategyType;

    std::string drawStrategy;
    if (isParam(argc, argv, "-1")) {
        // This will draw using DrawDeferredSingleThreaded draw strategy
        drawStrategyType = Sample::Draw_DeferredSingleThreaded;
        drawStrategy = "strategy: Draw_DeferredSingleThreaded";
        std::cout << drawStrategy << std::endl;

    } else if (isParam(argc, argv, "-2")) {
        // This will draw using DrawDeferredSingleThreaded draw strategy
        drawStrategyType = Sample::Draw_DeferredMutiThreaded;
        drawStrategy = "strategy: Draw_DeferredMutiThreaded";
        std::cout << drawStrategy << std::endl;
    } else {
        // With no command arguments specified we draw with immediate, single threaded
        drawStrategyType = Sample::Draw_ImmediateSingleThreaded;
        drawStrategy = "strategy: Draw_ImmediateSingleThreaded";
        std::cout << drawStrategy << std::endl;
    }

    int w, h;
    GetWindowWidthAndHeight(w, h, argc, argv, "-w", "-h");
    if (w != -1 && h != -1) {
        sampleApp.SetWindowWidthAndHeight(w, h);
    }
    GetWindowWidthAndHeight(w, h, argc, argv, "-sw", "-sh");
    if (w != -1 && h != -1) {
        sampleApp.SetSmallViewWidthAndHeight(w, h);
    }

    int rendererType = GetRendererType(argc, argv, "-renderer");
    std::string renderer = (rendererType == Atmosphere::OPENGL32CORE) ? "OpenGL 32" : "OpenGL";

    // If you want different clouds to be generated every time, remember to seed the
    // random number generator.

    srand(0);

    // set up GLFW and GLEW
    int ok = SetupOpenGL(drawStrategy, renderer);
    if (ok == 0) {
        std::cout << "OpenGL initialization failed!" << std::endl;
        return 0;
    }

    // Enable vertical sync if specified
    if (argc > 1 && strcmp(argv[1], "vsync") == 0) {
        SetVSync(1);
    } else {
        SetVSync(0);
    }

    sampleApp.Initialize(autoTest || autoTestConditionsOnly, drawStrategyType, rendererType
                         , GetBoolean(argc, argv, "--renderSmallViews", true)
                         , GetModelFileName(argc, argv)
                         , GetBoolean(argc, argv, "--slEnvironmentMap", false)
                         , GetBoolean(argc, argv, "--slShadowMap", false)
                         , GetFloat(argc, argv, "--yOffset", -100)
                         , GetFloat(argc, argv, "--initialPitch", -10)
                         , GetInt(argc, argv, "--hour", 6)
                         , GetInt(argc, argv, "--minute", 30)
                        );

    bool running = true;

    static long int frame = 0;

    do {
        DrawFrame(frame++);

        if (autoTest) {
            static int screenshotId = -1;
            static int framesFinishedForCurrentScreenShot = 0;
            const int waitForFrames = 10;
            const int numOfScreenShots = 10;
            if (screenshotId != -1) {
                if (screenshotId < numOfScreenShots && framesFinishedForCurrentScreenShot == waitForFrames) {
                    std::string dir;
                    if (drawStrategyType == Sample::Draw_ImmediateSingleThreaded) {
                        if (rendererType == Atmosphere::OPENGL32CORE) {
                            dir = "..\\..\\autotest\\multi\\";
                        } else {
                            dir = "..\\..\\autotest\\multi_gl\\";
                        }
                    } else if (drawStrategyType == Sample::Draw_DeferredSingleThreaded) {
                        if (rendererType == Atmosphere::OPENGL32CORE) {
                            dir = "..\\..\\autotest\\multi1\\";
                        } else {
                            dir = "..\\..\\autotest\\multi1_gl\\";
                        }
                    } else if (drawStrategyType == Sample::Draw_DeferredMutiThreaded) {
                        if (rendererType == Atmosphere::OPENGL32CORE) {
                            dir = "..\\..\\autotest\\multi2\\";
                        } else {
                            dir = "..\\..\\autotest\\multi2_gl\\";
                        }
                    }

                    std::stringstream ss;
                    ss << (screenshotId)+1;
                    if (autoTestSavingGoldenImages) {
                        ss << ".1";
                    }
                    ss << ".";
                    ss << sampleApp.MakeStringEnabledLayers();
                    if (sampleApp.myGenerateEnvMap) {
                        ss << "_environmentMap";
                    }
                    if (sampleApp.myGenerateShadowMap) {
                        ss << "_shadowMap";
                    }
                    ss << ".tga";
                    sampleApp.SaveScreenShot(dir + ss.str());
                    sampleApp.NextCloudLayer();
                    framesFinishedForCurrentScreenShot = 0;
                    ++screenshotId;
                }
                if (screenshotId == numOfScreenShots) {
                    sampleApp.ShutDown();
                    exit(1);
                }
            } else {
                ++screenshotId;
            }

            ++framesFinishedForCurrentScreenShot;
        }


        glfwSwapBuffers(main_window_and_context);
        glfwPollEvents();

        running &= (glfwGetKey(main_window_and_context, GLFW_KEY_ESCAPE) == GLFW_RELEASE);
        running &= (glfwWindowShouldClose(main_window_and_context) != GL_TRUE);

    } while (running);

    sampleApp.ShutDown();

    glfwTerminate();

    return 0;
}

