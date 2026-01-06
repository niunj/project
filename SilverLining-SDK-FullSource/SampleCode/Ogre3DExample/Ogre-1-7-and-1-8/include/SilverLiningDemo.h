#include "SDKSample.h"
#include "OgreRenderQueue.h"
#include "SilverLining.h"
#include <d3d9.h>

// Copyright (c) 2008-2012 Sundog Software, LLC. All rights reserved worldwide.

/**
This is an example of integrating SilverLining's library for 3D real time volumetric clouds, sky
simulation, and precipitation into Ogre3D. More info on SilverLining may be found at 
http://www.sundog-soft.com/

If you don't yet have a SilverLining license, be sure to run in windowed mode as you'll need
to acknowledge a warning dialog box when SilverLining::Atmosphere is constructed.

This source is based on the Ogre3D demo applications, and you should use the same project settings
from the demos with a few additions:

- You must link in SilverLining-MTD-DLL.lib for debug builds or
  SilverLining-MT-DLL.lib for release builds. Refer to the SilverLining documentation
  (http://www.sundog-soft.com/docs/html/index.html) for the proper library names to use for
  other code generation types.

- Add the "public headers" directory of the SilverLining SDK to your include path.

- Add the "lib/vcX/win32" directory of the SilverLining SDK to your library path, where X is the version
  of visual studio you're using (6, 7, 8, 9, or 10). x64 libraries are also available.
*/

// Set these to your own once you've purchased a license:
static char* kUserName = "User Name";
static char* kLicenseCode = "License Code";

static SilverLining::Atmosphere *atm = 0;

#define kVisibility 100000.0

// We use a RenderQueueListener in order to get the hooks we need into the beginning and end of the frame
// (well, almost the end - we do let overlays draw after us.) Ogre3D already has handy "skies" render
// queues.
class MyRenderQueueListener : public Ogre::RenderQueueListener
{
public:
    MyRenderQueueListener(Ogre::SceneManager *pSceneMgr, Ogre::RenderSystem *pRenderSystem, SilverLining::Atmosphere *atm) : RenderQueueListener(), 
        sceneMgr(pSceneMgr), renderSystem(pRenderSystem), atmosphere(atm) {}

    void renderQueueStarted(Ogre::uint8, const Ogre::String&, bool &)
    {
    }

	void renderQueueEnded(Ogre::uint8 queueGroupId, const Ogre::String &invocation, bool &skipThisInvocation) 
	{
        // If we're rendering a shadow map, we probably only want clouds drawn.
        bool bShadowMap = (invocation == "SHADOWS");

        switch (queueGroupId)
        {
        case Ogre::RENDER_QUEUE_SKIES_EARLY:
		    {
			    if (atmosphere)
			    {
                    // Sync up Ogre's state with SilverLining:
                    SetMatrices();
				    SetLighting();
				    SetFog();
                    
                    // Draw the sky box:
				    atmosphere->DrawSky(!bShadowMap);
			    }
		    }
            break;
            
        case Ogre::RENDER_QUEUE_SKIES_LATE:
		    {
			    if (atmosphere && renderSystem)
			    {
                    // Clear out any stray state that might interfere with our clouds:
                    renderSystem->unbindGpuProgram(Ogre::GPT_FRAGMENT_PROGRAM);
                    renderSystem->unbindGpuProgram(Ogre::GPT_VERTEX_PROGRAM);
                    for (int i = 0; i < 8; i++) {
                        renderSystem->_setTexture(i, false, "");
                        renderSystem->_setTextureCoordCalculation(i, Ogre::TexCoordCalcMethod::TEXCALC_NONE);
                    }

                    // Draw the clouds, lens flare, and precipitation effects.
                    atmosphere->EnableLensFlare(!bShadowMap);
				    atmosphere->DrawObjects(true, !bShadowMap, true);
			    }
		    }
            break;
        }
	}

protected:

    void SetMatrices(void)
    {
        Ogre::Camera *cam = sceneMgr->getCamera("MainCamera");
        if (cam && atmosphere)
        {
            Ogre::Matrix4 proj = cam->getProjectionMatrixWithRSDepth();
            Ogre::Matrix4 view = cam->getViewMatrix();
            double dProj[16], dView[16];
            int idx = 0;
            for (int row = 0; row < 4; row++)
            {
                for (int col = 0; col < 4; col++)
                {
                    dProj[idx] = proj[col][row];
                    dView[idx] = view[col][row];
                    idx++;
                }
            }
            atmosphere->SetProjectionMatrix(dProj);
            atmosphere->SetCameraMatrix(dView);
        }
    }
    
