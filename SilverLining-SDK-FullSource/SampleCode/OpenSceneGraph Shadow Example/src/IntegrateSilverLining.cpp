#include <osgDB/ReadFile>
#include <osgViewer/Viewer>
#include "SilverLining.h"
#include "SkyDrawable.h"
#include "CloudsDrawable.h"
#include "AtmosphereReference.h"
#include "SilverLiningProjectionMatrixCallback.h"

#include "../../SamplesCommon/Licenses.h"

#include <iostream>

void integrateSilverLining(osg::ref_ptr<osg::Node> sceneGraphRoot, osgViewer::Viewer& viewer)
{
    // No need for OSG to clear the color buffer, the sky will fill it for you.
//    viewer.getCamera()->setClearMask(0);

    // Make sure lighting mode is correct for setting sun direction in SilverLining's world space
    viewer.setLightingMode(osgViewer::View::SKY_LIGHT);

    // Instantiate an Atmosphere and associate it with this camera. If you have multiple cameras
    // in multiple contexts, be sure to instantiate seperate Atmosphere objects for each.
    SilverLining::Atmosphere *atm = new SilverLining::Atmosphere(SILVERLINING_LICENSE_USER, SILVERLINING_LICENSE_CODE);

    // Add the sky (calls Atmosphere::DrawSky and handles initialization once you're in
    // the rendering thread)
    osg::Geode *skyGeode = new osg::Geode;
    SkyDrawable *skyDrawable = new SkyDrawable(&viewer);

    // ***IMPORTANT!**** Check that the path to the resources folder for SilverLining in SkyDrawable.cpp
    // SkyDrawable::initializeSilverLining matches with where you installed SilverLining.

    skyGeode->addDrawable(skyDrawable);
    skyGeode->setCullingActive(false);

    /* If you let OSG auto-calculate the near and far clip planes; it'll exclude the
       sky box and clouds. One solution is to set the near and far clip planes explicitly
       like this:
    */
    /*
        viewer.getCamera()->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
        double fovy, aspect, zNear, zFar;
        viewer.getCamera()->getProjectionMatrixAsPerspective(fovy, aspect, zNear, zFar);
        viewer.getCamera()->setProjectionMatrixAsPerspective(fovy, aspect, 10, 100000);
    */
    /*
          or, you can use the included projection matrix callback to intercept how OSG computes
       the near and far clip planes and take SilverLining's objects into account, like this:
    */

    SilverLiningProjectionMatrixCallback *cb = new SilverLiningProjectionMatrixCallback(
        atm, viewer.getCamera());
    viewer.getCamera()->setClampProjectionMatrixCallback(cb);
    cb->setSkyDrawable(skyDrawable);


    AtmosphereReference *ar = new AtmosphereReference;
    ar->atmosphere = atm;
    viewer.getCamera()->setUserData(ar);

    // Use a RenderBin to enforce that the sky gets drawn first, then the scene, then the clouds
    skyGeode->getOrCreateStateSet()->setRenderBinDetails(-1, "RenderBin");

    // Add the models
    sceneGraphRoot.get()->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

    // Add the clouds (note, you need this even if you don't want clouds - it calls
    // Atmosphere::DrawObjects() )
    osg::Geode *cloudsGeode = new osg::Geode;
    cloudsGeode->addDrawable(new CloudsDrawable(&viewer));
    cloudsGeode->getOrCreateStateSet()->setRenderBinDetails(99, "RenderBin");
    cloudsGeode->setCullingActive(false);

    viewer.getSceneData()->asGroup()->addChild(skyGeode);
    viewer.getSceneData()->asGroup()->addChild(cloudsGeode);
}
