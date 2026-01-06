/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This application is open source and may be redistributed and/or modified
 * freely and without restriction, both in commericial and non commericial applications,
 * as long as this copyright notice is maintained.
 *
 * This application is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <osgDB/ReadFile>
#include <osgUtil/Optimizer>
#include <osg/CoordinateSystemNode>
#include <osg/Depth>

#include <osg/Switch>
#include <osgText/Text>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/TerrainManipulator>

#include "SilverLining.h"
#include "SkyDrawable.h"
#include "CloudsDrawable.h"
#include "AtmosphereReference.h"

#include "../SamplesCommon/Licenses.h"

#include <iostream>

/** Here's the meat of integrating SilverLining with an OSG Viewer, using the SkyDrawable and
    CloudsDrawable classes provided. This method (c) 2012 Sundog Software LLC. */
void integrateSilverLining(osg::ref_ptr<osg::Node> sceneGraphRoot, osgViewer::Viewer& viewer)
{
    // No need for OSG to clear the color buffer, the sky will fill it for you.
    viewer.getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // configure the near/far so we don't clip things that are up close
    viewer.getCamera()->setNearFarRatio(0.00002);

    // Make sure lighting mode is correct for setting sun direction in SilverLining's world space
    viewer.setLightingMode(osgViewer::View::SKY_LIGHT);

    // Instantiate an Atmosphere and associate it with this camera. If you have multiple cameras
    // in multiple contexts, be sure to instantiate seperate Atmosphere objects for each.
    // Remember to delete this object at shutdown.
    SilverLining::Atmosphere *atm = new SilverLining::Atmosphere(SILVERLINING_LICENSE_USER, SILVERLINING_LICENSE_CODE);

    AtmosphereReference *ar = new AtmosphereReference;
    ar->atmosphere = atm;
    viewer.getCamera()->setUserData(ar);

    // Add the sky (calls Atmosphere::DrawSky and handles initialization once you're in
    // the rendering thread)
    osg::Geode *skyGeode = new osg::Geode;
    SkyDrawable *skyDrawable = new SkyDrawable(&viewer);

    // ***IMPORTANT!**** Check that the path to the resources folder for SilverLining in SkyDrawable.cpp
    // SkyDrawable::initializeSilverLining matches with where you installed SilverLining.

    skyGeode->addDrawable(skyDrawable);
    skyGeode->setCullingActive(false); // The skybox is always visible.

    // Make sure OSG queries our Drawables to get the proper near and far clip planes. Or, you could manually
    // set the planes to values large enough to encompass the skybox and clouds.
    viewer.getCamera()->setComputeNearFarMode(osg::CullSettings::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES);

    // Use a RenderBin to enforce that the scene gets drawn first, then the sky, then the clouds
    skyGeode->getOrCreateStateSet()->setRenderBinDetails(98, "RenderBin");
    skyGeode->getOrCreateStateSet()->setAttributeAndModes( new osg::Depth( osg::Depth::LEQUAL, 0.0, 1.0, false ) );

    // Add the models
    sceneGraphRoot.get()->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

    // Add the clouds (note, you need this even if you don't have clouds in your scene - it calls
    // Atmosphere::DrawObjects() which also draws precipitation, lens flare, etc.)
    osg::Geode *cloudsGeode = new osg::Geode;
    CloudsDrawable * cloudsDrawable = new CloudsDrawable(&viewer);
    cloudsGeode->addDrawable(cloudsDrawable);
    cloudsGeode->getOrCreateStateSet()->setRenderBinDetails(99, "RenderBin");
    cloudsGeode->setCullingActive(false);

    // Add our sky and clouds into the scene.
    viewer.getSceneData()->asGroup()->addChild(skyGeode);
    viewer.getSceneData()->asGroup()->addChild(cloudsGeode);
}

