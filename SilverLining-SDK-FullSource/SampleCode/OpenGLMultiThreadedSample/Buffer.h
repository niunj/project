#pragma once
#include "GL/glew.h"
namespace Sample
{
    class Buffer
    {
    public:
        Buffer(int sizeInBytes, int type, const void* data);
        virtual ~Buffer();
        unsigned int BufferId(void) const;
    public:
        unsigned int bufferId = 0;
    };
}