#pragma once
#include <windows.h>
#include <assert.h>

#include "renderdoc_app.h"

class RenderDocManager
{
public:
	RenderDocManager()
	{
#if ME_PLATFORM_WIN64
		if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
		{
			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
			int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&RenderDocApi);
			assert(ret == 1);
		}
#endif
	}

	~RenderDocManager()
	{
	}

private:
	RENDERDOC_API_1_1_2* RenderDocApi = nullptr;
};