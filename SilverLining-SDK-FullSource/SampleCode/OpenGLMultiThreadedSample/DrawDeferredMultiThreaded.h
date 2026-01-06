// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "DrawStrategy.h"

#include <vector>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace Sample
{
    class SampleApp;

    /*

    - DrawDeferredMultiThreaded
    Each view has a corresponding deferred stream.

    Each view is drawn into the deferred stream, in its own thread.

    These threads don't have (or require) any OpenGL context.

    All streams are executed(i.e.the view drawn), in the main thread, which is the only one
    that has the OpenGL context.

    */
    class DrawDeferredMultiThreaded : public DrawStrategy
    {
    public:
        //! constructor
        DrawDeferredMultiThreaded(SampleApp& _sampleApp);

        //! destructor
        virtual ~DrawDeferredMultiThreaded();

    public:
        //! draw using this strategy
        virtual void Draw(double millis, int frame);

    private:
        //! render a view in a thread
        void RenderView(int view);

        //! signal that all views should start the rendering
        void SignalViewsToStartRendering(void);

        //! wait for all views to finish rendering
        void WaitForAllViewsToFinishRendering(void);

        //! indicate that we have finished rendering
        bool allFinished(void) const;
        
    private:
        //! the threads for each view
        std::vector<std::thread> myThreadForView;

        //! whether we are rendering into a view right now
        std::vector<bool> myBool_Begin_RenderView;

        //! whether we are done rendering into a view right now
        std::vector<bool> myBool_End_RenderView;

        //! internal synchronization variables for coordinating
        // the multiple threads
        std::condition_variable myConditionVariable;
        std::mutex myMutex;

        //! the frame being rendered
        std::atomic<long int> myNumRenders;

        //! whether we should quit the application
        bool quit;

        friend class AllFinished;
    };
}