// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "DrawStrategy.h"

namespace Sample
{
    /*

    -DrawImmediateSingleThreaded:
    All views drawn, immediately, in a single thread.

    */
    class DrawImmediateSingleThreaded : public DrawStrategy
    {
    public:
        //! constructor
        DrawImmediateSingleThreaded(SampleApp& sampleApp);

        //! destructor
        virtual ~DrawImmediateSingleThreaded();
    public:

        //! draw using this strategy
        virtual void Draw(double millis, int frame);
    };
}

