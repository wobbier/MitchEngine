#include "PCH.h"
#include "ScriptHost.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <windows.h>
#endif

#if USING( ME_SCRIPTING )
#include <string>

#include <nethost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>

// hostfxr uses strings on windows... everything else in the engine isn't that, fuck windows
static std::wstring ToWide( const std::string& inStr )
{
    return std::wstring( inStr.begin(), inStr.end() );
}


bool ScriptHost::Init( const std::string& inRuntimeConfigPath )
{
    wchar_t buffer[MAX_PATH];
    size_t bufferSize = sizeof( buffer ) / sizeof( wchar_t );
    int rc = get_hostfxr_path( buffer, &bufferSize, nullptr );
    if( rc != 0 )
    {
        YIKES_NEW( "get_hostfxr_path failed: 0x{:x}", rc );
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
    rc = initFptr( wConfig.c_str(), nullptr, reinterpret_cast<hostfxr_handle*>( &m_hostfxrCtx ) );
    if( rc != 0 || !m_hostfxrCtx )
    {
        YIKES_NEW( "hostfxr_initialize_for_runtime_config failed: 0x{:x} - runtimeconfig path correct?", rc );
        return false;
    }

    load_assembly_and_get_function_pointer_fn loadFn = nullptr;
    rc = getDelegateFptr(
        reinterpret_cast<hostfxr_handle>( m_hostfxrCtx ),
        hdt_load_assembly_and_get_function_pointer,
        reinterpret_cast<void**>( &loadFn ) );
    if( rc != 0 || !loadFn )
    {
        YIKES_NEW( "failed to get load_assembly delegate: 0x{:x}", rc );
        return false;
    }
    m_loadAssemblyFn = (void*)loadFn;

    DBG( "ScriptHost .NET runtime initialised" );
    return true;
}


void ScriptHost::Shutdown()
{
    if( m_hostfxrCtx && m_closeFptr )
    {
        m_closeFptr( m_hostfxrCtx );
        m_hostfxrCtx = nullptr;
    }
    if( m_hostfxrLib )
    {
        ::FreeLibrary( (HMODULE)m_hostfxrLib );
        m_hostfxrLib = nullptr;
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

    // wide because windows
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

    if( rc != 0 )
    {
        YIKES_NEW( "LoadFunction failed for {}.{} (0x{:x})", inTypeName, inMethodName, rc );
        return false;
    }
    return true;
}

#endif