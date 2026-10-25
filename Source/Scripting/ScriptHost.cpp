#include "PCH.h"
#include "ScriptHost.h"

#if USING( ME_SCRIPTING )
#include <filesystem>
#include <string>

#include <nethost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>

#if USING( ME_PLATFORM_WINDOWS )
#include <windows.h>

// hostfxr uses wide strings on Windows
static std::wstring ToWide( const std::string& inStr )
{
    return std::wstring( inStr.begin(), inStr.end() );
}

#elif USING( ME_PLATFORM_LINUX )
#include <dlfcn.h>
#include <limits.h>
#endif


bool ScriptHost::Init( const std::string& inRuntimeConfigPath, const std::string& inDotnetRoot )
{
#if USING( ME_PLATFORM_WINDOWS )
    // An app-local install: hostfxr comes from it, and the runtime resolves its frameworks there.
    const std::wstring wDotnetRoot = ToWide( inDotnetRoot );
    const get_hostfxr_parameters fxrParams{ sizeof( get_hostfxr_parameters ), nullptr, wDotnetRoot.c_str() };
    const hostfxr_initialize_parameters initParams{ sizeof( hostfxr_initialize_parameters ), nullptr, wDotnetRoot.c_str() };
    wchar_t buffer[MAX_PATH];
    size_t bufferSize = sizeof( buffer ) / sizeof( wchar_t );
    int rc = get_hostfxr_path( buffer, &bufferSize, inDotnetRoot.empty() ? nullptr : &fxrParams );
    if( rc != 0 )
    {
        YIKES_NEW( "get_hostfxr_path failed: 0x{:x} - no .NET 8 runtime found (install one, set DOTNET_ROOT, or ship one with Build Game)", static_cast<uint32_t>( rc ) );
        return false;
    }

    m_hostfxrLib = ( void* )::LoadLibraryW( buffer );
    if( !m_hostfxrLib )
    {
        YIKES( "LoadLibraryW(hostfxr) failed" );
        return false;
    }

    auto initFptr = ( hostfxr_initialize_for_runtime_config_fn )
        ::GetProcAddress( (HMODULE)m_hostfxrLib, "hostfxr_initialize_for_runtime_config" );
    auto getDelegateFptr = ( hostfxr_get_runtime_delegate_fn )
        ::GetProcAddress( (HMODULE)m_hostfxrLib, "hostfxr_get_runtime_delegate" );
    m_closeFptr = ( void( * )( void* ) )
        ::GetProcAddress( (HMODULE)m_hostfxrLib, "hostfxr_close" );

    if( !initFptr || !getDelegateFptr || !m_closeFptr )
    {
        YIKES( "failed to get hostfxr exports - is .NET installed?" );
        return false;
    }

    auto wConfig = ToWide( inRuntimeConfigPath );
    rc = initFptr( wConfig.c_str(), inDotnetRoot.empty() ? nullptr : &initParams, reinterpret_cast<hostfxr_handle*>( &m_hostfxrCtx ) );
    if( rc != 0 || !m_hostfxrCtx )
    {
        YIKES_NEW( "hostfxr_initialize_for_runtime_config failed: 0x{:x} - runtimeconfig path correct?", static_cast<uint32_t>( rc ) );
        return false;
    }

    load_assembly_and_get_function_pointer_fn loadFn = nullptr;
    rc = getDelegateFptr(
        reinterpret_cast<hostfxr_handle>( m_hostfxrCtx ),
        hdt_load_assembly_and_get_function_pointer,
        reinterpret_cast<void**>( &loadFn ) );
    if( rc != 0 || !loadFn )
    {
        YIKES_NEW( "failed to get load_assembly delegate: 0x{:x}", static_cast<uint32_t>( rc ) );
        return false;
    }
    m_loadAssemblyFn = (void*)loadFn;

#elif USING( ME_PLATFORM_LINUX )
    // An app-local install: hostfxr comes from it, and the runtime resolves its frameworks there.
    const get_hostfxr_parameters fxrParams{ sizeof( get_hostfxr_parameters ), nullptr, inDotnetRoot.c_str() };
    const hostfxr_initialize_parameters initParams{ sizeof( hostfxr_initialize_parameters ), nullptr, inDotnetRoot.c_str() };
    char_t buffer[PATH_MAX];
    size_t bufferSize = sizeof( buffer ) / sizeof( char_t );
    int rc = get_hostfxr_path( buffer, &bufferSize, inDotnetRoot.empty() ? nullptr : &fxrParams );
    if( rc != 0 )
    {
        YIKES_NEW( "get_hostfxr_path failed: 0x{:x} - no .NET 8 runtime found (install one, set DOTNET_ROOT, or ship one with Build Game)", static_cast<uint32_t>( rc ) );
        return false;
    }

    m_hostfxrLib = dlopen( buffer, RTLD_LAZY | RTLD_LOCAL );
    if( !m_hostfxrLib )
    {
        YIKES( "dlopen(hostfxr) failed" );
        return false;
    }

    auto initFptr = ( hostfxr_initialize_for_runtime_config_fn )
        dlsym( m_hostfxrLib, "hostfxr_initialize_for_runtime_config" );
    auto getDelegateFptr = ( hostfxr_get_runtime_delegate_fn )
        dlsym( m_hostfxrLib, "hostfxr_get_runtime_delegate" );
    m_closeFptr = ( void( * )( void* ) )
        dlsym( m_hostfxrLib, "hostfxr_close" );

    if( !initFptr || !getDelegateFptr || !m_closeFptr )
    {
        YIKES( "failed to get hostfxr exports - is .NET installed?" );
        return false;
    }

    rc = initFptr( inRuntimeConfigPath.c_str(), inDotnetRoot.empty() ? nullptr : &initParams, reinterpret_cast<hostfxr_handle*>( &m_hostfxrCtx ) );
    if( rc != 0 || !m_hostfxrCtx )
    {
        YIKES_NEW( "hostfxr_initialize_for_runtime_config failed: 0x{:x} - runtimeconfig path correct?", static_cast<uint32_t>( rc ) );
        return false;
    }

    load_assembly_and_get_function_pointer_fn loadFn = nullptr;
    rc = getDelegateFptr(
        reinterpret_cast<hostfxr_handle>( m_hostfxrCtx ),
        hdt_load_assembly_and_get_function_pointer,
        reinterpret_cast<void**>( &loadFn ) );
    if( rc != 0 || !loadFn )
    {
        YIKES_NEW( "failed to get load_assembly delegate: 0x{:x}", static_cast<uint32_t>( rc ) );
        return false;
    }
    m_loadAssemblyFn = (void*)loadFn;
#endif

#if USING( ME_PLATFORM_WINDOWS ) || USING( ME_PLATFORM_LINUX )
    // Which runtime a build runs on is the first question when scripts don't start on a player's
    // machine, so it's logged at Info (shipped builds log Info and up).
    const std::filesystem::path hostfxrPath( buffer );
    CLog::Log( CLog::LogType::Info, std::string( "Scripts: .NET runtime from " ) + ( inDotnetRoot.empty() ? "the system install (" : "the app-local install (" ) + hostfxrPath.generic_string() + ")" );
#endif
    return true;
}


