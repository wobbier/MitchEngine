#pragma once
#include "Dementia.h"

#if USING( ME_PLATFORM_WINDOWS )
#define ME_DEBUG_BREAK() __debugbreak()
#elif defined( __clang__ )
#define ME_DEBUG_BREAK() __builtin_debugtrap()
#else
#include <csignal>
#define ME_DEBUG_BREAK() std::raise( SIGTRAP )
#endif

#if USING( ME_RETAIL )
#define ME_ASSERT(expr) ((void)0)
#define ME_ASSERT_MSG(expr, msg) ((void)0)
#else

// Reports a failed assertion (log + call stack, plus a dialog on Win64).
// Returns true when the caller should break into the debugger.
// Setting *ignoreAlways silences this assert site for the rest of the run.
// Run with --assert-fatal to abort on the first failure (tests / CI).
bool CustomAssertFunction( const char* expression, const char* message, const char* file, int line, bool* ignoreAlways );

#define ME_ASSERT_MSG(expr, msg) \
    do { \
        static bool s_meAssertIgnored = false; \
        if( !s_meAssertIgnored && !( expr ) && CustomAssertFunction( #expr, msg, __FILE__, __LINE__, &s_meAssertIgnored ) ) { \
            ME_DEBUG_BREAK(); \
        } \
    } while( 0 )

#define ME_ASSERT(expr) ME_ASSERT_MSG( expr, nullptr )

#endif
