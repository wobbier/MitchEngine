#pragma once

#if USING( ME_ENABLE_RENDERDOC )

#include "renderdoc_app.h"
#include "CLog.h"
#include <Windows.h>
#include <libloaderapi.h>
#include "Core\Assert.h"

class RenderDocManager
{
public:
    RenderDocManager()
    {
        CLog::GetInstance().Log( CLog::LogType::Info, "[RenderDoc] Loading DLL." );
        HMODULE mod = LoadLibrary( L"renderdoc.dll" );
        if( mod )
        {
            CLog::GetInstance().Log( CLog::LogType::Info, "[RenderDoc] Loading." );
            pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress( mod, "RENDERDOC_GetAPI" );
            int ret = RENDERDOC_GetAPI( eRENDERDOC_API_Version_1_4_2, (void**)&RenderDocApi );
            ME_ASSERT_MSG( ret == 1, "The RenderDoc DLL version is \"built different\" from what I've expected." );
            CLog::GetInstance().Log( CLog::LogType::Info, "[RenderDoc] Loaded." );
        }
        else
        {
            CLog::GetInstance().Log( CLog::LogType::Error, "[RenderDoc] Failed to mount RenderDoc dll" );
        }
    }

    ~RenderDocManager()
    {
    }

    void Capture()
    {
        RenderDocApi->TriggerCapture();
    }
private:
    RENDERDOC_API_1_2_0* RenderDocApi = nullptr;
};

#endif
