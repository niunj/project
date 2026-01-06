// Copyright (c) 2008-2012 Sundog Software, LLC. All rights reserved worldwide.

#include "SkyDrawable.h"
#include "SilverLining.h"
#include "AtmosphereReference.h"

#include <GL/gl.h>
#include <GL/glu.h>
#include <assert.h>

using namespace SilverLining;

SkyDrawable::SkyDrawable()
    : osg::Drawable()
    , _view(0)
    , _skyboxSize(100000)
{
}

SkyDrawable::SkyDrawable(osgViewer::Viewer* view)
    : osg::Drawable()
    , _view(view)
    , _skyboxSize(100000)
{
    initializeDrawable();
}

void SkyDrawable::initializeDrawable()
{
    setDataVariance(osg::Object::DYNAMIC);
    setUseVertexBufferObjects(false);
    setUseDisplayList(false);

    cullCallback = new SilverLiningCullCallback();
    setCullCallback(cullCallback);

    updateCallback = new SilverLiningUpdateCallback();
    updateCallback->camera = _view->getCamera();
    setUpdateCallback(updateCallback);

    computeBoundingBoxCallback = new SilverLiningSkyComputeBoundingBoxCallback();
    computeBoundingBoxCallback->camera = _view->getCamera();
    setComputeBoundingBoxCallback(computeBoundingBoxCallback);
}

void SkyDrawable::setLighting(SilverLining::Atmosphere *atmosphere) const
{
    osg::Light *light = _view->getLight();
    osg::Vec4 ambient, diffuse;
    osg::Vec3 direction;

    if (atmosphere && light) {
        float ra, ga, ba, rd, gd, bd, x, y, z;
        atmosphere->GetAmbientColor(&ra, &ga, &ba);
        atmosphere->GetSunOrMoonColor(&rd, &gd, &bd);
        atmosphere->GetSunOrMoonPosition(&x, &y, &z);

        direction = osg::Vec3(x, y, z);
        ambient = osg::Vec4(ra, ga, ba, 1.0);
        diffuse = osg::Vec4(rd, gd, bd, 1.0);

        direction.normalize();

        light->setAmbient(ambient);
        light->setDiffuse(diffuse);
        light->setSpecular(osg::Vec4(0,0,0,1));
        light->setPosition(osg::Vec4(direction.x(), direction.y(), direction.z(), 0));
    }
}

void SkyDrawable::initializeSilverLining(AtmosphereReference *ar) const
{
    if (ar && !ar->atmosphereInitialized) {
        ar->atmosphereInitialized = true; // only try once.
        SilverLining::Atmosphere *atmosphere = ar->atmosphere;

        if (atmosphere) {
            srand(1234); // constant random seed to ensure consistent clouds across windows

            // Update the path below to where you installed SilverLining's resources folder.
            const char *slPath = getenv("SILVERLINING_PATH");
            if (!slPath) {
                printf("Can't find SilverLining; set the SILVERLINING_PATH environment variable ");
                printf("to point to the directory containing the SDK.\n");
                exit(0);
            }

            std::string resPath(slPath);
#ifdef _WIN32
            resPath += "\\Resources\\";
#else
            resPath += "/Resources/";
#endif
            int ret = atmosphere->Initialize(SilverLining::Atmosphere::OPENGL, resPath.c_str(),
                                             true, 0);
            if (ret != SilverLining::Atmosphere::E_NOERROR) {
                printf("SilverLining failed to initialize; error code %d.\n", ret);
                printf("Check that the path to the SilverLining installation directory is set properly ");
                printf("in SkyDrawable.cpp (in SkyDrawable::initializeSilverLining)\n");
                exit(0);
            }

            // Let SilverLining know which way is up. OSG usually has Z going up.
            atmosphere->SetUpVector(0, 0, 1);
            atmosphere->SetRightVector(1, 0, 0);

            // Set our location (change this to your own latitude and longitude)
            SilverLining::Location loc;
            loc.SetAltitude(0);
            loc.SetLatitude(45);
            loc.SetLongitude(-122);
            atmosphere->GetConditions()->SetLocation(loc);

            // Set the time to noon in PST
            SilverLining::LocalTime t;
            t.SetFromSystemTime();
            t.SetHour(12);
            t.SetTimeZone(PST);
            atmosphere->GetConditions()->SetTime(t);

            // Center the clouds around the camera's initial position
            osg::Vec3d pos = _view->getCameraManipulator()->getMatrix().getTrans();

            SilverLining::CloudLayer *cumulusCongestusLayer;
            cumulusCongestusLayer = SilverLining::CloudLayerFactory::Create(CUMULUS_CONGESTUS, *atmosphere);
            cumulusCongestusLayer->SetIsInfinite(true);
            cumulusCongestusLayer->SetBaseAltitude(4000);
            cumulusCongestusLayer->SetThickness(500);
            cumulusCongestusLayer->SetBaseLength(40000);
            cumulusCongestusLayer->SetBaseWidth(40000);
            cumulusCongestusLayer->SetDensity(0.8);
            cumulusCongestusLayer->SetAlpha(0.8);
            cumulusCongestusLayer->SetFadeTowardEdges(true);

            // Note, we pass in X and -Y since this accepts "east" and "south" coordinates.
            cumulusCongestusLayer->SetLayerPosition(pos.x(), -pos.y());
            cumulusCongestusLayer->SeedClouds(*atmosphere);

            atmosphere->GetConditions()->AddCloudLayer(cumulusCongestusLayer);

            cullCallback->atmosphere = atmosphere;
        }
    }
}

