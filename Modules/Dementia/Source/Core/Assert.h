#pragma once
#include "Dementia.h"

#if USING( ME_PLATFORM_WINDOWS ) && !USING( ME_RETAIL )

void CustomAssertFunction( const char* expression, const char* message, const char* file, int line );
#define ME_ASSERT_MSG(expr, msg) (void)((!!(expr)) || (CustomAssertFunction(#expr, msg, __FILE__, __LINE__), 0))

#define ME_ASSERT(expr) (void)((!!(expr)) || (CustomAssertFunction(#expr, nullptr, __FILE__, __LINE__), 0))

#else

#define ME_ASSERT(expr)
#define ME_ASSERT_MSG(expr, msg)

#endif