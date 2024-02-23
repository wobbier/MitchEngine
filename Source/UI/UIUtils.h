#pragma once
#include <Engine/Input.h>

namespace UIUtils
{
#if USING( ME_ULTRALIGHT )
    bool ConvertToULChar( KeyCode inKeyCode, int& outULKeyCode );
    bool ConvertToUL( KeyCode inKeyCode, int& outULKeyCode );
#endif
}