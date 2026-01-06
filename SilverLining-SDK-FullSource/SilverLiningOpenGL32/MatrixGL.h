// Copyright (c) 2004-2020  Sundog Software, LLC All rights reserved worldwide.

/**
\file Context.h
\brief Classes that declare a Matrix
*/

#pragma once

#include "MemAlloc.h"


namespace SilverLining
{
    class MatrixGL : public MemObject
    {
    public:
        double m[16];

        void setIdentity(void) {
            m[0] = 1;
            m[1] = 0;
            m[2] = 0;
            m[3] = 0;
            m[4] = 0;
            m[5] = 1;
            m[6] = 0;
            m[7] = 0;
            m[8] = 0;
            m[9] = 0;
            m[10] = 1;
            m[11] = 0;
            m[12] = 1;
            m[13] = 0;
            m[14] = 0;
            m[15] = 1;
        }
    };
}