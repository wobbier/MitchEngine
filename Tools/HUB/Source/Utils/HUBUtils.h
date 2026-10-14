#pragma once
#include "Dementia.h"
#include "Path.h"

namespace HUB
{
#if USING( ME_PLATFORM_WIN64 ) || USING( ME_PLATFORM_LINUX )
    Path ShowOpenFilePrompt( void* hwnd );
#endif
}
