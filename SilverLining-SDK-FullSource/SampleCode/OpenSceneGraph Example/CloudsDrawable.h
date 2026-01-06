// Copyright (c) 2008-2012 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include <osg/Drawable>
#include <osgViewer/Viewer>
#include "SilverLining.h"
#include <OpenThreads/Mutex>

// The update callback is just used to mark our bounds dirty each frame
struct SilverLiningCloudsUpdateCallback : public osg::Drawable::UpdateCallback
{
    SilverLiningCloudsUpdateCallback() {}

    virtual void update(osg::NodeVisitor*, osg::Drawable* drawable);
};

// We also hook in with a bounding box callback to tell OSG how big our cloud volumes are
struct SilverLiningCloudsComputeBoundingBoxCallback : public osg::Drawable::ComputeBoundingBoxCallback
{
    SilverLiningCloudsComputeBoundingBoxCallback() : camera(0) {}

    virtual osg::BoundingBox computeBound(const osg::Drawable&) const;

    osg::Camera *camera;
};

// The CloudsDrawable does the actual drawing of the clouds.
class CloudsDrawable : public osg::Drawable
{
public:
    CloudsDrawable();
	CloudsDrawable(osgViewer::Viewer* view);

    virtual bool isSameKindAs(const Object* obj) const {
        return dynamic_cast<const CloudsDrawable*>(obj)!=NULL;
    }
    virtual Object* cloneType() const {
        return new CloudsDrawable();
    }
    virtual Object* clone(const osg::CopyOp& copyop) const {
        return new CloudsDrawable();
    }

    virtual void drawImplementation(osg::RenderInfo& renderInfo) const;

protected:
    void initialize();

    osgViewer::Viewer* _view;
    mutable OpenThreads::Mutex _mutex;
};
