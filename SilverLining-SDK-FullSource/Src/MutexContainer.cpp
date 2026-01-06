// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.
#include "MutexContainer.h"
#include "Mutex.h"

namespace SilverLining
{
MutexContainer::MutexContainer()
{
    mutex = SL_NEW Mutex();
}
MutexContainer::~MutexContainer()
{
    SL_DELETE mutex;
}
}