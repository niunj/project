#include "AtmosphereReference.h"

#include <sstream>

int SilverLiningCameraManager::theCameraId = 0;

SilverLiningCameraManager::SilverLiningCameraManager()
// Let SilverLining know which way is up. OSG usually has Z going up.
    : myUpVector(SilverLining::Vector3(0, 0, 1))
    , myRightVector(SilverLining::Vector3(1, 0, 0))
{
    // nothing else to do
}

SilverLiningCameraManager::~SilverLiningCameraManager()
{
    if (myMapOsgCamerasToTcsDatas.empty() == false) {
        std::cout << "myMapOsgCamerasToTcsDatas.empty()==false" << std::endl;
    }
}

SilverLining::ThreadCameraStreamData* SilverLiningCameraManager::getOrCreateSilverLiningTcsData(const osg::Camera* osgCamera, const SilverLining::Atmosphere* atmosphere, bool& created)
{
    created = false;
    if (osgCamera == nullptr || atmosphere == nullptr) {
        return 0;
    }
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(myMapMutex);
    MapCamerasToTcsDatas::iterator it = myMapOsgCamerasToTcsDatas.find(osgCamera);
    if (it != myMapOsgCamerasToTcsDatas.cend()) {
        return it->second.myTcsData;
    }

    SilverLining::ThreadCameraStreamData* tcsData = SL_NEW SilverLining::ThreadCameraStreamData(true, atmosphere->GetUnitScale(), false);
    created = true;

    // set this before initializing!!
    tcsData->GetCamera()->SetUpVector(myUpVector);
    tcsData->GetCamera()->SetRightVector(myRightVector);

    TcsDataAndParams params;
    params.myTcsData = tcsData;
    params.mySeeded = false;
    myMapOsgCamerasToTcsDatas.insert(std::make_pair(osgCamera, params));

    std::stringstream ss;
    ss << "camera_" << theCameraId++ << std::endl;
    SilverLining::Camera* camera = tcsData->GetCamera();

    camera->SetName(ss.str().c_str());

    const bool avoidStalls = atmosphere->GetConfigOptionBoolean("avoid-opengl-stalls");
    tcsData->Initialize(SilverLining::Atmosphere::OPENGL32CORE, true, 0, avoidStalls);
    tcsData->Initialize(*atmosphere);

    return tcsData;
}

SilverLining::ThreadCameraStreamData* SilverLiningCameraManager::findSilverLiningTcsData(const osg::Camera* osgCamera, const SilverLining::Atmosphere* atmosphere)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(myMapMutex);
    MapCamerasToTcsDatas::iterator it = myMapOsgCamerasToTcsDatas.find(osgCamera);
    if (it != myMapOsgCamerasToTcsDatas.cend()) {
        return it->second.myTcsData;
    }
    return 0;
}

void SilverLiningCameraManager::seedLayers(const osg::Camera* osgCamera, SilverLining::Atmosphere* atmosphere, const osg::Vec3d& pos)
{
    if (osgCamera == nullptr || atmosphere == nullptr) {
        std::cout << "(osgCamera == nullptr || atmosphere == nullptr)" << std::endl;
        return;
    }
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(myMapMutex);
    MapCamerasToTcsDatas::iterator it = myMapOsgCamerasToTcsDatas.find(osgCamera);
    if (it == myMapOsgCamerasToTcsDatas.cend()) {
        std::cout << "tcs data == 0" << std::endl;
        return;
    }
    TcsDataAndParams& params = it->second;
    if (params.mySeeded == true) {
        std::cout << "seeded == true" << std::endl;
        return;
    }
    SilverLining::ThreadCameraStreamData* tcsData = params.myTcsData;

    SL_MAP(int, SilverLining::CloudLayer*)& cloudLayers = atmosphere->GetConditions()->GetCloudLayers();
    for (SL_MAP(int, SilverLining::CloudLayer*)::iterator layerIter = cloudLayers.begin(); layerIter != cloudLayers.end(); ++layerIter) {
        SilverLining::CloudLayer* cloudLayer = layerIter->second;

        // These are per tcs data, just store them in the default (tcsData = 0) so that we can get these values
        // for seeding for each tcs data later on
        cloudLayer->SetBaseAltitude(4000, true, tcsData);
        // Note, we pass in X and -Y since this accepts "east" and "south" coordinates.
        cloudLayer->SetLayerPosition(pos.x(), -pos.y(), tcsData);

        // reset the atmosphere, so that the cloud positions are the same in both views
        atmosphere->GetRandomNumberGenerator()->Reset();
        // seed the clouds with this tcs data
        cloudLayer->SeedClouds(*atmosphere, params.myTcsData);
    }
    params.mySeeded = true;
}

