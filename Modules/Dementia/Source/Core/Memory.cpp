#include "Core/Memory.h"

void* operator new[] (size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    (void)pName;
    (void)flags;
    (void)debugFlags;

    return std::malloc(size);
}

void* operator new[] (size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    (void)pName;
    (void)flags;
    (void)debugFlags;

    return std::malloc(size);
}

void operator delete[] (void* ptr, size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    (void)size;
    (void)pName;
    (void)debugFlags;
    (void)flags;
    (void)debugFlags;
    (void)file;
    (void)line;

    if (ptr)
    {
        std::free(ptr);
    }
}

void operator delete[] (void* ptr, size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    (void)size;
    (void)alignment;
    (void)alignmentOffset;
    (void)pName;
    (void)debugFlags;
    (void)flags;
    (void)debugFlags;
    (void)file;
    (void)line;

    if (ptr)
    {
        std::free(ptr);
    }
}