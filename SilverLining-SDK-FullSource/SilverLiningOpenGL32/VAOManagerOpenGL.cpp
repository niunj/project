// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.
#include "VAOManagerOpenGL.h"
#include "OpenGLUtils.h"

#include "Vertex.h"


// for debugging
#define SL_VAO_DEBUG 0

namespace SilverLining
{
VAOManagerOpenGL* VAOManagerOpenGL::theInstance = 0;

VAOManagerOpenGL* VAOManagerOpenGL::instance(void)
{
    if (theInstance == 0) {
        theInstance = new VAOManagerOpenGL();
    }
    return theInstance;
}

VAOManagerOpenGL::VAOManagerOpenGL()
    : refCount(0)
{
    // nothing else to do
}

void VAOManagerOpenGL::PrintStats(void) const
{
    std::cout << "mapGLContextsToMapVBOsToVAOs.size() = " << mapGLContextsToMapVBOsToVAOs.size() << std::endl;
    for (MapContextsToMapVBOsToVAOs::const_iterator iterContexts = mapGLContextsToMapVBOsToVAOs.begin();
            iterContexts != mapGLContextsToMapVBOsToVAOs.end(); ++iterContexts) {
        std::cout << "context: " << iterContexts->first << std::endl;
        std::cout << "number of vaos: " << iterContexts->second.size() << std::endl;
    }

    std::cout << "mapContextsToVAOQueueToDelete.size() = " << mapContextsToVAOQueueToDelete.size() << std::endl;

    for (MapContextsToVAOQueueToDelete::const_iterator iterContexts = mapContextsToVAOQueueToDelete.begin();
            iterContexts != mapContextsToVAOQueueToDelete.end(); ++iterContexts) {
        std::cout << "context: " << iterContexts->first << std::endl;
        std::cout << "number of vaos: " << iterContexts->second.size() << std::endl;
    }
}

VAOManagerOpenGL::~VAOManagerOpenGL()
{
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
#elif defined(LINUX)
    GLXContext glContext = glXGetCurrentContext();
#elif defined(MAC)
    void *glContext = 0;
#else
#error "Target platform not defined"
#endif

    // delete all from map of VBOs to VAOs for each context, only if we can delete
    for (MapContextsToMapVBOsToVAOs::iterator iterContexts = mapGLContextsToMapVBOsToVAOs.begin();
            iterContexts != mapGLContextsToMapVBOsToVAOs.end(); ++iterContexts) {
        MapVBOsToVAOs& mapVBOsToVAOs = iterContexts->second;
        for (MapVBOsToVAOs::iterator iterVAOs = mapVBOsToVAOs.begin(); iterVAOs != mapVBOsToVAOs.end(); ++iterVAOs) {

            const GLuint vao = iterVAOs->second;

            // if the context matches, delete
            if (glContext == iterContexts->first) {
#if SL_VAO_DEBUG
                std::cout << "SL(~): deleting vao: " << vao << ", context: " << glContext << std::endl;
#endif
                glDeleteVertexArrays(1, &vao);
            }
            // else push on to the queue to delete
            else {
                mapContextsToVAOQueueToDelete[iterContexts->first].push_back(vao);
            }
        }
    }

    // nothing else to do
    // vaos can possibly leak at this point, since
    // some vaos didn't get deleted
#if SL_VAO_DEBUG
    std::cout << "~VAOManagerOpenGL" << std::endl;
    PrintStats();
#endif
}

void VAOManagerOpenGL::DeleteVAOsForBuffer(GLuint buffNum)
{

#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
#elif defined(LINUX)
    GLXContext glContext = glXGetCurrentContext();
#elif defined(MAC)
    void *glContext = 0;
#else
#error "Target platform not defined"
#endif

    // search each context to map of VBOs to VAOs for this buffer
    for (MapContextsToMapVBOsToVAOs::iterator iterContexts = mapGLContextsToMapVBOsToVAOs.begin();
            iterContexts != mapGLContextsToMapVBOsToVAOs.end(); ++iterContexts) {
        MapVBOsToVAOs& mapVBOsToVAOs = iterContexts->second;

        // If not found in this map, continue
        MapVBOsToVAOs::iterator iterVAOs = mapVBOsToVAOs.find(buffNum);
        if (iterVAOs == mapVBOsToVAOs.end()) {
            continue;
        }

        const GLuint vao = iterVAOs->second;

        // If the context matches, delete
        if (glContext == iterContexts->first) {
#if SL_VAO_DEBUG
            std::cout << "SL(deleteVAOsForBuffer): deleting vao: " << vao << ", context: " << glContext << std::endl;
#endif
            glDeleteVertexArrays(1, &vao);
        }
        // else push on to our queue of context to vaos to delete
        else {
            mapContextsToVAOQueueToDelete[iterContexts->first].push_back(vao);
        }

        // erase the entry
        mapVBOsToVAOs.erase(iterVAOs);
    }
}

void VAOManagerOpenGL::ContextBeingDeleted(void* context)
{
#if defined(WIN32) || defined(WIN64)
    HGLRC currentContext = wglGetCurrentContext();
    HGLRC glContext = (HGLRC)context;
#elif defined(LINUX)
    GLXContext currentContext = glXGetCurrentContext();
    GLXContext glContext = (GLXContext)context;
#elif defined(MAC)
    void *currentContext = 0;
    void *glContext = context;
#else
#error "Target platform not defined"
#endif

    MapContextsToMapVBOsToVAOs::iterator iterContexts = mapGLContextsToMapVBOsToVAOs.find(glContext);
    if (iterContexts != mapGLContextsToMapVBOsToVAOs.end()) {
        MapVBOsToVAOs& mapVBOsToVAOs = iterContexts->second;
        for (MapVBOsToVAOs::iterator iterVAOs = mapVBOsToVAOs.begin(); iterVAOs != mapVBOsToVAOs.end(); ++iterVAOs) {
            const GLuint vao = iterVAOs->second;
            // if the context is the same, delete it
            if (currentContext == context) {
                glDeleteVertexArrays(1, &vao);
            }
        }
        // clear the map
        mapVBOsToVAOs.clear();
    }
    // erase the entry for this context
    mapGLContextsToMapVBOsToVAOs.erase(glContext);

    // Delete everything in the static queue for the current context as well
    HeartBeat();
}

void VAOManagerOpenGL::HeartBeat(void)
{
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
#elif defined(__linux__)
    GLXContext glContext = glXGetCurrentContext();
#else
#error "Target platform not defined"
#endif
    // search for the queue for this context
    MapContextsToVAOQueueToDelete::iterator qIter = mapContextsToVAOQueueToDelete.find(glContext);
    // If there is a queue for this context, get it
    if (qIter != mapContextsToVAOQueueToDelete.end()) {
        // delete all vaos for this context
        SL_LIST(GLuint)& vaoQueue = qIter->second;
        for (SL_LIST(GLuint)::iterator vaoIter = vaoQueue.begin(); vaoIter != vaoQueue.end(); ++vaoIter) {
            GLuint vao = *vaoIter;
#if SL_VAO_DEBUG
            std::cout << "SL(HeartBeat): deleting vao: " << vao << ", context: " << glContext << std::endl;
#endif
            glDeleteVertexArrays(1, &vao);
        }
        mapContextsToVAOQueueToDelete.erase(glContext);
    }
}

GLuint VAOManagerOpenGL::GetOrCreateVAOForContextAndBuffer(GLuint buffNum, const VertexAttribLocations& locations)
{
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
#elif defined(LINUX)
    GLXContext glContext = glXGetCurrentContext();
#elif defined(MAC)
    void *glContext = 0;
#else
#error "Target platform not defined"
#endif

    // search for this context
    MapContextsToMapVBOsToVAOs::iterator it = mapGLContextsToMapVBOsToVAOs.find(glContext);

    // If not found, make an entry
    if (it == mapGLContextsToMapVBOsToVAOs.end()) {
        mapGLContextsToMapVBOsToVAOs[glContext];
    }

    it = mapGLContextsToMapVBOsToVAOs.find(glContext);

    // Get the map of VBOs to VAOs for this context
    MapVBOsToVAOs& mapVBOsToVAOs = it->second;

    // If found, return it
    MapVBOsToVAOs::const_iterator iterVAOs = mapVBOsToVAOs.find(buffNum);
    if (iterVAOs != mapVBOsToVAOs.end()) {
        return iterVAOs->second;
    }

    // Else, make one
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

#if SL_VAO_DEBUG
    std::cout << "SL: new vao: " << vao << ", context: " << glContext << std::endl;
#endif

    OpenGLUtils::CheckError(__LINE__);
    mapVBOsToVAOs.insert(std::make_pair(buffNum, vao));

    // Record it
    RecordVAO(vao, buffNum, locations);

    return vao;
}

void VAOManagerOpenGL::RecordVAO(GLuint vao, GLuint buffNum, const VertexAttribLocations& locations)
{
    // start recording
    glBindVertexArray(vao);

    if (buffNum > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, buffNum);
    }

