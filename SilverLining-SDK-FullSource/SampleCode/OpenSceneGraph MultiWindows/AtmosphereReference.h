// Copyright (c) 2008-2013 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "SilverLining.h"

#include <osg/Referenced>
#include <osg/Vec3d>
#include <osg/observer_ptr>
#include <osg/Camera>

#include <OpenThreads/Mutex>

#include <map>

namespace SilverLining
{
   class Atmosphere;
   class ThreadCameraStreamData;
}

class SilverLiningCameraManager
{
public:
   //! constructor
   SilverLiningCameraManager();

   //! destructor
   virtual ~SilverLiningCameraManager();
public:
   //! Get or create the SL tcs data for this atmosphere, given the osg camera
   virtual SilverLining::ThreadCameraStreamData* getOrCreateSilverLiningTcsData(const osg::Camera* osgCamera, const SilverLining::Atmosphere* atmosphere, bool& created);

   //! find SL tcs data for this atmosphere, given the osg camera
   virtual SilverLining::ThreadCameraStreamData* findSilverLiningTcsData(const osg::Camera* osgCamera, const SilverLining::Atmosphere* atmosphere);

   //! each layer needs to be separately seeded again for each camera/view(or equivalently tcs data)
   virtual void seedLayers(const osg::Camera* camera, SilverLining::Atmosphere* atmosphere, const osg::Vec3d& pos);
private:
   struct TcsDataAndParams
   {
      SilverLining::ThreadCameraStreamData* myTcsData;
      bool mySeeded;
   };
   typedef std::map< osg::observer_ptr<const osg::Camera>, TcsDataAndParams> MapCamerasToTcsDatas;
   MapCamerasToTcsDatas myMapOsgCamerasToTcsDatas;

   const SilverLining::Vector3 myUpVector;
   const SilverLining::Vector3 myRightVector;

   OpenThreads::Mutex  myMapMutex;

   static int theCameraId;
};

class AtmosphereReference : public osg::Referenced
{
public:
   AtmosphereReference(SilverLining::Atmosphere* atmosphere);
   virtual ~AtmosphereReference();
public:
   //! initialize if not initialized
   virtual bool initializeIfNecessary(const osg::Vec3d& pos);

   //! get the camera manager (non-const)
   virtual SilverLiningCameraManager* silverLiningCameraManager();

   //! get the camera manager (const)
   virtual const SilverLiningCameraManager* silverLiningCameraManager() const;

   //! get the Atmosphere (non-const)
   virtual SilverLining::Atmosphere* atmosphere();

   //! get the Atmosphere (const)
   virtual const SilverLining::Atmosphere* atmosphere() const;

public:
   SilverLining::Atmosphere* myAtmosphere;
private:

   SilverLiningCameraManager* mySlCameraManager;

   bool myAtmosphereInitialized;

   OpenThreads::Mutex  myMutex;
};
