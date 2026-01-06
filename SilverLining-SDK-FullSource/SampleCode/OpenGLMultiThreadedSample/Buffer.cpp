#include "Buffer.h"

namespace Sample
{
Buffer::Buffer(int sizeInBytes, int type, const void* data)
{
    glGenBuffers(1, &bufferId);
    glBindBuffer(type, bufferId);
    glBufferData(type, sizeInBytes, data, GL_STATIC_DRAW);
    glBindBuffer(type, 0);
}

Buffer::~Buffer()
{
    if (bufferId != 0) {
        glDeleteBuffers(1, &bufferId);
    }
}

unsigned int Buffer::BufferId(void) const
{
    return bufferId;
}
}