    // PPP: TO DO Locations could potentially be different.
    locations.SetUpVertexAttribPointers();
    locations.EnableVertexAttribArrays();

    // finish recording
    glBindVertexArray(0);

    OpenGLUtils::CheckError(__LINE__);
}

void VAOManagerOpenGL::RecordVAO(GLuint vao, const VertexAttribLocations& locations)
{
    // start recording
    glBindVertexArray(vao);

    glBindVertexBuffer(0, 0, 0, sizeof(Vertex));

    glVertexAttribFormat(0, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, x));
    glVertexAttribFormat(1, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, r));
    glVertexAttribFormat(2, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, u));

    glVertexAttribBinding(0, 0);
    glVertexAttribBinding(1, 0);
    glVertexAttribBinding(2, 0);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glEnableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);

    // finish recording
    glBindVertexArray(0);

    OpenGLUtils::CheckError(__LINE__);
}

GLuint VAOManagerOpenGL::GetOrCreateVAOForContext(const VertexAttribLocations& locations)
{
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
#elif defined(LINUX)
    GLXContext glContext = glXGetCurrentContext();
#elif defined(MAC)
    void *glContext = 0;
#else
#error "Target platform not defined"
#endif

    // search for this context
    MapGlContextsToVaos::iterator it = mapGlContextsToVaos.find(glContext);

    // If not found, make an entry
    if (it == mapGlContextsToVaos.end()) {

        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        mapGlContextsToVaos.insert(std::make_pair(glContext, vao));
        RecordVAO(vao, locations);

        return vao;
    }

    return it->second;
}

void VAOManagerOpenGL::AddRef()
{
    ++refCount;
}

void VAOManagerOpenGL::RemoveRef()
{
    --refCount;
    if (refCount == 0) {
        delete theInstance;
        theInstance = 0;
    }
}
}