AtmosphereReference::AtmosphereReference(SilverLining::Atmosphere* atmosphere)
    : myAtmosphereInitialized(false), myAtmosphere(atmosphere)
{
    mySlCameraManager = new SilverLiningCameraManager();
}

AtmosphereReference::~AtmosphereReference()
{
    delete mySlCameraManager;
}

bool AtmosphereReference::initializeIfNecessary(const osg::Vec3d& pos)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(myMutex);
    if (myAtmosphereInitialized) {
        return true;
    }

    myAtmosphereInitialized = true; // only try once.

    if (myAtmosphere) {
        // Update the path below to where you installed SilverLining's resources folder.
        const char* slPath = getenv("SILVERLINING_PATH");
        if (!slPath) {
            std::cout << "Can't find SilverLining; set the SILVERLINING_PATH environment variable " << std::endl;
            std::cout << "to point to the directory containing the SDK.\n" << std::endl;
            exit(0);
        }

        std::string resPath(slPath);
        resPath += "\\Resources\\";
        int ret = myAtmosphere->Initialize(SilverLining::Atmosphere::OPENGL32CORE, resPath.c_str(),
                                           true, 0);
        if (ret != SilverLining::Atmosphere::E_NOERROR) {
            std::cout << "SilverLining failed to initialize; error code: " << ret << std::endl;
            std::cout << "Check that the path to the SilverLining installation directory is set properly " << std::endl;
            std::cout << "in SkyDrawable.cpp (in SkyDrawable::initializeSilverLining)" << std::endl;
            exit(0);
        }

        // The osg contexts don't use shareable contexts/resources in this sample, so set this to no
        myAtmosphere->SetConfigOption("textures-are-shared", "no");
        // we propose to use this sample to demonstrate the use of deferred
        // rendering and multi-threaded. so just set this yes as noted in the docs
        myAtmosphere->SetConfigOption("avoid-opengl-stalls", "yes");

        // set the up and right vector. This is also what is going to be used by the tcs datas we
        // create
        myAtmosphere->SetUpVector(0, 0, 1);
        myAtmosphere->SetRightVector(1, 0, 0);

        // Set our location (change this to your own latitude and longitude)
        SilverLining::Location loc;
        loc.SetAltitude(0);
        loc.SetLatitude(45);
        loc.SetLongitude(-122);
        myAtmosphere->GetConditions()->SetLocation(loc);

        // Set the time to noon in PST
        SilverLining::LocalTime t;
        t.SetFromSystemTime();
        t.SetHour(12);
        t.SetTimeZone(PST);
        myAtmosphere->GetConditions()->SetTime(t);

        //////////////////////////////////////////////////////////////////////////////////////////
        SilverLining::CloudLayer* cumulusCongestusLayer;
        cumulusCongestusLayer = SilverLining::CloudLayerFactory::Create(CUMULUS_CONGESTUS, *myAtmosphere);
        cumulusCongestusLayer->SetIsInfinite(true);

        // common parameters to all tcs datas
        cumulusCongestusLayer->SetThickness(500);
        cumulusCongestusLayer->SetBaseLength(40000);
        cumulusCongestusLayer->SetBaseWidth(40000);
        cumulusCongestusLayer->SetDensity(0.3);

        cumulusCongestusLayer->GenerateShadowMaps(false);

        myAtmosphere->GetConditions()->AddCloudLayer(cumulusCongestusLayer);
    }
    return true;
}

SilverLiningCameraManager* AtmosphereReference::silverLiningCameraManager()
{
    return mySlCameraManager;
}
const SilverLiningCameraManager* AtmosphereReference::silverLiningCameraManager() const
{
    return mySlCameraManager;
}

SilverLining::Atmosphere* AtmosphereReference::atmosphere()
{
    return myAtmosphere;
}

const SilverLining::Atmosphere* AtmosphereReference::atmosphere() const
{
    return myAtmosphere;
}