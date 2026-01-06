// Copyright (c) 2008-2013 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include <osg/Drawable>
#include <osgViewer/Viewer>
#include "SilverLining.h"

class AtmosphereReference;
class SkyDrawable;

// Our update callback just marks our bounds dirty each frame (since they move with the camera.)
struct SilverLiningUpdateCallback : public osg::Drawable::UpdateCallback
{
   SilverLiningUpdateCallback();

   virtual void update(osg::NodeVisitor*, osg::Drawable* drawable);
};

// We also hook in with a bounding box callback to tell OSG how big our sky is
struct SilverLiningSkyComputeBoundingBoxCallback : public osg::Drawable::ComputeBoundingBoxCallback
{
   SilverLiningSkyComputeBoundingBoxCallback();
   virtual osg::BoundingBox computeBound(const osg::Drawable&) const;

};

// The SkyDrawable wraps SilverLining to handle updates, culling, and drawing of the skybox - and works together with a CloudsDrawable
// to draw the clouds toward the end of the scene.
class SkyDrawable : public osg::Drawable
{
public:
   SkyDrawable();
   SkyDrawable(osgViewer::View* view);

   virtual bool isSameKindAs(const Object* obj) const {
      return dynamic_cast<const SkyDrawable*>(obj)!=NULL;
   }
   virtual Object* cloneType() const {
      return new SkyDrawable();
   }
   virtual Object* clone(const osg::CopyOp& copyop) const {
      return new SkyDrawable();
   }

   void setSkyboxSize(double size) {mySkyBoxSize = size;}

   double getSkyboxSize() const {return mySkyBoxSize;}

   virtual void drawImplementation(osg::RenderInfo& renderInfo) const;

protected:
   void setLighting(SilverLining::Atmosphere *atmosphere, SilverLining::ThreadCameraStreamData* tcsData) const;
   void initializeSilverLiningIfNecessary(AtmosphereReference *ar) const;
   void initializeDrawable();
protected:
   osgViewer::View* myView;
   double mySkyBoxSize;

   SilverLiningUpdateCallback* myUpdateCallback;
   SilverLiningSkyComputeBoundingBoxCallback* myComputeBoundingBoxCallback;
};
