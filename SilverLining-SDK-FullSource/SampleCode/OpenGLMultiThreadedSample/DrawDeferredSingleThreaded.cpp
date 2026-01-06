#include "GL/glew.h"
#include "DrawDeferredSingleThreaded.h"
#include "SampleApp.h"
#include "MatrixUtils.h"

#include "SilverLining.h"
namespace Sample
{
DrawDeferredSingleThreaded::DrawDeferredSingleThreaded(SampleApp& sampleApp)
    : DrawStrategy(sampleApp)
{

}
DrawDeferredSingleThreaded::~DrawDeferredSingleThreaded()
{

}

void DrawDeferredSingleThreaded::Draw(double millis, int frame)
{
    double projectionMatrix[16];
    double modelViewMatrix[16];

    if (mySampleApp.RenderSmallViews()) {
        for (int view = 0; view < mySampleApp.NumViews(); ++view) {
            mySampleApp.RenderSmallViewBegin(view, millis);

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

            camera->SetViewport(0, 0, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight());
            camera->SetRenderTarget(mySampleApp.rtt(view)->GetFBO());

            // After setting up your projection and modelview matrices to reflect the current
            // camera position, call Atmosphere::DrawSky() to draw the sky and do the lighting
            // pass on the clouds, if necessary.
            // draw, but don't actually draw
            mySampleApp.myAtmosphere->DrawSky(true, false, 0
                                              , true, true, true
                                              , 0
                                              , 0, 0
                                              , mySampleApp.myTcsDatas[view]);

            // Now, do all your own drawing...
            mySampleApp.SetSceneLighting(view, mySampleApp.myTcsDatas[view]);
            mySampleApp.SetSceneFog(view, mySampleApp.myTcsDatas[view]);

            if (mySampleApp.DrawObjects()) {
                // When you're done, call Atmosphere::DrawObjects() to draw all the clouds from back to front.
                // draw, but don't actually draw
                mySampleApp.myAtmosphere->DrawObjects(true, true
                                                      , true, mySampleApp.CrepuscularIntensity(), false
                                                      , 0, true, true
                                                      , mySampleApp.myDrawLightning, false
                                                      , mySampleApp.myTcsDatas[view]);
            }

            // execute the stream
            mySampleApp.myTcsDatas[view]->ExecuteStream();
            mySampleApp.RenderSmallViewEnd(view, millis);
        }
    }

    mySampleApp.GetProjectionMatrix(mySampleApp.NumViews(), projectionMatrix);
    mySampleApp.GetModelViewMatrix(mySampleApp.NumViews(), modelViewMatrix);

    // Pass in the view and projection matrices to SilverLining.
    SilverLining::Matrix4 slModelViewMatrix(modelViewMatrix);
    SilverLining::Matrix4 slProjectionMatrix(projectionMatrix);

    slModelViewMatrix.Transpose();
    slProjectionMatrix.Transpose();

    SilverLining::Camera* camera = mySampleApp.myTcsDatas[mySampleApp.NumViews()]->GetCamera();

    camera->SetModelViewMatrix(slModelViewMatrix);
    camera->SetProjectionMatrix(slProjectionMatrix);

    camera->SetViewport(0, 0, mySampleApp.GetWindowWidth(), mySampleApp.GetWindowHeight());
    camera->SetRenderTarget(0);

    // do the main view now
    glViewport(0, 0, mySampleApp.GetWindowWidth(), mySampleApp.GetWindowHeight());

    // After setting up your projection and modelview matrices to reflect the current
    // camera position, call Atmosphere::DrawSky() to draw the sky and do the lighting
    // pass on the clouds, if necessary.
    // draw, but don't actually draw
    mySampleApp.myAtmosphere->DrawSky(true, false, 0
                                      , true, true, true
                                      , 0
                                      , 0, 0
                                      , mySampleApp.myTcsDatas[mySampleApp.NumViews()]);


    // Now, do all your own drawing...
    mySampleApp.SetSceneLighting(mySampleApp.NumViews(), mySampleApp.myTcsDatas[mySampleApp.NumViews()]);
    mySampleApp.SetSceneFog(mySampleApp.NumViews(), mySampleApp.myTcsDatas[mySampleApp.NumViews()]);

    if (mySampleApp.DrawObjects()) {
        // When you're done, call Atmosphere::DrawObjects() to draw all the clouds from back to front.
        // draw, but don't actually draw
        mySampleApp.myAtmosphere->DrawObjects(true, true
                                              , true, mySampleApp.CrepuscularIntensity(), false
                                              , 0, true, true
                                              , mySampleApp.myDrawLightning, false
                                              , mySampleApp.myTcsDatas[mySampleApp.NumViews()]
                                             );

        // execute the stream
        mySampleApp.myTcsDatas[mySampleApp.NumViews()]->ExecuteStream();
    }

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