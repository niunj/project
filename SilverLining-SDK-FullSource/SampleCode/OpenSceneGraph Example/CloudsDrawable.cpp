// Copyright (c) 2008-2012 Sundog Software, LLC. All rights reserved worldwide.

#include "CloudsDrawable.h"
#include "SilverLining.h"
#include "AtmosphereReference.h"

#include <GL/gl.h>
#include <GL/glu.h>
#include <assert.h>

using namespace SilverLining;

CloudsDrawable::CloudsDrawable()
    : osg::Drawable()
{
    initialize();
}

CloudsDrawable::CloudsDrawable(osgViewer::Viewer* view)
    : osg::Drawable()
    , _view(view)
{
    initialize();
}

// Set up our attributes and callbacks.
void CloudsDrawable::initialize()
{
    setUseVertexBufferObjects(false);
    setUseDisplayList(false);

    if (_view) {
        SilverLiningCloudsUpdateCallback *updateCallback = new SilverLiningCloudsUpdateCallback;
        setUpdateCallback(updateCallback);

        SilverLiningCloudsComputeBoundingBoxCallback *boundsCallback = new SilverLiningCloudsComputeBoundingBoxCallback;
        boundsCallback->camera = _view->getCamera();
        setComputeBoundingBoxCallback(boundsCallback);
    }
}

// Draw the clouds.
void CloudsDrawable::drawImplementation(osg::RenderInfo& renderInfo) const
{
    AtmosphereReference *ar = dynamic_cast<AtmosphereReference *>(renderInfo.getCurrentCamera()->getUserData());
    SilverLining::Atmosphere *atmosphere = 0;
    if (ar) atmosphere = ar->atmosphere;

    renderInfo.getState()->disableAllVertexArrays();

    if (atmosphere) {
        OpenThreads::ScopedLock<OpenThreads::Mutex> lock( _mutex );
        atmosphere->DrawObjects(true);
    }

    renderInfo.getState()->dirtyAllVertexArrays();
}

// We use the Atmosphere::GetCloudBounds() to get the bounds of the clouds in the scene. This is updated
// and persisted after the update pass, so it fits well with OSG's threading model.
osg::BoundingBox SilverLiningCloudsComputeBoundingBoxCallback::computeBound(const osg::Drawable&) const
{
    osg::BoundingBox box;

    if (camera) {
        AtmosphereReference *ar = dynamic_cast<AtmosphereReference *>( camera->getUserData() );
        if ( ar && ar->atmosphere) {
            SilverLining::Atmosphere * atmosphere = ar->atmosphere;

            double minX, minY, minZ, maxX, maxY, maxZ;
            atmosphere->GetCloudBounds(minX, minY, minZ, maxX, maxY, maxZ);
            osg::Vec3d min(minX, minY, minZ);
            osg::Vec3d max(maxX, maxY, maxZ);
            box = osg::BoundingBox(min, max);
        }
    }

    return box;
}

// Recompute the bounds of the clouds every frame.
void SilverLiningCloudsUpdateCallback::update(osg::NodeVisitor*, osg::Drawable* drawable)
{
    if (drawable) {
        drawable->dirtyBound();
    }
}