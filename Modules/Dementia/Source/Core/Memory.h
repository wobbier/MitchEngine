#pragma once
#include <Dementia.h>

#if USING( ME_MEMORY_LEAK_TRACKER )
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#define DEBUG_NEW new (_NORMAL_BLOCK, __FILE__, __LINE__)
#define ME_NEW DEBUG_NEW
#else
#define ME_NEW new
#endif

#define ME_DELETE(Object)  \
    do {                   \
        delete (Object);   \
        (Object) = nullptr; \
    } while(0)

#define ME_DELETE_ARRAY(Object)  \
    do {                         \
        delete[] (Object);       \
        (Object) = nullptr;      \
    } while(0)
