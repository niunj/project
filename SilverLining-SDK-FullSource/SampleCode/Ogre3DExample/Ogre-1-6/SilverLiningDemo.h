#include "ExampleApplication.h"
#include "SilverLining.h"

// Copyright (c) 2008-2010 Sundog Software, LLC. All rights reserved worldwide.

/**
This is an example of integrating SilverLining's library for 3D real time volumetric clouds, sky
simulation, and precipitation into Ogre3D. More info on SilverLining may be found at 
http://www.sundog-soft.com/

This source is based on the Ogre3D demo applications, and you should use the same project settings
from the demos with a few additions:

- You must link in winmm.lib (a system library) and SilverLining-MTD-DLL.lib for debug builds or
  SilverLining-MT-DLL.lib for release builds. Refer to the SilverLining documentation
  (http://www.sundog-soft.com/docs/html/index.html) for the proper library names to use for
  other code generation types.

- Add the "public headers" directory of the SilverLining SDK to your include path.

- Add the "lib/vcX/win32" directory of the SilverLining SDK to your library path, where X is the version
  of visual studio you're using (6, 7, 8, 9, or 10). x64 libraries are also available.
*/

// Set these to your own:
static char* kUserName = "User Name";
static char* kLicenseCode = "License Code";
static char *kPathToResources = "C:\\Program Files\\SilverLining SDK\\resources\\";

static SilverLining::Atmosphere *atm = 0;
static void *pD3DDevice = 0;

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
        switch (queueGroupId)
        {
            case RENDER_QUEUE_SKIES_EARLY:
		    {
			    if (atmosphere)
			    {
                    SetMatrices();
				    SetLighting();
				    SetFog();
                    
				    atmosphere->DrawSky(true);
			    }
		    }
            break;
            
            case RENDER_QUEUE_SKIES_LATE:
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
				    atmosphere->DrawObjects();
			    }
		    }
            break;
        }
	}

protected:

    void SetMatrices(void)
    {
        Ogre::Camera *cam = sceneMgr->getCamera("PlayerCam");
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
            sceneMgr->setAmbientLight(ColourValue(ra, ga, ba));
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
        
        sceneMgr->setFog(Ogre::FOG_EXP, ColourValue(r, g, b), density);
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

    void eventOccurred(const String& eventName, const NameValuePairList *parameters)
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

class SilverLiningApplication : public ExampleApplication
{
public:
    SilverLiningApplication() {}

protected:

    bool setup(void)
    {
        // Instantiate the Atmosphere before going into full-screen mode, so the licensing warning dialog
        // won't block things if you're unregistered:
        atm = new SilverLining::Atmosphere(kUserName, kLicenseCode);

        return ExampleApplication::setup();
    }

    void SetupSilverLining(void)
    {
        // Obtain the D3D device, if we're using D3D.
        mWindow->getCustomAttribute("D3DDEVICE",&pD3DDevice);

        // Substitute the path to your SilverLining resources directory below
        int err = atm->Initialize(pD3DDevice == NULL ? SilverLining::Atmosphere::OPENGL : SilverLining::Atmosphere::DIRECTX9, 
            kPathToResources,
            true, pD3DDevice);

        if (err == SilverLining::Atmosphere::E_NOERROR)
        {
            atm->GetConditions()->SetVisibility(kVisibility);

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
            cumulusCongestusLayer->SetBaseLength(40000);
            cumulusCongestusLayer->SetBaseWidth(40000);
            cumulusCongestusLayer->SetDensity(0.3);
            cumulusCongestusLayer->SetLayerPosition(0, 0);
            cumulusCongestusLayer->SeedClouds(*atm);
            cumulusCongestusLayer->GenerateShadowMaps(false);
            atm->GetConditions()->AddCloudLayer(cumulusCongestusLayer);

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

    void createViewports(void)
    {
        Viewport* vp = mWindow->addViewport(mCamera);

        // Since the sky will clear the frame, don't waste time by clearing 
        // the viewport.
        vp->setClearEveryFrame(false, 0);

        mCamera->setAspectRatio(
            Real(vp->getActualWidth()) / Real(vp->getActualHeight()));
    }

    void createScene(void)
    {
        // Initialize SilverLining and set up some clouds and conditions.
        SetupSilverLining();

        // Inject hooks for SilverLining
        MyRenderQueueListener *myListener = 0;
        myListener = new MyRenderQueueListener(mSceneMgr,  mRoot->getRenderSystem(), atm);
        mSceneMgr->addRenderQueueListener(myListener);

        mRoot->getRenderSystem()->addListener(new MyRenderSystemListener());

        // Create a light
        Light* l = mSceneMgr->createLight("MainLight");
        l->setType(Ogre::Light::LT_DIRECTIONAL);

        // Add in a model to illustrate lighting
        Entity *ent = mSceneMgr->createEntity( "razor", "razor.mesh" );
        mSceneMgr->getRootSceneNode()->attachObject( ent );
    }

};
