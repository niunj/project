/*
-----------------------------------------------------------------------------
Filename:    SilverLiningApplication.cpp
-----------------------------------------------------------------------------

Illustrates an integration of SilverLining with Ogre using the OgreWiki Tutorial
framework.

If you have any problems, contact support@sundog-soft.com and we'll do our best
to help out.

-----------------------------------------------------------------------------
*/
#include "SilverLiningApplication.h"

static SilverLining::Atmosphere *atm = 0;

// Set these to your own once you've purchased a license:
static char* kUserName = "User Name";
static char* kLicenseCode = "License Code";

#define kVisibility 100000.0

//-------------------------------------------------------------------------------------
SilverLiningApplication::SilverLiningApplication(void)
{
}
//-------------------------------------------------------------------------------------
SilverLiningApplication::~SilverLiningApplication(void)
{
    if (mRoot && mRenderSystemListener) {
        mRoot->getRenderSystem()->removeListener(mRenderSystemListener);
    }

    if (atm) {
        delete atm;
        atm = 0;
    }
}

void SilverLiningApplication::SetupSilverLining(void)
{
    // Obtain the D3D device, if we're using D3D.
    void *pD3DDevice = 0;
    mWindow->getCustomAttribute("D3DDEVICE",&pD3DDevice);

    if (!atm) {
        // WARNING: If you're in full screen mode, any license warning dialog boxes will be obscured.
        // Work in windowed mode until you have a real SilverLining license, or instantiate the
        // Atmosphere before Ogre's window gets created (as this demo does.)
        atm = new SilverLining::Atmosphere(kUserName, kLicenseCode);
    }

    // Substitute the path to your SilverLining resources directory below
    Ogre::String resPath = getenv("SILVERLINING_PATH");
    resPath += "\\resources\\";

    int err = atm->Initialize(pD3DDevice == NULL ? SilverLining::Atmosphere::OPENGL : SilverLining::Atmosphere::DIRECTX9,
                              resPath.c_str(), true, pD3DDevice);

    if (err == SilverLining::Atmosphere::E_NOERROR) {
        // Add some atmospheric perspective
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
        cumulusCongestusLayer->SetBaseAltitude(3000);
        cumulusCongestusLayer->SetThickness(500);
        cumulusCongestusLayer->SetBaseLength(70000);
        cumulusCongestusLayer->SetBaseWidth(70000);
        cumulusCongestusLayer->SetDensity(0.6);
        cumulusCongestusLayer->SetLayerPosition(0, 0);
        cumulusCongestusLayer->SetFadeTowardEdges(true);
        cumulusCongestusLayer->SetAlpha(0.8);
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
        cirrusCloudLayer->SetBaseLength(100000);
        cirrusCloudLayer->SetBaseWidth(100000);
        cirrusCloudLayer->SetLayerPosition(0, 0);
        cirrusCloudLayer->SeedClouds(*atm);
        atm->GetConditions()->AddCloudLayer(cirrusCloudLayer);
    }
}

void SilverLiningApplication::createViewports(void)
{
    // setup default viewport layout and camera
    mCamera = mSceneMgr->createCamera("MainCamera");
    Ogre::Viewport* vp = mWindow->addViewport(mCamera);

    // Since the sky will clear the frame, don't waste time by clearing
    // the viewport.
    vp->setClearEveryFrame(false, 0);

    mCamera->setAspectRatio((Ogre::Real)vp->getActualWidth() / (Ogre::Real)vp->getActualHeight());
    mCamera->setNearClipDistance(5);
    mCamera->setFarClipDistance(100000);
    mCamera->setFOVy(Ogre::Radian(Ogre::Degree(15.0f)));
    Ogre::Vector3 direction = Ogre::Vector3(0.0f, 0.1f, -1.0f);
    direction.normalise();
    mCamera->setDirection(direction);

    mCameraMan = new OgreBites::SdkCameraMan(mCamera);   // create a default camera controller
}

//-------------------------------------------------------------------------------------
void SilverLiningApplication::createScene(void)
{
    // Initialize SilverLining and set up some clouds and conditions.
    SetupSilverLining();

    // Inject hooks for SilverLining
    MyRenderQueueListener *myListener = 0;
    myListener = new MyRenderQueueListener(mSceneMgr, mRoot->getRenderSystem(), atm);
    mSceneMgr->addRenderQueueListener(myListener);

    mRenderSystemListener = new MyRenderSystemListener(atm);
    mRoot->getRenderSystem()->addListener(mRenderSystemListener);

    // Set the scene's ambient light
    mSceneMgr->setAmbientLight(Ogre::ColourValue(0.5f, 0.5f, 0.5f));

    // Create an Entity
    Ogre::Entity* ogreHead = mSceneMgr->createEntity("Head", "ogrehead.mesh");

    // Create a SceneNode and attach the Entity to it
    Ogre::SceneNode* headNode = mSceneMgr->getRootSceneNode()->createChildSceneNode("HeadNode");
    headNode->attachObject(ogreHead);
    headNode->setPosition(0, 0, -1000);

    // Create a Light; SilverLining iwll update its position and color.
    Ogre::Light* light = mSceneMgr->createLight("MainLight");
}



#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR strCmdLine, INT )
#else
int main(int argc, char *argv[])
#endif
{
    // Create application object
    SilverLiningApplication app;

    // This is only here so the license warning dialog box pops up before you go into fullscreen mode.
    atm = new SilverLining::Atmosphere(kUserName, kLicenseCode);

    try {
        app.go();
    } catch( Ogre::Exception& e ) {
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
        MessageBox( NULL, e.getFullDescription().c_str(), "An exception has occured!", MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
        std::cerr << "An exception has occured: " <<
                  e.getFullDescription().c_str() << std::endl;
#endif
    }

    return 0;
}

#ifdef __cplusplus
}
#endif
