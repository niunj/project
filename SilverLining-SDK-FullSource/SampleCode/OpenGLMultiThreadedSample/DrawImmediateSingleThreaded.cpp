#include "GL/glew.h"
#include "DrawImmediateSingleThreaded.h"
#include "SampleApp.h"
#include "MatrixUtils.h"
#include "RenderToTexture.h"
#include "Buffer.h"
#include "GltfModel.h"

#include "SilverLining.h"

namespace Sample
{
DrawImmediateSingleThreaded::DrawImmediateSingleThreaded(SampleApp& sampleApp)
    : DrawStrategy(sampleApp)
{

}
DrawImmediateSingleThreaded::~DrawImmediateSingleThreaded()
{

}

void DrawImmediateSingleThreaded::Draw(double millis, int frame)
{
    double projectionMatrix[16];
    double modelViewMatrix[16];

    // Pass in the view and projection matrices to SilverLining.

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


            //// After setting up your projection and modelview matrices to reflect the current
            //// camera position, call Atmosphere::DrawSky() to draw the sky and do the lighting
            //// pass on the clouds, if necessary.
            if (mySampleApp.DrawSky()) {
                mySampleApp.myAtmosphere->DrawSky(true, false, 0
                                                  , true, true, true
                                                  , 0
                                                  , 0, 0
                                                  , mySampleApp.myTcsDatas[view]);

            }
            // Now, do all your own drawing...
            mySampleApp.SetSceneLighting(view, mySampleApp.myTcsDatas[view]);
            mySampleApp.SetSceneFog(view, mySampleApp.myTcsDatas[view]);

            if (mySampleApp.DrawObjects()) {
                //// When you're done, call Atmosphere::DrawObjects() to draw all the clouds from back to front.
                mySampleApp.myAtmosphere->DrawObjects(true, true
                                                      , true, mySampleApp.CrepuscularIntensity(), false
                                                      , 0, true, true
                                                      , mySampleApp.myDrawLightning, false
                                                      , mySampleApp.myTcsDatas[view]);
            }

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

    void* envMap = nullptr;
    if (mySampleApp.myGenerateEnvMap) {
        mySampleApp.myAtmosphere->GetEnvironmentMap(envMap, 6, false, 0, true, true, false, mySampleApp.myTcsDatas[mySampleApp.NumViews()]);

        camera->SetViewport(0, 0, mySampleApp.GetWindowWidth(), mySampleApp.GetWindowHeight());
        camera->SetRenderTarget(0);
    }

    void* shadowMap = nullptr;
    SilverLining::Matrix4 dummy;
    if (mySampleApp.myGenerateShadowMap) {
        mySampleApp.myAtmosphere->GetShadowMap(shadowMap, &dummy, &dummy, true, 1, false, -1.0, 1.0, mySampleApp.myTcsDatas[mySampleApp.NumViews()]);

        camera->SetViewport(0, 0, mySampleApp.GetWindowWidth(), mySampleApp.GetWindowHeight());
        camera->SetRenderTarget(0);
    }

    // After setting up your projection and modelview matrices to reflect the current
    // camera position, call Atmosphere::DrawSky() to draw the sky and do the lighting
    // pass on the clouds, if necessary.
    if (mySampleApp.DrawSky()) {
        mySampleApp.myAtmosphere->DrawSky(true, false, 0
                                          , true, true, true
                                          , 0
                                          , 0, 0
                                          , mySampleApp.myTcsDatas[mySampleApp.NumViews()]);
    }

    // Now, do all your own drawing...
    mySampleApp.SetSceneLighting(mySampleApp.NumViews(), mySampleApp.myTcsDatas[mySampleApp.NumViews()]);
    mySampleApp.SetSceneFog(mySampleApp.NumViews(), mySampleApp.myTcsDatas[mySampleApp.NumViews()]);

    if (mySampleApp.DrawObjects()) {
        // When you're done, call Atmosphere::DrawObjects() to draw all the clouds from back to front.
        mySampleApp.myAtmosphere->DrawObjects(true, true
                                              , true, mySampleApp.CrepuscularIntensity(), false
                                              , 0, true, true
                                              , mySampleApp.myDrawLightning, false
                                              , mySampleApp.myTcsDatas[mySampleApp.NumViews()]
                                             );
    }

    mySampleApp.DrawModel((unsigned int)(envMap));

    if (mySampleApp.RenderSmallViews()) {
        glDisable(GL_BLEND);

        mySampleApp.RenderQuad(20, 40, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight(), 0);

        if (mySampleApp.GetSmallViewHeight() == 256 && mySampleApp.GetSmallViewHeight() == 256) {
            mySampleApp.RenderQuad(600, 40, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight(), 1);
        } else {
            int xOffset = (int)((float)mySampleApp.GetWindowWidth() / (float)mySampleApp.GetWindowHeight()) * mySampleApp.GetSmallViewWidth() + 100;
            mySampleApp.RenderQuad(xOffset, 20, mySampleApp.GetSmallViewWidth(), mySampleApp.GetSmallViewHeight(), 1);
        }
    }

    if (mySampleApp.myGenerateShadowMap) {
        mySampleApp.RenderQuadWithShadowTexture(100, 40, 512, 512, (unsigned int)shadowMap);
    }
}
}