int main(int argc, char** argv)
{
    // Our code doesn't handle multiple displays; you need to associate an Atmosphere with each context in
    // that case. So, we'll force a single window / context.
    putenv("OSG_WINDOW=20 30 1024 768");

    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc,argv);

    arguments.getApplicationUsage()->setApplicationName(arguments.getApplicationName());
    arguments.getApplicationUsage()->setDescription(arguments.getApplicationName()+" is the standard OpenSceneGraph example which loads and visualises 3d models.");
    arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName()+" [options] filename ...");
    arguments.getApplicationUsage()->addCommandLineOption("--image <filename>","Load an image and render it on a quad");
    arguments.getApplicationUsage()->addCommandLineOption("--dem <filename>","Load an image/DEM and render it on a HeightField");
    arguments.getApplicationUsage()->addCommandLineOption("-h or --help","Display command line parameters");
    arguments.getApplicationUsage()->addCommandLineOption("--help-env","Display environmental variables available");
    arguments.getApplicationUsage()->addCommandLineOption("--help-keys","Display keyboard & mouse bindings available");
    arguments.getApplicationUsage()->addCommandLineOption("--help-all","Display all command line, env vars and keyboard & mouse bindings.");
    arguments.getApplicationUsage()->addCommandLineOption("--SingleThreaded","Select SingleThreaded threading model for viewer.");
    arguments.getApplicationUsage()->addCommandLineOption("--CullDrawThreadPerContext","Select CullDrawThreadPerContext threading model for viewer.");
    arguments.getApplicationUsage()->addCommandLineOption("--DrawThreadPerContext","Select DrawThreadPerContext threading model for viewer.");
    arguments.getApplicationUsage()->addCommandLineOption("--CullThreadPerCameraDrawThreadPerContext","Select CullThreadPerCameraDrawThreadPerContext threading model for viewer.");

    // if user request help write it out to cout.
    bool helpAll = arguments.read("--help-all");
    unsigned int helpType = ((helpAll || arguments.read("-h") || arguments.read("--help"))? osg::ApplicationUsage::COMMAND_LINE_OPTION : 0 ) |
                            ((helpAll ||  arguments.read("--help-env"))? osg::ApplicationUsage::ENVIRONMENTAL_VARIABLE : 0 ) |
                            ((helpAll ||  arguments.read("--help-keys"))? osg::ApplicationUsage::KEYBOARD_MOUSE_BINDING : 0 );
    if (helpType) {
        arguments.getApplicationUsage()->write(std::cout, helpType);
        return 1;
    }

    osgViewer::Viewer viewer(arguments);

    // report any errors if they have occurred when parsing the program arguments.
    if (arguments.errors()) {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }

    if (arguments.argc()<=1) {
        arguments.getApplicationUsage()->write(std::cout,osg::ApplicationUsage::COMMAND_LINE_OPTION);
        return 1;
    }

    // set up the camera manipulators.
    {
        osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;

        keyswitchManipulator->addMatrixManipulator( '1', "Trackball", new osgGA::TrackballManipulator() );
        keyswitchManipulator->addMatrixManipulator( '2', "Flight", new osgGA::FlightManipulator() );
        keyswitchManipulator->addMatrixManipulator( '3', "Drive", new osgGA::DriveManipulator() );
        keyswitchManipulator->addMatrixManipulator( '4', "Terrain", new osgGA::TerrainManipulator() );

        std::string pathfile;
        char keyForAnimationPath = '5';
        while (arguments.read("-p",pathfile)) {
            osgGA::AnimationPathManipulator* apm = new osgGA::AnimationPathManipulator(pathfile);
            if (apm || !apm->valid()) {
                unsigned int num = keyswitchManipulator->getNumMatrixManipulators();
                keyswitchManipulator->addMatrixManipulator( keyForAnimationPath, "Path", apm );
                keyswitchManipulator->selectMatrixManipulator(num);
                ++keyForAnimationPath;
            }
        }

        viewer.setCameraManipulator( keyswitchManipulator.get() );
    }

    // Force multi-threaded cull / draw for testing purposes
    viewer.setThreadingModel(osgViewer::Viewer::CullThreadPerCameraDrawThreadPerContext);

    // add the state manipulator
    viewer.addEventHandler( new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()) );

    // add the thread model handler
    viewer.addEventHandler(new osgViewer::ThreadingHandler);

    // add the window size toggle handler
    viewer.addEventHandler(new osgViewer::WindowSizeHandler);

    // add the stats handler
    viewer.addEventHandler(new osgViewer::StatsHandler);

    // add the help handler
    viewer.addEventHandler(new osgViewer::HelpHandler(arguments.getApplicationUsage()));

    // add the record camera path handler
    viewer.addEventHandler(new osgViewer::RecordCameraPathHandler);

    // add the LOD Scale handler
    viewer.addEventHandler(new osgViewer::LODScaleHandler);

    // load the data
    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFiles(arguments);
    if (!loadedModel) {
        std::cout << arguments.getApplicationName() <<": No data loaded" << std::endl;
        return 1;
    }

    // any option left unread are converted into errors to write out later.
    arguments.reportRemainingOptionsAsUnrecognized();

    // report any errors if they have occurred when parsing the program arguments.
    if (arguments.errors()) {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }

    // optimize the scene graph, remove redundant nodes and state etc.
    osgUtil::Optimizer optimizer;
    optimizer.optimize(loadedModel.get());

#if 0
    viewer.getCamera()->setComputeNearFarMode( osg::Camera::DO_NOT_COMPUTE_NEAR_FAR );
    viewer.getCamera()->setProjectionMatrixAsFrustum( -1,1,-1,1,1, 50000 );
#endif

    viewer.setSceneData( loadedModel.get() );

    integrateSilverLining(loadedModel, viewer);

    viewer.realize();

    int retVal = viewer.run();

    // Delete our Atmosphere object when we're done.
    AtmosphereReference *ar = (AtmosphereReference *)(viewer.getCamera()->getUserData());
    if (ar) {
        delete ar->atmosphere;
    }
}
