// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

#include "TcsUserData.h"
#include "Mutex.h"

#include <iostream>

namespace SilverLining
{

#if SILVERLINING_TRACK_TCSUSERDATAS
MutexContainer TcsUserData::countMutexContainer;
int TcsUserData::numTcsUserDatas = 0;
#endif
TcsUserData::TcsUserData()
{

#if SILVERLINING_TRACK_TCSUSERDATAS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    id = numTcsUserDatas;
    ++numTcsUserDatas;
    std::cout << "num of TcsUserDatas: " << numTcsUserDatas << std::endl;
#endif
}
TcsUserData::~TcsUserData()
{
#if SILVERLINING_TRACK_TCSUSERDATAS
    ScopedMutex scopedMutex(countMutexContainer.mutex);
    --numTcsUserDatas;
    std::cout << "num of TcsUserDatas: " << numTcsUserDatas << std::endl;
#endif
}
}