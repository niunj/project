// Copyright 2006-2021 Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudCompilationOperation.h
\brief A class for expressing a run time cloud compilation operation
*/

#pragma once
#include "MemAlloc.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class CloudCompilationOperation : public MemObject
    {
    public:
        virtual void Execute(ThreadCameraStreamData* tcsData) = 0;
    };
}

