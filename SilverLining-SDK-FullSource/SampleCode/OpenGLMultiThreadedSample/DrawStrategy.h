// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

namespace Sample
{
    class SampleApp;

    //! An abstract 'drawing strategy'
    class DrawStrategy
    {
    public:

        //! constructor
        DrawStrategy(SampleApp& sampleApp);

        //! destructor
        virtual ~DrawStrategy();
    public:
        //! abstract draw
        virtual void Draw(double millis, int frame) = 0;
    protected:
        //! back reference to the application
        SampleApp& mySampleApp;
    };
}