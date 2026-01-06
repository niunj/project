// Copyright 2006-2021 Sundog Software, LLC. All rights reserved worldwide.

/** \file TcsUserData.h
\brief A class for describing and managing data/rendering in a particular thread and associated camera, stream
*/

#pragma once
#include "MemAlloc.h"
#include "MutexContainer.h"

namespace SilverLining
{
    //! abstract user data interface associated with the ThreadCameraStreamData
    class TcsUserData : public MemObject
    {
    public:
        TcsUserData();
        virtual ~TcsUserData();

#define SILVERLINING_TRACK_TCSUSERDATAS 0
#if SILVERLINING_TRACK_TCSUSERDATAS
        static MutexContainer countMutexContainer;
        static int numTcsUserDatas; // total num  of cloud layer tcs user datas
        int id;// internal id
#endif
    };
}