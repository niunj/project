// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "DrawStrategy.h"

namespace Sample
{
    /*
    - DrawDeferredSingleThreaded :

    Each view has a corresponding deferred stream.

    Each view is drawn into the deferred stream, in a single thread.

    All streams are executed(i.e. the view drawn), in a single thread
    , and hence, in a serialized fashion.

    */
    class DrawDeferredSingleThreaded : public DrawStrategy
    {
    public:
        //! constructor
        DrawDeferredSingleThreaded(SampleApp& sampleApp);

        //! destructor
        virtual ~DrawDeferredSingleThreaded();
    public:
        //! draw using this strategy
        virtual void Draw(double millis, int frame);
    };
}

