#pragma once

#include "Dementia.h"
#include "Path.h"

namespace HUB
{
#if USING( ME_PLATFORM_WIN64 )
    Path ShowOpenFilePrompt( void* hwnd = nullptr );
#endif
}