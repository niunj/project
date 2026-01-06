// Copyright (c) 2004-2008  Sundog Software, LLC All rights reserved worldwide

#include "Buffer.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
Buffer::Buffer(const char* _name, BufferType _type, int _size, const BufferProperties& _bufferProperties)
    : name(_name), type(_type), size(_size), bufferProperties(_bufferProperties)
{
}

Buffer::~Buffer()
{
}
}
