#pragma once

#define forever for(;;)

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

#define ME_HARDSTUCK(Class) ME_NONCOPYABLE(Class); ME_NONMOVABLE(Class);

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

#if defined( _MSC_VER )
#define ME_COMPILER_MSVC IN_USE
#else
#define ME_COMPILER_MSVC NOT_IN_USE
#endif

#if defined( __clang__ )
#define ME_COMPILER_CLANG IN_USE
#else
#define ME_COMPILER_CLANG NOT_IN_USE
#endif

#if defined( __GNUC__ )
#define ME_COMPILER_GCC IN_USE
#else
#define ME_COMPILER_GCC NOT_IN_USE
#endif

#if USING ( ME_COMPILER_MSVC )
#define DISABLE_OPTIMIZATION __pragma( optimize( "", off ) )
#define ENABLE_OPTIMIZATION __pragma( optimize( "", on ) )
#elif USING ( ME_COMPILER_GCC )
#define DISABLE_OPTIMIZATION \
        _Pragma( "GCC push_options" ) \
        _Pragma( "GCC optimize (\"O0\")" )
#define ENABLE_OPTIMIZATION _Pragma( "GCC pop_options" )
#elif USING ( ME_COMPILER_CLANG )
#define DISABLE_OPTIMIZATION _Pragma( "clang optimize off" )
#define ENABLE_OPTIMIZATION _Pragma( "clang optimize on" )
#else
#error Unknown Compiler
#endif

#ifdef ME_HEADLESS
#define ME_HEADLESS IN_USE
#else
#define ME_HEADLESS NOT_IN_USE
#endif

#if defined( USE_OPTICK )
#define ME_OPTICK IN_USE;
#else
#define ME_OPTICK NOT_IN_USE;
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

#if defined( DEFINE_ME_FMOD )
#define ME_FMOD USE_IF( USING ( IN_USE ) && !USING( ME_HEADLESS ) )
#else
#define ME_FMOD USE_IF( USING ( NOT_IN_USE ) && !USING( ME_HEADLESS ) )
#endif

#ifdef _DEBUG
#define ME_DEBUG IN_USE
#else
#define ME_DEBUG NOT_IN_USE
#endif

#if defined( DEFINE_ME_RELEASE )
#define ME_RELEASE IN_USE
#else
#define ME_RELEASE NOT_IN_USE
#endif

#if defined( DEFINE_ME_RETAIL )
#define ME_RETAIL IN_USE
#else
#define ME_RETAIL NOT_IN_USE
#endif

#if defined( DEFINE_ME_MONO )
#define ME_MONO IN_USE
#else
#define ME_MONO NOT_IN_USE
#endif

#if defined( DEFINE_ME_RENDERDOC )
#define ME_ENABLE_RENDERDOC IN_USE
#else
#define ME_ENABLE_RENDERDOC NOT_IN_USE
#endif

#define ME_PLATFORM_WINDOWS USE_IF( USING( ME_PLATFORM_WIN64 ) || USING( ME_PLATFORM_UWP ) )
#define ME_EDITOR_WIN64     USE_IF( USING( ME_EDITOR ) && USING( ME_PLATFORM_WIN64 ) )
#define ME_EDITOR_MACOS     USE_IF( USING( ME_EDITOR ) && USING( ME_PLATFORM_MACOS ) )
#define ME_SCRIPTING        USE_IF( USING( ME_MONO ) )
#define ME_PROFILING        USE_IF( USING( ME_DEBUG ) || USING( ME_RELEASE ) )
// I'm currently using some ImGui stuff in debug for profiling, TOOLS needs it even that it's currently bundled with the editor.
#define ME_IMGUI            USE_IF( USING( ME_EDITOR ) || USING( ME_TOOLS ) || USING( ME_PROFILING ) )
#define ME_BASIC_PROFILER   USE_IF( USING( ME_IMGUI ) && USING( ME_PROFILING ) )