#pragma once

#define forever for(;;)

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

#ifdef ME_HEADLESS
#define ME_HEADLESS IN_USE
#else
#define ME_HEADLESS NOT_IN_USE
#endif

#if defined( DEFINE_ME_PLATFORM_WIN64 )
#define ME_PLATFORM_WIN64 IN_USE
#else
#define ME_PLATFORM_WIN64 NOT_IN_USE
#endif

#if defined( DEFINE_ME_PLATFORM_UWP )
#define ME_PLATFORM_UWP IN_USE
#else
#define ME_PLATFORM_UWP NOT_IN_USE
#endif

#if defined( DEFINE_ME_PLATFORM_MACOS )
#define ME_PLATFORM_MACOS IN_USE
#else
#define ME_PLATFORM_MACOS NOT_IN_USE
#endif

#if defined( DEFINE_ME_EDITOR )
#define ME_EDITOR IN_USE
#else
#define ME_EDITOR NOT_IN_USE
#endif

#if defined( DEFINE_ME_TOOLS )
#define ME_TOOLS USE_IF( USING( ME_EDITOR ) || USING( IN_USE ) )
#else
#define ME_TOOLS USE_IF( USING( ME_EDITOR ) || USING( NOT_IN_USE ) )
#endif

#if defined( FMOD_ENABLED )
#define ME_FMOD USE_IF( USING ( IN_USE ) && !USING( ME_HEADLESS ) )
#else
#define ME_FMOD USE_IF( USING ( NOT_IN_USE ) && !USING( ME_HEADLESS ) )
#endif

#ifdef _DEBUG
#define ME_DEBUG IN_USE
#else
#define ME_DEBUG NOT_IN_USE
#endif

#if defined( ME_ENABLE_RENDERDOC )
#define ME_ENABLE_RENDERDOC IN_USE
#else
#define ME_ENABLE_RENDERDOC NOT_IN_USE
#endif

#define ME_PLATFORM_WINDOWS USE_IF( USING( ME_PLATFORM_WIN64 ) || USING( ME_PLATFORM_UWP ) )
#define ME_EDITOR_WIN64     USE_IF( USING( ME_EDITOR ) && USING( ME_PLATFORM_WIN64 ) )
#define ME_EDITOR_MACOS     USE_IF( USING( ME_EDITOR ) && USING( ME_PLATFORM_MACOS ) )
#define ME_SCRIPTING        IN_USE
#define ME_PROFILING        USE_IF( USING( ME_DEBUG ) )