std::string ScriptHost::FindSystemDotnetRoot()
{
#if USING( ME_PLATFORM_WINDOWS )
    wchar_t buffer[MAX_PATH];
    size_t bufferSize = sizeof( buffer ) / sizeof( wchar_t );
#elif USING( ME_PLATFORM_LINUX )
    char_t buffer[PATH_MAX];
    size_t bufferSize = sizeof( buffer ) / sizeof( char_t );
#endif
#if USING( ME_PLATFORM_WINDOWS ) || USING( ME_PLATFORM_LINUX )
    if( get_hostfxr_path( buffer, &bufferSize, nullptr ) != 0 )
    {
        return std::string();
    }
    // <root>/host/fxr/<version>/<hostfxr library>
    return std::filesystem::path( buffer ).parent_path().parent_path().parent_path().parent_path().generic_string();
#else
    return std::string();
#endif
}


void ScriptHost::Shutdown()
{
    // Called from engine shutdown, while the runtime is alive. The hostfxr library stays loaded:
    // CoreCLR can't be unloaded from a process, and unloading the host under it crashes at exit.
    if( m_hostfxrCtx && m_closeFptr )
    {
        m_closeFptr( m_hostfxrCtx );
        m_hostfxrCtx = nullptr;
    }
    m_loadAssemblyFn = nullptr;
}


bool ScriptHost::LoadFunction( const std::string& inAssemblyPath,
    const std::string& inTypeName,
    const std::string& inMethodName,
    void** outFnPtr )
{
    if( !m_loadAssemblyFn )
    {
        YIKES( "ScriptHost not initialised" );
        return false;
    }

    auto fn = reinterpret_cast<load_assembly_and_get_function_pointer_fn>( m_loadAssemblyFn );

#if USING( ME_PLATFORM_WINDOWS )
    auto wAssembly = ToWide( inAssemblyPath );
    auto wType = ToWide( inTypeName );
    auto wMethod = ToWide( inMethodName );

    int rc = fn(
        wAssembly.c_str(),
        wType.c_str(),
        wMethod.c_str(),
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        outFnPtr );
#elif USING( ME_PLATFORM_LINUX )
    int rc = fn(
        inAssemblyPath.c_str(),
        inTypeName.c_str(),
        inMethodName.c_str(),
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        outFnPtr );
#endif

    if( rc != 0 )
    {
        YIKES_NEW( "LoadFunction failed for {}.{} (0x{:x})", inTypeName, inMethodName, static_cast<uint32_t>( rc ) );
        return false;
    }
    return true;
}

#endif