#include "GL/glew.h"
#include "DrawDeferredMultiThreaded.h"
#include "SampleApp.h"
#include "MatrixUtils.h"
#include "SilverLining.h"

#include <iostream>
#include <cassert>

namespace Sample
{
#define  DRAWDEFERREDMULTITHREADED_DEBUG 1

struct FrameParameters {
    double millis;
    int frame;
};

FrameParameters params;

void DrawDeferredMultiThreaded::SignalViewsToStartRendering()
{
    // signal threads to start rendering
    {
        std::lock_guard<std::mutex> lk(myMutex);
        for (int i = 0; i < mySampleApp.NumViews() + 1; ++i) {
            myBool_Begin_RenderView[i] = true;
        }
    }
    myConditionVariable.notify_all();
}

bool DrawDeferredMultiThreaded::allFinished() const
{
    bool allFinished = true;
    for (int i = 0; i < mySampleApp.NumViews() + 1; ++i) {
        allFinished &= myBool_End_RenderView[i];
    }
    return allFinished;
}

class AllFinished
{
public:
    AllFinished(const DrawDeferredMultiThreaded* _drawDeferredMultiThreaded)
        : drawDeferredMultiThreaded(_drawDeferredMultiThreaded)
    {

    }
    bool operator()() const
    {
        return drawDeferredMultiThreaded->allFinished();
    }
    const DrawDeferredMultiThreaded* drawDeferredMultiThreaded;
};

void DrawDeferredMultiThreaded::WaitForAllViewsToFinishRendering()
{
    // wait for the worker threads
    std::unique_lock<std::mutex> lk(myMutex);
    //while (!allFinished())
    //{
    myConditionVariable.wait(lk, AllFinished(this));
    //}
    for (int i = 0; i < mySampleApp.NumViews() + 1; ++i) {
        myBool_End_RenderView[i] = false;
    }
}

void DrawDeferredMultiThreaded::RenderView(int view)
{
    for (;;) {
        // Wait for main thread to signal ready
        {
            std::unique_lock<std::mutex> lk(myMutex);

            myConditionVariable.wait(lk, [this, view] { return myBool_Begin_RenderView[view] || quit; });
            if (quit) {
                break;
            }
        }

        // This will do the stream/packet generation
        // For DrawConcurrent, we don't need a context at all!
        // Its simply generating all the commands for this view,
        // to be executed later on the main thread
        if (mySampleApp.RenderSmallViews() && view != 2) {
            mySampleApp.myAtmosphere->DrawSky(true, false, 0
                                              , true, true, true
                                              , 0
                                              , 0, 0
                                              , mySampleApp.myTcsDatas[view]);

            if (mySampleApp.DrawObjects()) {
                // When you're done, call Atmosphere::DrawObjects() to draw all the clouds from back to front.
                // draw, but don't actually draw
                mySampleApp.myAtmosphere->DrawObjects(true, true
                                                      , true, mySampleApp.CrepuscularIntensity(), false
                                                      , 0, true, true
                                                      , mySampleApp.myDrawLightning, false
                                                      , mySampleApp.myTcsDatas[view]);
            }
        } else if (view==2) {
            mySampleApp.myAtmosphere->DrawSky(true, false, 0
                                              , true, true, true
                                              , 0
                                              , 0, 0
                                              , mySampleApp.myTcsDatas[view]);

            if (mySampleApp.DrawObjects()) {
                // When you're done, call Atmosphere::DrawObjects() to draw all the clouds from back to front.
                // draw, but don't actually draw
                mySampleApp.myAtmosphere->DrawObjects(true, true
                                                      , true, mySampleApp.CrepuscularIntensity(), false
                                                      , 0, true, true
                                                      , mySampleApp.myDrawLightning, false
                                                      , mySampleApp.myTcsDatas[view]);
            }
        }

        ++myNumRenders;

#if DRAWDEFERREDMULTITHREADED_DEBUG
        std::cout << "Rendered view: " << view << ", in thread: " << std::this_thread::get_id() << std::endl;
#endif
        {
            std::unique_lock<std::mutex> lk(myMutex);
            myBool_Begin_RenderView[view] = false;
            myBool_End_RenderView[view] = true;
            lk.unlock();
            myConditionVariable.notify_all();
        }
    }
}

DrawDeferredMultiThreaded::DrawDeferredMultiThreaded(SampleApp& sampleApp)
    : DrawStrategy(sampleApp)
    , quit(false)
{
    myNumRenders = 0;
    myThreadForView.resize(mySampleApp.NumViews() + 1);
    myBool_Begin_RenderView.resize(mySampleApp.NumViews() + 1, false);
    myBool_End_RenderView.resize(mySampleApp.NumViews() + 1, false);

    for (int i = 0; i < mySampleApp.NumViews(); ++i) {
        myThreadForView[i] = std::thread(&DrawDeferredMultiThreaded::RenderView, this, i);
    }
    myThreadForView[mySampleApp.NumViews()] = std::thread(&DrawDeferredMultiThreaded::RenderView, this, mySampleApp.NumViews());
}

DrawDeferredMultiThreaded::~DrawDeferredMultiThreaded()
{
    quit = true;
    myConditionVariable.notify_all();
    for (int i = 0; i < mySampleApp.NumViews() + 1; ++i) {
        myThreadForView[i].join();
    }

    //assert(myNumRenders / (mySampleApp.NumViews() + 1) == frame);
}

void DrawDeferredMultiThreaded::Draw(double millis, int frame)
{
#if DRAWDEFERREDMULTITHREADED_DEBUG
    std::cout << "<DrawDeferredMultiThreaded>" << std::endl;
#endif
    double projectionMatrix[16];
    double modelViewMatrix[16];

    for (int view = 0; view <= mySampleApp.NumViews(); ++view) {

        mySampleApp.GetProjectionMatrix(view, projectionMatrix);
        mySampleApp.GetModelViewMatrix(view, modelViewMatrix);

        // Pass in the view and projection matrices to SilverLining.
        SilverLining::Matrix4 slModelViewMatrix(modelViewMatrix);
        SilverLining::Matrix4 slProjectionMatrix(projectionMatrix);

        slModelViewMatrix.Transpose();
        slProjectionMatrix.Transpose();

        SilverLining::Camera* camera = mySampleApp.myTcsDatas[view]->GetCamera();
        camera->SetModelViewMatrix(slModelViewMatrix);
        camera->SetProjectionMatrix(slProjectionMatrix);

        if (view == 2) {
            camera->SetViewport(0, 0, mySampleApp.GetWindowWidth(), mySampleApp.GetWindowHeight());
            camera->SetRenderTarget(0);
        } else {
            camera->SetViewport(0, 0, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight());
            camera->SetRenderTarget(mySampleApp.rtt(view)->GetFBO());
        }
    }


#if DRAWDEFERREDMULTITHREADED_DEBUG
    std::cout << "Signaling..." << std::endl;

    SignalViewsToStartRendering();

    std::cout << "waiting..." << std::endl;

    WaitForAllViewsToFinishRendering();

    std::cout << "Back in main!" << std::endl;
    std::cout << "</DrawDeferredMultiThreaded>" << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
#endif

    // execute the deferred compile operations
    if (mySampleApp.RenderSmallViews()) {
        for (int view = 0; view < mySampleApp.NumViews(); ++view) {
            mySampleApp.RenderSmallViewBegin(view, millis);
            mySampleApp.myTcsDatas[view]->ExecuteStream();
            mySampleApp.RenderSmallViewEnd(view, millis);
        }
    }

    // do the main view now
    glViewport(0, 0, mySampleApp.GetWindowWidth(), mySampleApp.GetWindowHeight());
    glClearColor(0.0f, 0.1f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // execute the stream
    mySampleApp.myTcsDatas[mySampleApp.NumViews()]->ExecuteStream();

    if (mySampleApp.RenderSmallViews()) {
        glDisable(GL_BLEND);

        mySampleApp.RenderQuad(20, 20, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight(), 0);

        if (mySampleApp.GetSmallViewHeight() == 256 && mySampleApp.GetSmallViewHeight() == 256) {
            mySampleApp.RenderQuad(600, 20, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight(), 1);
        } else {
            int xOffset = (int)((float)mySampleApp.GetWindowWidth() / (float)mySampleApp.GetWindowHeight())*mySampleApp.GetSmallViewWidth() + 100;
            mySampleApp.RenderQuad(xOffset, 20, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight(), 1);
        }
    }
}
}






