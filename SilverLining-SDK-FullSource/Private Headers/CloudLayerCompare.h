// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudLayerCompare.h
\brief CloudLayerCompare class
*/

namespace SilverLining
{
   class Camera;
   class CloudLayer;
   class ThreadCameraStreamData;

   class CloudLayerCompare
   {
   public:
       CloudLayerCompare(const Camera* camera, bool enforceStrictWeak, ThreadCameraStreamData* tcsData);
      virtual ~CloudLayerCompare();

      bool operator () (const CloudLayer* c1, const CloudLayer* c2);
   private:
      const Camera* camera;
      bool enforceStrictWeak;
      ThreadCameraStreamData* tcsData;
   };
}