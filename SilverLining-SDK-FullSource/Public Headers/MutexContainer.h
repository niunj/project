// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file MutexContainer.h
\brief A class for RAII Mutex
*/
#pragma once

#ifdef SWIG
%module SilverLiningMutexContainer
#define SILVERLINING_API
%{
#include "MutexContainer.h"
    using namespace SilverLining;
    % }
#endif

#include "MemAlloc.h"

namespace SilverLining
{
    class Mutex;
    class MutexContainer
    {
    public:
        MutexContainer();
        ~MutexContainer();
    public:
        Mutex* mutex;
    };
}
