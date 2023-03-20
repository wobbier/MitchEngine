#pragma once

#define ME_DISABLE_DEFAULT_CONSTRUCTOR(Class)	\
Class() = delete;

#define ME_DISABLE_COPY_CONSTRUCTOR(Class)		\
Class(const Class&) = delete;

#define ME_DISABLE_COPY_ASSIGNMENT(Class)		\
Class& operator=(const Class&) = delete;

#define ME_DISABLE_MOVE_CONSTRUCTOR(Class)		\
Class(Class&&) = delete;

#define ME_DISABLE_MOVE_ASSIGNMENT(Class)		\
Class& operator=(Class&&) = delete;

#define ME_NONCOPYABLE(Class) ME_DISABLE_COPY_CONSTRUCTOR(Class); ME_DISABLE_COPY_ASSIGNMENT(Class);

#define ME_NONMOVABLE(Class) ME_DISABLE_MOVE_CONSTRUCTOR(Class); ME_DISABLE_MOVE_ASSIGNMENT(Class);

#ifndef IN_USE
#define IN_USE &&
#endif /* IN USE */

#ifndef NOT_IN_USE
#define NOT_IN_USE &&!
#endif /* NOT_IN_USE */

#ifndef USE_IF
#define USE_IF(x) &&((x) ? 1: 0)&&
#endif /* USE_IF */

#ifndef USING
#define USING(x) (1 x 1)
#endif /* USING */

#ifdef ME_PLATFORM_WIN64
#define ME_PLATFORM_WIN64 IN_USE
#else
#define ME_PLATFORM_WIN64 NOT_IN_USE
#endif

#ifdef ME_PLATFORM_UWP
#define ME_PLATFORM_UWP IN_USE
#else
#define ME_PLATFORM_UWP NOT_IN_USE
#endif

#ifdef ME_PLATFORM_MACOS
#define ME_PLATFORM_MACOS IN_USE
#else
#define ME_PLATFORM_MACOS NOT_IN_USE
#endif

#ifdef ME_EDITOR
#define ME_EDITOR IN_USE
#else
#define ME_EDITOR NOT_IN_USE
#endif

#ifdef ME_TOOLS
#define ME_TOOLS 1
#define USE_ME_TOOLS IN_USE
#else
#define ME_TOOLS 0
#define USE_ME_TOOLS NOT_IN_USE
#endif

#define forever for(;;)
//#define ME_PLATFORM_WINDOWS USING
#define UME_TOOLS USE_IF( USE_ME_TOOLS )
#define ME_PLATFORM_WINDOWS USE_IF( USING( ME_PLATFORM_WIN64 ) || USING( ME_PLATFORM_UWP ) )
