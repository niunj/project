#pragma once

namespace SilverLining
{
    template <class T>
    class ObjectListener
    {
    public:
        virtual void Deleted(const T* object) const = 0;
    };
}
