// Copyright (c) 2008-2013 Sundog Software, LLC. All rights reserved worldwide.

#include "SkyDrawable.h"
#include "SilverLining.h"
#include "AtmosphereReference.h"

#include <assert.h>
//#include <thread>

using namespace SilverLining;

SkyDrawable::SkyDrawable()
    : osg::Drawable()
    , myView(0)
    , mySkyBoxSize(100000)
{
    // nothing else
}

SkyDrawable::SkyDrawable(osgViewer::View* view)
    : osg::Drawable()
    , myView(view)
    , mySkyBoxSize(100000)
{
    initializeDrawable();
}

void SkyDrawable::initializeDrawable()
{
    setDataVariance(osg::Object::DYNAMIC);
    setUseVertexBufferObjects(false);
    setUseDisplayList(false);

    myUpdateCallback = new SilverLiningUpdateCallback();
    setUpdateCallback(myUpdateCallback);

    myComputeBoundingBoxCallback = new SilverLiningSkyComputeBoundingBoxCallback();
    setComputeBoundingBoxCallback(myComputeBoundingBoxCallback);
}

void SkyDrawable::setLighting(SilverLining::Atmosphere *atmosphere, SilverLining::ThreadCameraStreamData* tcsData) const
{
    osg::Light *light = myView->getLight();
    osg::Vec4 ambient, diffuse;
    osg::Vec3 direction;

    if (atmosphere && light) {
        float ra, ga, ba, rd, gd, bd, x, y, z;
        atmosphere->GetAmbientColor(&ra, &ga, &ba, tcsData);
        atmosphere->GetSunOrMoonColor(&rd, &gd, &bd, tcsData);
        atmosphere->GetSunOrMoonPosition(&x, &y, &z, tcsData);

        direction = osg::Vec3(x, y, z);
        ambient = osg::Vec4(ra, ga, ba, 1.0);
        diffuse = osg::Vec4(rd, gd, bd, 1.0);

        direction.normalize();

        light->setAmbient(ambient);
        light->setDiffuse(diffuse);
        light->setSpecular(osg::Vec4(0, 0, 0, 1));
        light->setPosition(osg::Vec4(direction.x(), direction.y(), direction.z(), 0));
    }
}

void SkyDrawable::drawImplementation(osg::RenderInfo& renderInfo) const
{
    renderInfo.getState()->disableAllVertexArrays();

    // Center the clouds around the camera's initial position
    const osg::Camera* osgCamera = renderInfo.getCurrentCamera();

    osg::Vec3d osgCameraPosition;
    osg::Vec3d osgCenter, osgUp;

    osgCamera->getViewMatrixAsLookAt(osgCameraPosition, osgCenter, osgUp);

    AtmosphereReference* atmosphereReference = dynamic_cast<AtmosphereReference*>(renderInfo.getCurrentCamera()->getUserData());
    if (atmosphereReference == 0) {
        std::cout << "atmosphereReference==0" << std::endl;
        return;
    }

    atmosphereReference->initializeIfNecessary(osgCameraPosition);

    SilverLining::Atmosphere* atmosphere = atmosphereReference->atmosphere();

    if (atmosphere && atmosphere->IsInitialized()) {
        bool created = false;
        SilverLining::ThreadCameraStreamData* tcsData = atmosphereReference->silverLiningCameraManager()->getOrCreateSilverLiningTcsData(osgCamera, atmosphere, created);
        if (tcsData == 0) {
            std::cout << "(tcsData == 0)" << std::endl;
            return;
        }

        SilverLining::Camera* silverLiningCamera = tcsData->GetCamera();

        const osg::Matrix& projectionMatrix = renderInfo.getState()->getProjectionMatrix();
        const osg::Matrix& modelViewMatrix = renderInfo.getCurrentCamera()->getViewMatrix();

        SilverLining::Matrix4 slProjectionMatrix(projectionMatrix.ptr());
        slProjectionMatrix.Transpose();
        silverLiningCamera->SetProjectionMatrix(slProjectionMatrix);

        SilverLining::Matrix4 slModelViewMatrix(modelViewMatrix.ptr());
        slModelViewMatrix.Transpose();
        silverLiningCamera->SetModelViewMatrix(slModelViewMatrix);

        if (created) {
            atmosphereReference->silverLiningCameraManager()->seedLayers(osgCamera, atmosphere, osgCameraPosition);
        }

        const osg::Viewport* osgViewPort = osgCamera->getViewport();
        if (osgViewPort) {
            silverLiningCamera->SetViewport(osgViewPort->x(), osgViewPort->y()
                                            , osgViewPort->width(), osgViewPort->height());
        }
        // for debugging
        //std::thread::id this_id = std::this_thread::get_id();
        //std::cout << "id: " << this_id << std::endl;

        atmosphere->DrawSky(true, false, mySkyBoxSize
                            , true, false, true
                            , 0, 0, 0, tcsData);

        atmosphere->DrawObjects(true, false
                                , true, 0.0f, false
                                , 0, true, true
                                , true, false, tcsData);

        setLighting(atmosphere, tcsData);
    }

    renderInfo.getState()->dirtyAllVertexArrays();
}

SilverLiningUpdateCallback::SilverLiningUpdateCallback()
{
    // nothing else
}

void SilverLiningUpdateCallback::update(osg::NodeVisitor*, osg::Drawable* drawable)
{
    // Since the skybox bounds are a function of the camera position, always update the bounds.
    drawable->dirtyBound();
}

SilverLiningSkyComputeBoundingBoxCallback::SilverLiningSkyComputeBoundingBoxCallback()
{
    // nothing else
}

osg::BoundingBox SilverLiningSkyComputeBoundingBoxCallback::computeBound(const osg::Drawable& drawable) const
{
    const SkyDrawable& skyDrawable = dynamic_cast<const SkyDrawable&>(drawable);

    double skyBoxSize = skyDrawable.getSkyboxSize();

    osg::Vec3d vMin = osg::Vec3d(-skyBoxSize, -skyBoxSize, -skyBoxSize);
    osg::Vec3d vMax = osg::Vec3d(skyBoxSize, skyBoxSize, skyBoxSize);
    return osg::BoundingBox(vMin, vMax);

}