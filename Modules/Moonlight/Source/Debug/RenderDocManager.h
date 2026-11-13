#pragma once

#include <Dementia.h>

#if USING( ME_ENABLE_RENDERDOC )

#include "../../../../ThirdParty/RenderDoc/renderdoc_app.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if USING( ME_PLATFORM_WINDOWS )
#include <Windows.h>
#include <libloaderapi.h>
#else
#include <dlfcn.h>
#endif

// RenderDoc's in-application API (ME-61). Must be constructed before the graphics device is
// created (before bgfx::init) so RenderDoc can hook the API.
//
// Loads whenever RenderDoc is found (pass --no-renderdoc to skip it):
//  - already injected (launched from qrenderdoc "Launch Application"), any platform, or
//  - Windows: renderdoc.dll next to the exe (the build copies it when RenderDoc is installed), or
//  - Linux: librenderdoc.so via ME_RENDERDOC_LIB, the loader path, or the usual system locations.
//
// Captures land in .tmp/Captures/RenderDoc/. If no RenderDoc UI is connected, a finished capture
// opens qrenderdoc on it (--renderdoc-no-ui to skip). --renderdoc-capture-frame N captures frame N.
class RenderDocManager
{
public:
    RenderDocManager()
    {
        void* module = FindLoadedModule();
        if( module )
        {
            CLog::Log( CLog::LogType::Info, "[RenderDoc] Already injected, attaching." );
        }
        else if( ShouldLoad() )
        {
            module = LoadModule();
            if( !module )
            {
                CLog::Log( CLog::LogType::Info, "[RenderDoc] Not found; captures unavailable." );
                return;
            }
        }
        else
        {
            return;
        }

        pRENDERDOC_GetAPI getApi = (pRENDERDOC_GetAPI)GetSymbol( module, "RENDERDOC_GetAPI" );
        if( !getApi || getApi( eRENDERDOC_API_Version_1_4_2, (void**)&RenderDocApi ) != 1 )
        {
            CLog::Log( CLog::LogType::Error, "[RenderDoc] RENDERDOC_GetAPI failed (version mismatch?)." );
            RenderDocApi = nullptr;
            return;
        }
        Configure();
        CLog::Log( CLog::LogType::Info, "[RenderDoc] Loaded. F11 (or Help > RenderDoc) captures a frame." );
    }

    // bgfx's own RenderDoc hook (debug/profile inits) overrides the capture keys and path when the
    // device is created; call this after bgfx::init to put ours back.
    void Configure()
    {
        if( !RenderDocApi )
        {
            return;
        }
        std::error_code ec;
        const std::filesystem::path folder = std::filesystem::absolute( ".tmp/Captures/RenderDoc", ec );
        std::filesystem::create_directories( folder, ec );
        RenderDocApi->SetCaptureFilePathTemplate( ( folder / "capture" ).string().c_str() );
        // The engine drives captures from its own input (F11) so the editor and game agree on focus.
        RenderDocApi->SetCaptureKeys( nullptr, 0 );
        RenderDocApi->SetFocusToggleKeys( nullptr, 0 );
        RenderDocApi->MaskOverlayBits( eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None );
        m_capturesSeen = RenderDocApi->GetNumCaptures();
    }

    bool IsLoaded() const
    {
        return RenderDocApi != nullptr;
    }

    void Capture()
    {
        if( RenderDocApi )
        {
            RenderDocApi->TriggerCapture();
            CLog::Log( CLog::LogType::Info, "[RenderDoc] Capturing next frame..." );
        }
    }

    // Once per frame: reports finished captures and opens the replay UI when none is attached.
    void Update()
    {
        if( !RenderDocApi )
        {
            return;
        }
        // --renderdoc-capture-frame N: an unattended capture of frame N (scripts, headless runs).
        static const int s_captureFrame = CommandLine::GetInt( "--renderdoc-capture-frame", -1 );
        if( m_frame++ == s_captureFrame )
        {
            Capture();
        }
        const uint32_t count = RenderDocApi->GetNumCaptures();
        if( count == m_capturesSeen )
        {
            return;
        }
        std::string lastPath;
        for( uint32_t i = m_capturesSeen; i < count; ++i )
        {
            uint32_t length = 0;
            if( RenderDocApi->GetCapture( i, nullptr, &length, nullptr ) && length > 0 )
            {
                std::vector<char> path( length + 1, '\0' );
                RenderDocApi->GetCapture( i, path.data(), &length, nullptr );
                lastPath = path.data();
                CLog::Log( CLog::LogType::Info, "[RenderDoc] Saved " + lastPath );
            }
        }
        m_capturesSeen = count;

        if( !lastPath.empty() && !RenderDocApi->IsTargetControlConnected() && !CommandLine::Has( "--renderdoc-no-ui" ) )
        {
            RenderDocApi->LaunchReplayUI( 1, lastPath.c_str() );
        }
    }

private:
    static bool ShouldLoad()
    {
        return !CommandLine::Has( "--no-renderdoc" );
    }

#if USING( ME_PLATFORM_WINDOWS )
    static void* FindLoadedModule()
    {
        return (void*)GetModuleHandleA( "renderdoc.dll" );
    }

    static void* LoadModule()
    {
        return (void*)LoadLibraryA( "renderdoc.dll" );
    }

    static void* GetSymbol( void* module, const char* name )
    {
        return (void*)GetProcAddress( (HMODULE)module, name );
    }
#else
    static void* FindLoadedModule()
    {
        return dlopen( "librenderdoc.so", RTLD_NOW | RTLD_NOLOAD );
    }

    static void* LoadModule()
    {
        std::vector<std::string> candidates;
        if( const char* env = std::getenv( "ME_RENDERDOC_LIB" ) )
        {
            candidates.push_back( env );
        }
        candidates.push_back( "librenderdoc.so" );
        candidates.push_back( "/run/current-system/sw/lib/librenderdoc.so" );
        candidates.push_back( "/usr/lib/librenderdoc.so" );
        candidates.push_back( "/usr/lib/x86_64-linux-gnu/librenderdoc.so" );
        for( const std::string& candidate : candidates )
        {
            if( void* module = dlopen( candidate.c_str(), RTLD_NOW | RTLD_GLOBAL ) )
            {
                CLog::Log( CLog::LogType::Info, "[RenderDoc] Loaded " + candidate );
                return module;
            }
        }
        return nullptr;
    }

    static void* GetSymbol( void* module, const char* name )
    {
        return dlsym( module, name );
    }
#endif

    RENDERDOC_API_1_4_2* RenderDocApi = nullptr;
    uint32_t m_capturesSeen = 0;
    int m_frame = 0;
};

#endif
