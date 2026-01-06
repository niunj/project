// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.
#include "CubeMapFboManager.h"
#include "SLAssert.h"
#include <iostream>

namespace SilverLining
{
CubeMapFboManager* CubeMapFboManager::theInstance = 0;

CubeMapFboManager::CubeMapFboManager()
    : refCount(0)
{
    // nothing else to do
}

CubeMapFboManager::~CubeMapFboManager()
{
    // Attempt deleting as many as possible
    HeartBeat();
}

CubeMapFboManager* CubeMapFboManager::instance(void)
{
    if (theInstance == 0) {
        theInstance = new CubeMapFboManager();
    }
    return theInstance;
}

CubeMapFboManager* CubeMapFboManager::findInstance(void)
{
    return theInstance;
}

void CubeMapFboManager::HeartBeat(void)
{
    // Also deleted any cube map fbos for the current context
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
#elif defined(__linux__)
    GLXContext glContext = glXGetCurrentContext();
#else
#error "Target platform not defined"
#endif

    MapContextsToCubeMapFBOs::iterator qIter = mapContextsToCubeMapFbosToDelete.find(glContext);
    if (qIter != mapContextsToCubeMapFbosToDelete.end()) {
        SL_SET(GLuint)& fboSet = qIter->second;
        for (SL_SET(GLuint)::iterator fboIter = fboSet.begin(); fboIter != fboSet.end(); ++fboIter) {
            GLuint cubeMapFBO = *fboIter;
            glDeleteFramebuffers(1, &cubeMapFBO);
        }
        mapContextsToCubeMapFbosToDelete.erase(glContext);
    }
}

unsigned int CubeMapFboManager::CreateFbo(void*& contextCreatedIn)
{
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
#elif defined(__linux__)
    GLXContext glContext = glXGetCurrentContext();
#else
#error "Target platform not defined"
#endif
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);

    mapContextsToCubeMapFbos[glContext].insert(fbo);

    contextCreatedIn = glContext;

    return fbo;

}

void CubeMapFboManager::DestroyFbo(unsigned int fbo, void* _contextCreatedIn)
{
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
    HGLRC contextCreatedIn = (HGLRC)(_contextCreatedIn);
#elif defined(__linux__)
    GLXContext glContext = glXGetCurrentContext();
    GLXContext contextCreatedIn = (GLXContext)(_contextCreatedIn);
#else
#error "Target platform not defined"
#endif
    // If not the same context, add to the list for deletetion
    // and return
    if (glContext != contextCreatedIn) {
        SL_SET(GLuint)& setFBOsToDelete = mapContextsToCubeMapFbosToDelete[contextCreatedIn];
        setFBOsToDelete.insert(fbo);
        return;
    }

    // check that this context exists
    MapContextsToCubeMapFBOs::iterator it = mapContextsToCubeMapFbos.find(glContext);
    SL_ASSERT(it != mapContextsToCubeMapFbos.end());
    // If it doesn't, something went wrong!
    if (it == mapContextsToCubeMapFbos.end()) {
        std::cout << "Something went wrong" << std::endl;
        return;
    }

    // Get the set of fbos for this context
    SL_SET(GLuint)& setFBOs = it->second;
    // Check that the fbo being deleted, was infact created and added to
    // the set for this context
    SL_SET(GLuint)::iterator fboIter = setFBOs.find(fbo);
    // If it was not, something went wrong!
    if (fboIter == setFBOs.end()) {
        std::cout << "Something went wrong" << std::endl;
        return;
    }

    // delete the fbo
    glDeleteFramebuffers(1, &fbo);

    // erase the entry
    setFBOs.erase(fboIter);

    if (setFBOs.size() == 0) {
        mapContextsToCubeMapFbos.erase(it);
    }
}

void CubeMapFboManager::AddRef()
{
    ++refCount;
}

void CubeMapFboManager::RemoveRef()
{
    --refCount;
    if (refCount == 0) {
        delete theInstance;
        theInstance = 0;
    }
}
}