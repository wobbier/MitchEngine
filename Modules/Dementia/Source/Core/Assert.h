#pragma once
#include "Dementia.h"

#if USING( ME_RETAIL )
#define ME_ASSERT(expr)
#define ME_ASSERT_MSG(expr, msg)
#else

#if USING( ME_PLATFORM_WINDOWS )

bool CustomAssertFunction( const char* expression, const char* message, const char* file, int line );
#define ME_ASSERT_MSG(expr, msg) \
if (!(expr) && CustomAssertFunction(#expr, msg, __FILE__, __LINE__)) { \
__debugbreak();\
}

#define ME_ASSERT(expr) \
if (!(expr) && CustomAssertFunction(#expr, nullptr, __FILE__, __LINE__)) { \
__debugbreak();\
}

#else

#define ME_ASSERT(expr)
#define ME_ASSERT_MSG(expr, msg)

#endif

#endif