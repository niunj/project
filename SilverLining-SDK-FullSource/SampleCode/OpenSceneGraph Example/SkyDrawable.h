// Copyright (c) 2008-2012 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include <osg/Drawable>
#include <osgViewer/Viewer>
#include "SilverLining.h"

class AtmosphereReference;
class SkyDrawable;

// SilverLining now supports separate cull and update methods, so we hook into OSG's cull and update
// passes using callbacks on our Drawable object.
struct SilverLiningCullCallback : public osg::Drawable::CullCallback
{
    SilverLiningCullCallback() : atmosphere(0) {}

    virtual bool cull(osg::NodeVisitor* nv, osg::Drawable* drawable, osg::RenderInfo* renderInfo) const 
    { 
        if (atmosphere)  {
            atmosphere->CullObjects();
        }
        return false; 
    }

    SilverLining::Atmosphere *atmosphere;
};

// Our update callback just marks our bounds dirty each frame (since they move with the camera.)
struct SilverLiningUpdateCallback : public osg::Drawable::UpdateCallback
{
    SilverLiningUpdateCallback() : camera(0) {}

    virtual void update(osg::NodeVisitor*, osg::Drawable* drawable);

    osg::Camera *camera;
};

// We also hook in with a bounding box callback to tell OSG how big our skybox is, plus the
// atmospheric limb if applicable.
struct SilverLiningSkyComputeBoundingBoxCallback : public osg::Drawable::ComputeBoundingBoxCallback
{
    SilverLiningSkyComputeBoundingBoxCallback() : camera(0) {}

    virtual osg::BoundingBox computeBound(const osg::Drawable&) const;

    osg::Camera *camera;
};

// The SkyDrawable wraps SilverLining to handle updates, culling, and drawing of the skybox - and works together with a CloudsDrawable
// to draw the clouds toward the end of the scene.
class SkyDrawable : public osg::Drawable
{
public:
	SkyDrawable();
    SkyDrawable(osgViewer::Viewer* view);

    virtual bool isSameKindAs(const Object* obj) const {
        return dynamic_cast<const SkyDrawable*>(obj)!=NULL;
    }
    virtual Object* cloneType() const {
        return new SkyDrawable();
    }
    virtual Object* clone(const osg::CopyOp& copyop) const {
        return new SkyDrawable();
    }

	void setSkyboxSize(double size) {_skyboxSize = size;}

    double getSkyboxSize() const {return _skyboxSize;}

    virtual void drawImplementation(osg::RenderInfo& renderInfo) const;

protected:
	void setLighting(SilverLining::Atmosphere *atm) const;
    void initializeSilverLining(AtmosphereReference *ar) const;
    void initializeDrawable();

    osgViewer::Viewer* _view;
	double _skyboxSize;

    SilverLiningCullCallback *cullCallback;
    SilverLiningUpdateCallback *updateCallback;
    SilverLiningSkyComputeBoundingBoxCallback *computeBoundingBoxCallback;
};
