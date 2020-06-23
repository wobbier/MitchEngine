#pragma once

#if ME_ENABLE_RENDERDOC

#include <assert.h>
#include "Dementia.h"

#include "renderdoc_app.h"
#include "CLog.h"

class RenderDocManager
{
public:
	RenderDocManager()
	{
		HMODULE mod = GetModuleHandleA("renderdoc.dll");
		if (mod)
		{
			CLog::GetInstance().Log(CLog::LogType::Info, "[RenderDoc] Loading.");
			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
			int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_2_0, (void **)&RenderDocApi);
			assert(ret == 1);
			CLog::GetInstance().Log(CLog::LogType::Info, "[RenderDoc] Loaded.");
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