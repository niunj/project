// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "../SamplesCommon/SampleDefines.h"
#include "QuadRendering.h"

#include <vector>
#include <string>

#include "SampleMath.h"

namespace SilverLining
{
    class Atmosphere;
    class CloudLayer;
    class Camera;
    class ThreadCameraStreamData;
}

namespace Sample
{
    class GltfModel;

    enum  DrawStrategyType
    {
        Draw_ImmediateSingleThreaded = 0
        , Draw_DeferredSingleThreaded = 1
        , Draw_DeferredMutiThreaded = 2
    };

    class Buffer;

    class RenderToTexture;

    class DrawStrategy;
    class LayersContainer;

    class SampleApp
    {
    public:
        //! constructor
        SampleApp();

        //! destructor
        virtual ~SampleApp();

    public:
        //! initialize the application
        virtual void Initialize(bool autoTest, DrawStrategyType drawStrategyType, int rendererType, bool renderSmallViews
            , const std::string& modelFileName
            , bool generateEnvMap
            , bool generateShadowMap
            , float yOffset
            , float initialPitch
            , int initialHour
            , int initialMinute);

        //! shutdown the application
        virtual void ShutDown();

        //! draw using the configured strategy
        virtual void Draw(double millis, int frame);

        //! render the small views into an rtt texture (used to
        //! draw the 2 small views on top
        //! follows the begin-draw-end paradigm
        virtual void RenderSmallViewBegin(int view, double millis) const;
        virtual void RenderQuad(int x, int y, int width, int height, int view);
        virtual void RenderSmallViewEnd(int view, double millis) const;

        //! whether we should render the small views
        virtual bool RenderSmallViews(void) const;

        //! the number of small views (2 small)
        virtual int NumViews(void) const { return myNumViews; }

        //! draw the sky
        virtual bool DrawSky(void) const;

        //! draw the clouds
        virtual bool DrawObjects(void) const;

        virtual void DrawModel(unsigned int environmentMap) const;

        //! set up the scene lighting
        virtual void SetSceneLighting(int view, const SilverLining::ThreadCameraStreamData* tcsData);

        //! set up the scene fog
        virtual void SetSceneFog(int view, const SilverLining::ThreadCameraStreamData* tcsData);

        //! advance the time
        virtual void AdvanceTime(long delta);

        //! toggle to the next cloud layer (turn it on)
        virtual void NextCloudLayer();

        //! delete a layer at index in layer container
        virtual void DeleteLayer(int index);

        //! toggle to the next precipitation layer (turn it on)
        virtual void NextPrecipitationType();

        //! save a screenshot
        virtual bool SaveScreenShot(const std::string& fileName);

        //! Get a string which is a concatenation of all enabled layers
        //! useful for naming a screenshot/auto-testing
        virtual std::string MakeStringEnabledLayers(void) const;

        //! Get the yaw
        virtual float GetYaw(int view) const;

        //! Get the pitch
        virtual float GetPitch(int view) const;

        //! Get the roll
        virtual float GetRoll(int view) const;

        //! Get the model view matrix
        virtual void GetModelViewMatrix(int view, double modelViewMatrix[16]) const;

        //! Get the projection matrix
        virtual void GetProjectionMatrix(int view, double projectionMatrix[16]) const;

        //! Do an action
        virtual void DoAction(Action action);

        //! Get the main window width
        virtual int GetWindowWidth(void) const;

        //! Get the main window height
        virtual int GetWindowHeight(void) const;

        virtual void SetWindowWidthAndHeight(int w, int h);

        virtual void SetSmallViewWidthAndHeight(int w, int h);

        virtual int GetSmallViewWidth(void) const { return myViewWidth; }

        virtual int GetSmallViewHeight(void) const { return myViewHeight; }

        virtual int MainViewIndex(void) const { return myNumViews; }

        virtual float CrepuscularIntensity(void) const { return myCrepuscularIntensity; }

        const RenderToTexture* rtt(int view) const { return myRenderToTextures[view]; }

        virtual void RenderQuadWithShadowTexture(int x, int y, int width, int height, unsigned int shadowMap);
    protected:
        //! initialize quad rendering
        virtual void InitQuadStuff();

        //! set up atmospheric conditions
        virtual void SetupAtmosphericConditions();

        //! set up the time and location
        virtual void SetTimeAndLocation();

        virtual std::vector<RenderToTexture*>& RenderToTextures(void);

        virtual void ResetViews(void);

    public:

        bool myDrawLightning;

        std::vector<SilverLining::ThreadCameraStreamData*> myTcsDatas;

        SilverLining::Atmosphere* myAtmosphere;

        bool quit;

        bool myGenerateEnvMap = false;

        bool myGenerateShadowMap = true;
    protected:

        DrawStrategyType myDrawStrategyType;

        DrawStrategy* myDrawStrategy;

        std::vector<RenderToTexture*> myRenderToTextures;

        //! layers
        LayersContainer* myLayersContainer;

        //! booleans to control various rendering toggles
        bool myRenderSmallViews;
        bool myDrawObjects;
        bool myDrawSky;


        float myYaw[3];
        float myPitch[3];
        float myTranslation[3];

        const int myNumViews = 2;

        int myViewWidth;
        int myViewHeight;

        int myWindowWidth;
        int myWindowHeight;

        float myCrepuscularIntensity;

        // Simulated visibility in meters, for fog effects.
        const float myKVisibility = 200000.0f;

        float myWindMultiplier;

        // -1 -> all views
        int myViewBeingControlled;

        QuadRenderer* myQuadRenderer;

        GltfModel* myModel = nullptr;
        glm::mat4 myInstanceMatrix;
        unsigned int myModelProgramId = 0;
        int myModelMatrixIndex = -1;
        int myViewMatrixIndex = -1;
        int myProjectionMatrixIndex = -1;
        int myEnvironmentMapIndex = -1;
        int myCameraPositionIndex = -1;
        Buffer* myModelVertexBufferGpu = nullptr;
        Buffer* myModelIndexBufferGpu = nullptr;

        unsigned int myModelVao = 0;

        float myYOffset = 0;
        float myInitialPitch = 0;
        int myInitialHour = 0;
        int myInitialMinutes = 0;
    };
}