    void SetLighting(void)
    {
        Ogre::Light *light = sceneMgr->getLight("MainLight");
        if (atmosphere && light)
        {
            float ra, ga, ba, rd, gd, bd, x, y, z;
            atmosphere->GetAmbientColor(&ra, &ga, &ba);
            atmosphere->GetSunOrMoonColor(&rd, &gd, &bd);
            atmosphere->GetSunOrMoonPosition(&x, &y, &z);

            light->setDiffuseColour(rd, gd, bd);
            light->setSpecularColour(0, 0, 0);
            sceneMgr->setAmbientLight(Ogre::ColourValue(ra, ga, ba));
            light->setDirection(-x, -y, -z);
        }
    }

    void SetFog(void)
    {
        if (!atmosphere || !sceneMgr) return;

        float density, r, g, b;
        if (atmosphere->GetFogEnabled())
        {
            float ar, ag, ab;
            atmosphere->GetSunOrMoonColor(&ar, &ag, &ab);
            atmosphere->GetFogSettings(&density, &r, &g, &b);
            r *= ar; g *= ag; b *= ab;
        }
        else
        {
            atm->GetHorizonColor(0, &r, &g, &b);
            density = 1.0 / kVisibility;

            // Decrease fog density with altitude, to avoid fog effects through the vacuum of space.
            static const double H = 8435.0; // Pressure scale height of Earth's atmosphere
            double isothermalEffect = exp(-(atm->GetConditions()->GetLocation().GetAltitude() / H));
            if (isothermalEffect <= 0) isothermalEffect = 1E-9;
            if (isothermalEffect > 1.0) isothermalEffect = 1.0;
            density *= isothermalEffect;
        }
        
        sceneMgr->setFog(Ogre::FOG_EXP, Ogre::ColourValue(r, g, b), density);
    }

    Ogre::SceneManager *sceneMgr;
    Ogre::RenderSystem *renderSystem;
    SilverLining::Atmosphere *atmosphere;
};

// Properly handle device lost and reset events under DX9:
class MyRenderSystemListener : public Ogre::RenderSystem::Listener
{
public:
    MyRenderSystemListener() : Ogre::RenderSystem::Listener() {}

    void eventOccurred(const Ogre::String& eventName, const Ogre::NameValuePairList *parameters)
    {
       if (eventName == "DeviceLost")
       {
            if (atm)
            {
                atm->D3D9DeviceLost();
            }
       }
       else if (eventName == "DeviceRestored")
       {
           if (atm)
           {
               atm->D3D9DeviceReset();
           }
       }
    }
};

class SilverLiningApplication : public OgreBites::SdkSample
{
public:
	SilverLiningApplication()
	{
		mInfo["Title"] = "SilverLining";
		mInfo["Description"] = "Shows how to use the SilverLining sky, cloud, and weather SDK.";
		mInfo["Thumbnail"] = "thumb_skybox.png";
		mInfo["Category"] = "Environment";

        mRenderSystemListener = 0;
	}

    ~SilverLiningApplication()
    {
        if (mRoot && mRenderSystemListener) 
        {
            mRoot->getRenderSystem()->removeListener(mRenderSystemListener);
        }

        if (atm)
        {
            delete atm;
            atm = 0;
        }
    }

protected:

