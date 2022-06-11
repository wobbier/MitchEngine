#pragma once
#include <new>
#include <cstdlib>
#include <stdint.h>

// EASTL

void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line);

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line);

void operator delete[](void* ptr, size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line);

void operator delete[](void* ptr, size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line);