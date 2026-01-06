#include "SDKSample.h"
#include "OgreRenderQueue.h"
#include "SilverLining.h"
#include <d3d9.h>

// Copyright (c) 2008-2013 Sundog Software, LLC. All rights reserved worldwide.

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
                        renderSystem->_setTextureCoordCalculation(i, Ogre::TEXCALC_NONE);
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
            atmosphere->GetHorizonColor(0, &r, &g, &b);
            density = (float)(1.0 / atmosphere->GetConditions()->GetVisibility());

            // Decrease fog density with altitude, to avoid fog effects through the vacuum of space.
            static const double H = 8435.0; // Pressure scale height of Earth's atmosphere
            double isothermalEffect = exp(-(atmosphere->GetConditions()->GetLocation().GetAltitude() / H));
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
    MyRenderSystemListener(SilverLining::Atmosphere *atm) : Ogre::RenderSystem::Listener(), atmosphere(atm) {}

    void eventOccurred(const Ogre::String& eventName, const Ogre::NameValuePairList *parameters)
    {
       if (eventName == "DeviceLost")
       {
            if (atmosphere)
            {
                atmosphere->D3D9DeviceLost();
            }
       }
       else if (eventName == "DeviceRestored")
       {
           if (atmosphere)
           {
               atmosphere->D3D9DeviceReset();
           }
       }
    }

private:
    SilverLining::Atmosphere *atmosphere;
};