    void SetupSilverLining(void)
    {
        // Obtain the D3D device, if we're using D3D.
        void *pD3DDevice = 0;
        mWindow->getCustomAttribute("D3DDEVICE",&pD3DDevice);

        // WARNING: If you're in full screen mode, any license warning dialog boxes will be obscured.
        // Work in windowed mode until you have a real SilverLining license.
        atm = new SilverLining::Atmosphere(kUserName, kLicenseCode);

        // Substitute the path to your SilverLining resources directory below
        Ogre::String resPath = getenv("SILVERLINING_PATH");
        resPath += "\\resources\\";

        int err = atm->Initialize(pD3DDevice == NULL ? SilverLining::Atmosphere::OPENGL : SilverLining::Atmosphere::DIRECTX9, 
            resPath.c_str(),
            true, pD3DDevice);

        if (err == SilverLining::Atmosphere::E_NOERROR)
        {
            atm->GetConditions()->SetVisibility(kVisibility);

            // Add a little wind
            SilverLining::WindVolume wv;
            wv.SetWindSpeed(20);
            atm->GetConditions()->SetWind(wv);

            // Set our location (change this to your own latitude and longitude)
            SilverLining::Location loc;
            loc.SetAltitude(0);
            loc.SetLatitude(45);
            loc.SetLongitude(-122);
            atm->GetConditions()->SetLocation(loc);

            // Set the time to the current system time (or you could set it explicitly to a fixed time)
            SilverLining::LocalTime t;
            t.SetFromSystemTime();
            t.SetTimeZone(PST); // It's important that your time zone is consistent with your location above
            atm->GetConditions()->SetTime(t);

            // Throw in some cumulus congestus clouds
            SilverLining::CloudLayer *cumulusCongestusLayer;
            cumulusCongestusLayer = SilverLining::CloudLayerFactory::Create(CUMULUS_CONGESTUS);
            cumulusCongestusLayer->SetIsInfinite(true);
            cumulusCongestusLayer->SetBaseAltitude(4000);
            cumulusCongestusLayer->SetThickness(500);
            cumulusCongestusLayer->SetBaseLength(80000);
            cumulusCongestusLayer->SetBaseWidth(80000);
            cumulusCongestusLayer->SetDensity(0.3);
            cumulusCongestusLayer->SetLayerPosition(0, 0);
            cumulusCongestusLayer->SetFadeTowardEdges(true);
            cumulusCongestusLayer->SeedClouds(*atm);
            cumulusCongestusLayer->GenerateShadowMaps(false);
            atm->GetConditions()->AddCloudLayer(cumulusCongestusLayer);

            // Sample for setting up broken stratus clouds if you prefer:
            /*
            SilverLining::CloudLayer *stratusLayer;
            stratusLayer = SilverLining::CloudLayerFactory::Create(STRATUS);
            stratusLayer->SetIsInfinite(true);
            stratusLayer->SetBaseAltitude(1000.0);
            stratusLayer->SetThickness(500.0);
            stratusLayer->SetBaseLength(30000);
            stratusLayer->SetBaseWidth(30000);
            stratusLayer->SetDensity(0.5);
            stratusLayer->SetFadeTowardEdges(true);
            stratusLayer->SeedClouds(*atm);
            atm->GetConditions()->AddCloudLayer(stratusLayer);
            */

            // A little cirrus too
            SilverLining::CloudLayer *cirrusCloudLayer;
            cirrusCloudLayer = SilverLining::CloudLayerFactory::Create(CIRRUS_FIBRATUS);
            cirrusCloudLayer->SetBaseAltitude(8000);
            cirrusCloudLayer->SetThickness(0);
            cirrusCloudLayer->SetBaseLength(200000);
            cirrusCloudLayer->SetBaseWidth(200000);
            cirrusCloudLayer->SetLayerPosition(0, 0);
            cirrusCloudLayer->SeedClouds(*atm);
            atm->GetConditions()->AddCloudLayer(cirrusCloudLayer);
        }
    }

    void setupView(void)
    {
        // setup default viewport layout and camera
		mCamera = mSceneMgr->createCamera("MainCamera");
		mViewport = mWindow->addViewport(mCamera);

        // Since the sky will clear the frame, don't waste time by clearing 
        // the viewport.
        mViewport->setClearEveryFrame(false, 0);

		mCamera->setAspectRatio((Ogre::Real)mViewport->getActualWidth() / (Ogre::Real)mViewport->getActualHeight());
		mCamera->setNearClipDistance(5);
        mCamera->setFarClipDistance(100000);

		mCameraMan = new OgreBites::SdkCameraMan(mCamera);   // create a default camera controller
    }

    void setupContent(void)
    {
        // Initialize SilverLining and set up some clouds and conditions.
        SetupSilverLining();

        // Inject hooks for SilverLining
        MyRenderQueueListener *myListener = 0;
        myListener = new MyRenderQueueListener(mSceneMgr, mRoot->getRenderSystem(), atm);
        mSceneMgr->addRenderQueueListener(myListener);

        mRenderSystemListener = new MyRenderSystemListener();
        mRoot->getRenderSystem()->addListener(mRenderSystemListener);

        // Create a light
        Ogre::Light* l = mSceneMgr->createLight("MainLight");
        l->setType(Ogre::Light::LT_DIRECTIONAL);

        // Add in a model to illustrate lighting
        Ogre::Entity *ent = mSceneMgr->createEntity( "razor", "razor.mesh" );
        mSceneMgr->getRootSceneNode()->attachObject( ent );

        // set the camera's initial position and orientation
		mCamera->setPosition(0, 0, 150);
		mCamera->yaw(Ogre::Degree(5));
    }

    MyRenderSystemListener *mRenderSystemListener;

};
