/*
-----------------------------------------------------------------------------
Filename:    TutorialApplication.h
-----------------------------------------------------------------------------

This source file is part of the
   ___                 __    __ _ _    _ 
  /___\__ _ _ __ ___  / / /\ \ (_) | _(_)
 //  // _` | '__/ _ \ \ \/  \/ / | |/ / |
/ \_// (_| | | |  __/  \  /\  /| |   <| |
\___/ \__, |_|  \___|   \/  \/ |_|_|\_\_|
      |___/                              
      Tutorial Framework
      http://www.ogre3d.org/tikiwiki/
-----------------------------------------------------------------------------
*/
#ifndef __TutorialApplication_h_
#define __TutorialApplication_h_

#include "BaseApplication.h"
#include "SilverLiningOgre.h"

class SilverLiningApplication : public BaseApplication
{
public:
    SilverLiningApplication(void);
    virtual ~SilverLiningApplication(void);

protected:
    virtual void createScene(void);

    virtual void createViewports(void);

    void SetupSilverLining(void);

    MyRenderSystemListener *mRenderSystemListener;
};

#endif // #ifndef __TutorialApplication_h_
