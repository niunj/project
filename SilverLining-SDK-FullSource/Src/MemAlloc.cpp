// Copyright (c) 2010-2016 Sundog Software LLC, All rights reserved worldwide.

#include "MemAlloc.h"

#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#else
#include <stdlib.h>
#endif

using namespace SilverLining;

Allocator * Allocator::sAllocator = 0;

Allocator::Allocator()
{
#if defined(WIN32) || defined(WIN64)
    heap = GetProcessHeap();
#endif
}

Allocator::~Allocator()
{

}

// Linker does not guarantee that static objects get created in
// particular order. And some static objects using Allocator may
// infact be created before static Allocator gets created.
// Using static function creating static Default Allocator object
// avoids this problem as Default Allocator contructor will be invoked
// when Allocator is used first time by any allocation request.

Allocator * Allocator::GetDefaultAllocator()
{
    static Allocator sDefaultAllocator;
    return &sDefaultAllocator;
}

void *Allocator::alloc(size_t bytes)
{
#if defined(WIN32) || defined(WIN64)
    return HeapAlloc(heap, 0, bytes);
#else
    return malloc(bytes);
#endif
}

void Allocator::dealloc(void *p)
{
#if defined(WIN32) || defined(WIN64)
    HeapFree(heap, 0, p);
#else
    free(p);
#endif
}
