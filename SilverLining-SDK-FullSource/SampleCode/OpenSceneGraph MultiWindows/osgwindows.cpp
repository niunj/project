/* OpenSceneGraph example, osgwindows.
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
*  THE SOFTWARE.
*/

#include <osgDB/ReadFile>
#include <osgUtil/Optimizer>
#include <osgViewer/CompositeViewer>
#include <osg/Depth>
#include <osgViewer/ViewerEventHandlers>

#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/TerrainManipulator>


#include <iostream>
#include "SilverLining.h"
#include "SkyDrawable.h"
#include "AtmosphereReference.h"

#include "../SamplesCommon/Licenses.h"

/** Here's the meat of integrating SilverLining with an OSG Viewer, using the SkyDrawable and
CloudsDrawable classes provided. This method (c) 2013 Sundog Software LLC. */
void integrateSilverLining(osg::ref_ptr<osg::Node> sceneGraphRoot, osgViewer::CompositeViewer& viewer)
{
    AtmosphereReference* atmosphereReference = new AtmosphereReference(new SilverLining::Atmosphere(SILVERLINING_LICENSE_USER, SILVERLINING_LICENSE_CODE));

    for (unsigned int j = 0; j < viewer.getNumViews(); j++) {
        osgViewer::View *view = viewer.getView(j);
        unsigned int iNSlaves = view->getNumSlaves();
        unsigned int i;

        for (i=0; i<iNSlaves; i++) {

            view->getSlave(i)._camera->setClearMask(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            view->getSlave(i)._camera->setNearFarRatio(0.00002);
            view->getSlave(i)._camera->setUserData(atmosphereReference);
            // Make sure OSG queries our Drawables to get the proper near and far clip planes. Or, you could manually
            // set the planes to values large enough to encompass the skybox and clouds.
            view->getSlave(i)._camera->setComputeNearFarMode(osg::CullSettings::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES);

            // Add the sky (calls Atmosphere::DrawSky and handles initialization once you're in
            // the rendering thread)
            osg::Geode *skyGeode = new osg::Geode;
            SkyDrawable *skyDrawable = new SkyDrawable(view);
            // ***IMPORTANT!**** Check that the path to the resources folder for SilverLining in SkyDrawable.cpp
            // SkyDrawable::initializeSilverLining matches with where you installed SilverLining.

            skyGeode->addDrawable(skyDrawable);
            skyGeode->setCullingActive(false); // The skybox is always visible.

            // Use a RenderBin to enforce that the scene gets drawn first, then the sky and clouds
            skyGeode->getOrCreateStateSet()->setRenderBinDetails(99, "RenderBin");
            skyGeode->getOrCreateStateSet()->setAttributeAndModes( new osg::Depth( osg::Depth::LEQUAL, 0.0, 1.0, false ) );

            // Add the models
            sceneGraphRoot.get()->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

            // Add our sky and clouds into the scene.
            sceneGraphRoot->asGroup()->addChild(skyGeode);
        }

        // Pick an Atmosphere to associate with the main camera, for use in the update thread for computing bounds etc.
        view->getCamera()->setUserData(atmosphereReference);

        view->getCamera()->setComputeNearFarMode(osg::CullSettings::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES);
        view->getCamera()->setNearFarRatio(0.00002);
    }

}
int main( int argc, char **argv )
{
    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc,argv);

    // read the scene from the list of file specified commandline args.
    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFiles(arguments);

    // if not loaded assume no arguments passed in, try use default mode instead.
    if (!loadedModel) loadedModel = osgDB::readNodeFile("lz.osgt");

    // if no model has been successfully loaded report failure.
    if (!loadedModel) {
        std::cout << arguments.getApplicationName() <<": No data loaded" << std::endl;
        return 1;
    }

    // construct the viewer.
    osgViewer::CompositeViewer viewer;

    //viewer.setLightingMode(osgViewer::View::SKY_LIGHT);

    int xoffset = 40;
    int yoffset = 40;
    osgViewer::View* view1 = NULL;
    osgViewer::View* view2 = NULL;

    // left window + left slave camera
    {
        osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
        traits->x = xoffset + 0;
        traits->y = yoffset + 0;
        traits->width = 1024;
        traits->height = 768;
        traits->windowDecoration = true;
        traits->doubleBuffer = true;
        traits->sharedContext = 0;

        osg::ref_ptr<osg::GraphicsContext> gc = osg::GraphicsContext::createGraphicsContext(traits.get());

        osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        camera->setGraphicsContext(gc.get());
        camera->setViewport(new osg::Viewport(0,0, traits->width, traits->height));
        GLenum buffer = traits->doubleBuffer ? GL_BACK : GL_FRONT;
        camera->setDrawBuffer(buffer);
        camera->setReadBuffer(buffer);

        view1 = new osgViewer::View;
        view1->setName("View one");
        view1->setCameraManipulator(new osgGA::TrackballManipulator);
        view1->addSlave(camera.get(), camera->getProjectionMatrix(),osg::Matrixd(), true);

        viewer.addView(view1);
    }

    // right window + right slave camera
    {
        osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
        traits->x = xoffset + 1024;
        traits->y = yoffset + 0;
        traits->width = 1024;
        traits->height = 768;
        traits->windowDecoration = true;
        traits->doubleBuffer = true;
        traits->sharedContext = 0;

        osg::ref_ptr<osg::GraphicsContext> gc = osg::GraphicsContext::createGraphicsContext(traits.get());

        osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        camera->setGraphicsContext(gc.get());
        camera->setViewport(new osg::Viewport(0,0, traits->width, traits->height));
        GLenum buffer = traits->doubleBuffer ? GL_BACK : GL_FRONT;
        camera->setDrawBuffer(buffer);
        camera->setReadBuffer(buffer);


        view2 = new osgViewer::View;
        view2->setName("View one");
        view2->setCameraManipulator(new osgGA::TrackballManipulator);
        view2->addSlave(camera.get(), camera->getProjectionMatrix(),osg::Matrixd(), true);

        viewer.addView(view2);
    }

    osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;

    keyswitchManipulator->addMatrixManipulator( '1', "Trackball", new osgGA::TrackballManipulator() );

    view1->setCameraManipulator( keyswitchManipulator.get() );
    view2->setCameraManipulator( keyswitchManipulator.get() );

    // Any of these threading models can be used
    //viewer.setThreadingModel(osgViewer::ViewerBase::SingleThreaded);
    //viewer.setThreadingModel(osgViewer::ViewerBase::CullDrawThreadPerContext);
    //viewer.setThreadingModel(osgViewer::ViewerBase::DrawThreadPerContext);
    viewer.setThreadingModel(osgViewer::ViewerBase::CullThreadPerCameraDrawThreadPerContext);

    // optimize the scene graph, remove rendundant nodes and state etc.
    osgUtil::Optimizer optimizer;
    optimizer.optimize(loadedModel.get());

    // set the scene to render
    view1->setSceneData(loadedModel.get());
    view2->setSceneData(loadedModel.get());

    integrateSilverLining(loadedModel, viewer);

    viewer.realize();

    while(!viewer.done()) {
        viewer.frame();
    }

    return 0;//viewer.run();
}

