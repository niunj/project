// Copyright (c) 2012 Sundog Software LLC. All rights reserved worldwide.

#ifndef SILVERLINING_MUTEX_H
#define SILVERLINING_MUTEX_H

#if (defined(WIN32) || defined(WIN64))
#include <Windows.h>
#define WINDOWS
#else
#include <pthread.h>
#endif

namespace SilverLining
{
    class Mutex
    {
#ifdef WINDOWS
    public:
        Mutex()
        {
            mutex = NULL;
            mutex = CreateMutex(NULL, FALSE, NULL);
        }

        ~Mutex()
        {
            if (mutex) CloseHandle(mutex);
        }

        void Lock()
        {
            if (mutex) WaitForSingleObject(mutex, INFINITE);
        }

        void Unlock()
        {
            if (mutex) ReleaseMutex(mutex);
        }

    private:
        HANDLE mutex;

#else
    public:
        Mutex()
        {
            hasMutex = pthread_mutex_init(&mutex, NULL) == 0;
        }

        ~Mutex()
        {
            if (hasMutex) pthread_mutex_destroy(&mutex);
        }

        void Lock()
        {
            if (hasMutex) pthread_mutex_lock(&mutex);
        }

        void Unlock()
        {
            if (hasMutex) pthread_mutex_unlock(&mutex);
        }

    private:
        pthread_mutex_t mutex;
	bool hasMutex;
#endif
    };

    class ScopedMutex
    {
    public:
        ScopedMutex(Mutex* _mutex)
            : mutex(_mutex)
        {
            if (mutex)
            {
                mutex->Lock();
            }
        }
        ~ScopedMutex()
        {
            if (mutex)
            {
                mutex->Unlock();
            }
        }
    private:
        Mutex* mutex;
    };
}

#endif
