// Copyright (c) 2004-2020  Sundog Software, LLC All rights reserved.


#include "Renderable.h"
#include "Mutex.h"

namespace SilverLining
{
Renderable::Renderable(bool lockable) : depthOffset(0), instanced(false), mutex(0)
{
    if (lockable) {
        mutex = SL_NEW Mutex();
    }
}

void Renderable::AddListenerIfNecessary(RenderableListener* listener)
{
    if (mutex) {
        mutex->Lock();
    }
    if (listeners.find(listener) == listeners.end()) {
        listeners.insert(listener);
    }
    if (mutex) {
        mutex->Unlock();
    }
}

Renderable::~Renderable()
{
    for (SL_SET(RenderableListener*)::const_iterator it = listeners.begin(); it != listeners.end(); ++it) {
        RenderableListener* listener = *it;
        listener->renderableDeleted(this);
    }
    if (mutex) {
        SL_DELETE mutex;
    }
}

void Renderable::RemoveListenener(RenderableListener* listener)
{
    if (listeners.find(listener) != listeners.end()) {
        listeners.erase(listener);
    }
}
}