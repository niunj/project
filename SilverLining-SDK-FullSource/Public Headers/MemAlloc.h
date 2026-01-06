// Copyright (c) 2010-2016 Sundog Software, LLC. All rights reserved worldwide.


/** \file MemAlloc.h
   \brief Memory allocation interface for SilverLining.
 */

#ifndef MEMALLOC_H
#define MEMALLOC_H

#include <cstddef>
#include <vector>

// Customers report too much variance in definitions for unordered_map
// on Linux compilers that leads to crashes when returning it via the API
//#if ((_MSC_VER >= 1600) || defined(LINUX))
#if (_MSC_VER >= 1600)
#define UNORDERED_MAP
#define UNORDERED_SET
#endif

#ifdef UNORDERED_MAP
#include <unordered_map>
#else
#include <map>
#endif

#ifdef UNORDERED_SET
#include <unordered_set>
#else
#include <set>
#endif

#include <string>

#define SL_NEW new
#define SL_DELETE delete
#define SL_MALLOC Allocator::GetAllocator()->alloc
#define SL_FREE(p) Allocator::GetAllocator()->dealloc(p); p = 0;

#ifdef _WIN32
#define SILVERLINING_API __cdecl
#else
#define SILVERLINING_API
#endif

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{
/** You may extend the Allocator class to hook your own memory management scheme into SilverLining.
   Instantiate your own implementation of Allocator, and pass it into Allocator::SetAllocator prior to
   calling any other SilverLining methods or instantiating any SilverLining objects.

   Each object in SilverLining overloads the new and delete operators, and routes memory management through
   the Allocator as well.
 */
class Allocator
{
public:
    Allocator();
    virtual ~Allocator();

    /// Allocate a block of memory; defaults to malloc()
    virtual void * SILVERLINING_API alloc(size_t bytes);

    /// Free a block of memory; defaults to free()
    virtual void SILVERLINING_API dealloc(void *p);

    /// Retrieves the static allocator object
    static Allocator * SILVERLINING_API GetAllocator()
    {
        if( !sAllocator )
            sAllocator = GetDefaultAllocator();

        return sAllocator;
    }

    /// Sets a new static allocator object. If this is not called, the default
    /// implementation using malloc and free is used.
    static void SILVERLINING_API SetAllocator(Allocator *a) 
    {
        sAllocator = a;
    }

protected:
    /// Retrieves default allocator object
    static Allocator * SILVERLINING_API GetDefaultAllocator();

    static Allocator *sAllocator;    

private:
    void *heap;
};

/** This base class for all SilverLining objects intercepts the new and delete operators,
   routing them through SilverLining::Allocator(). */
class MemObject
{
public:
#ifndef ANDROID
#ifndef OPENGLES
    #if (!((defined(WIN32) || defined(WIN64)) && (_MSC_VER < 1300)))

    void *operator new(size_t bytes)
    {
        return SL_MALLOC(bytes);
    }

    void operator delete(void *p)
    {
        SL_FREE(p);
    }
    #endif
#endif
#endif
};
}

// intercepting STL allocation under VC6 is just hopeless.
#if (((defined(WIN32) || defined(WIN64)) && (_MSC_VER < 1300)) || defined(ANDROID) || defined(OPENGLES))

#define SL_VECTOR(T) std::vector< T >
#define SL_MAP(A, B) std::map< A, B >
#define SL_ORDERED_MAP(A, B) std::map< A, B >
#define SL_LIST(T) std::list< T >
#define SL_DEQUE(T) std::deque< T >
#define SL_STRING std::string
#define SL_STACK(T) std::stack< T >
#define SL_SET(A) std::set< A >
#else

// Custom STL allocator

#define MAX_ALLOCATOR_SIZE 100E6

template <class T>
class SilverLiningAllocator {
public:
    // type definitions
    typedef T value_type;
    typedef T*       pointer;
    typedef const T* const_pointer;
    typedef T&       reference;
    typedef const T& const_reference;

    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    // rebind allocator to type U
    template <class U >
    struct rebind {
        typedef SilverLiningAllocator< U > other;
    };

    // return address of values
    pointer SILVERLINING_API address (reference value) const {
        return &value;
    }
    const_pointer SILVERLINING_API address (const_reference value) const {
        return &value;
    }

    /* constructors and destructor
     * - nothing to do because the allocator has no state
     */
    SilverLiningAllocator() throw() {
    }

    SilverLiningAllocator(const SilverLiningAllocator& ) throw() {
    }

    template <class U >
    SilverLiningAllocator (const SilverLiningAllocator< U > &src) throw() {
    }

    ~SilverLiningAllocator() throw() {
    }

    // return maximum number of elements that can be allocated
    size_type SILVERLINING_API max_size () const throw() {
        return (size_type)(MAX_ALLOCATOR_SIZE / sizeof(T));
    }

    // allocate but don't initialize num elements of type T
    pointer SILVERLINING_API allocate (size_type num, const void* = 0) {
        pointer ret = reinterpret_cast<T*>(SilverLining::Allocator::GetAllocator()->alloc(num * sizeof(T)));
        return ret;
    }

    // initialize elements of allocated storage p with value value
    void SILVERLINING_API construct (pointer p, const T& value) {
        // initialize memory with placement new
        ::new ((void*)p)T(value);
    }

    // destroy elements of initialized storage p
    void SILVERLINING_API destroy (pointer p) {
        // destroy objects by calling their destructor
        p->~T();
    }

    // deallocate storage p of deleted elements
    void SILVERLINING_API deallocate (pointer p, size_type) {
        SilverLining::Allocator::GetAllocator()->dealloc(p);
    }
};

// return that all specializations of this allocator are interchangeable
template <class T1, class T2>
bool SILVERLINING_API operator== (const SilverLiningAllocator<T1>&, const SilverLiningAllocator<T2>&) throw() {
    return true;
}
template <class T1, class T2>
bool SILVERLINING_API operator!= (const SilverLiningAllocator<T1>&, const SilverLiningAllocator<T2>&) throw() {
    return false;
}

// Convenience macros for STL object using our allocator

#define SL_VECTOR(T) std::vector<T, SilverLiningAllocator< T > >

#ifdef UNORDERED_MAP
    #define SL_MAP(A, B) std::unordered_map<A, B, std::hash<A>, std::equal_to<A>, SilverLiningAllocator< std::pair<A const, B > > >
#else
    #define SL_MAP(A, B) std::map<A, B, std::less< A >, SilverLiningAllocator< std::pair<A const, B > > >
#endif
#define SL_ORDERED_MAP(A, B) std::map<A, B, std::less< A >, SilverLiningAllocator< std::pair<A const, B > > >


#ifdef UNORDERED_SET
#define SL_SET(A) std::unordered_set<A>
#else
#define SL_SET(A) std::set<A, std::less< A >, SilverLiningAllocator< A > >
#endif

#define SL_LIST(T) std::list<T, SilverLiningAllocator< T > >
#define SL_DEQUE(T) std::deque<T, SilverLiningAllocator< T > >
#define SL_STRING std::basic_string<char, std::char_traits<char>, SilverLiningAllocator<char> >
#define SL_STACK(T) std::stack<T, std::deque< T, SilverLiningAllocator< T > > >

#pragma pack(pop)

#endif

// "Lazy" wrapper for handling STL objects that were static
template <class T>
class SL_LAZY
{
public:
    SL_LAZY() : obj(0) {}
    virtual ~SL_LAZY() { if (obj) SL_DELETE obj; }

    T& Get() {
        if (!obj) obj = SL_NEW T;
        return *obj;
    }

private:
    T* obj;
};

#endif