void SkyDrawable::drawImplementation(osg::RenderInfo& renderInfo) const
{
    SilverLining::Atmosphere *atmosphere = 0;

    AtmosphereReference *ar = dynamic_cast<AtmosphereReference *>(renderInfo.getCurrentCamera()->getUserData());
    if (ar) atmosphere = ar->atmosphere;

    renderInfo.getState()->disableAllVertexArrays();

    if (atmosphere) {
        initializeSilverLining(ar);

        osg::Matrix projMat = renderInfo.getState()->getProjectionMatrix();
        atmosphere->SetProjectionMatrix(projMat.ptr());
        osg::Matrix viewMat = renderInfo.getCurrentCamera()->getViewMatrix();
        atmosphere->SetCameraMatrix(viewMat.ptr());

        atmosphere->DrawSky(true, false, _skyboxSize, true, false);
        setLighting(atmosphere);
    }

    renderInfo.getState()->dirtyAllVertexArrays();
}

void SilverLiningUpdateCallback::update(osg::NodeVisitor*, osg::Drawable* drawable)
{
    SilverLining::Atmosphere *atmosphere = 0;
    AtmosphereReference *ar = dynamic_cast<AtmosphereReference *>(camera->getUserData());
    if (ar) {
        if (!ar->atmosphereInitialized) return;

        atmosphere = ar->atmosphere;
    }

    //if (atmosphere) {
    //    atmosphere->UpdateSkyAndClouds();
    //}

    // Since the skybox bounds are a function of the camera position, always update the bounds.
    drawable->dirtyBound();
}

osg::BoundingBox SilverLiningSkyComputeBoundingBoxCallback::computeBound(const osg::Drawable& drawable) const
{
    osg::BoundingBox box;

    if (camera) {
        const SkyDrawable& skyDrawable = dynamic_cast<const SkyDrawable&>(drawable);

        SilverLining::Atmosphere *atmosphere = 0;
        AtmosphereReference *ar = dynamic_cast<AtmosphereReference *>(camera->getUserData());
        if (ar) {
            atmosphere = ar->atmosphere;
        }

        if (atmosphere) {
            double skyboxSize;

            if (skyDrawable.getSkyboxSize() != 0.0) {
                skyboxSize = skyDrawable.getSkyboxSize();
            } else {
                skyboxSize = atmosphere->GetConfigOptionDouble("sky-box-size");
                if (skyboxSize == 0.0) skyboxSize = 1000.0;
            }

            double radius = skyboxSize * 0.5;
            osg::Vec3f eye, center, up;
            camera->getViewMatrixAsLookAt(eye, center, up);
            osg::Vec3d camPos = eye;
            osg::Vec3d min(camPos.x() - radius, camPos.y() - radius, camPos.z() - radius);
            osg::Vec3d max(camPos.x() + radius, camPos.y() + radius, camPos.z() + radius);

            box.set(min, max);

            double dToOrigin = camPos.length();

            bool hasLimb = atmosphere->GetConfigOptionBoolean("enable-atmosphere-from-space");
            if (hasLimb) {
                // Compute bounds of atmospheric limb centered at 0,0,0
                double earthRadius = atmosphere->GetConfigOptionDouble("earth-radius-meters");
                double atmosphereHeight = earthRadius +
                                          + atmosphere->GetConfigOptionDouble("atmosphere-height");
                double atmosphereThickness = atmosphere->GetConfigOptionDouble("atmosphere-scale-height-meters")
                                             + earthRadius;

                osg::BoundingBox atmosphereBox;
                osg::Vec3d atmMin(-atmosphereThickness, -atmosphereThickness, -atmosphereThickness);
                osg::Vec3d atmMax(atmosphereThickness, atmosphereThickness, atmosphereThickness);

                // Expand these bounds by it
                box.expandBy(atmosphereBox);
            }
        }
    }

    return box;
}