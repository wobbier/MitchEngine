#include "PCH.h"
#include "UIUtils.h"

#if USING( ME_ULTRALIGHT )

#include "Ultralight\KeyCodes.h"

bool UIUtils::ConvertToULChar( KeyCode inKeyCode, int& outULKeyCode )
{
    switch( inKeyCode )
    {
    case KeyCode::A:
        outULKeyCode = ultralight::KeyCodes::GK_A;
        break;
    case KeyCode::B:
        outULKeyCode = ultralight::KeyCodes::GK_B;
        break;
    case KeyCode::C:
        outULKeyCode = ultralight::KeyCodes::GK_C;
        break;
    case KeyCode::D:
        outULKeyCode = ultralight::KeyCodes::GK_D;
        break;
    case KeyCode::E:
        outULKeyCode = ultralight::KeyCodes::GK_E;
        break;
    case KeyCode::F:
        outULKeyCode = ultralight::KeyCodes::GK_F;
        break;
    case KeyCode::G:
        outULKeyCode = ultralight::KeyCodes::GK_G;
        break;
    case KeyCode::H:
        outULKeyCode = ultralight::KeyCodes::GK_H;
        break;
    case KeyCode::I:
        outULKeyCode = ultralight::KeyCodes::GK_I;
        break;
    case KeyCode::J:
        outULKeyCode = ultralight::KeyCodes::GK_J;
        break;
    case KeyCode::K:
        outULKeyCode = ultralight::KeyCodes::GK_K;
        break;
    case KeyCode::L:
        outULKeyCode = ultralight::KeyCodes::GK_L;
        break;
    case KeyCode::M:
        outULKeyCode = ultralight::KeyCodes::GK_M;
        break;
    case KeyCode::N:
        outULKeyCode = ultralight::KeyCodes::GK_N;
        break;
    case KeyCode::O:
        outULKeyCode = ultralight::KeyCodes::GK_O;
        break;
    case KeyCode::P:
        outULKeyCode = ultralight::KeyCodes::GK_P;
        break;
    case KeyCode::Q:
        outULKeyCode = ultralight::KeyCodes::GK_Q;
        break;
    case KeyCode::R:
        outULKeyCode = ultralight::KeyCodes::GK_R;
        break;
    case KeyCode::S:
        outULKeyCode = ultralight::KeyCodes::GK_S;
        break;
    case KeyCode::T:
        outULKeyCode = ultralight::KeyCodes::GK_T;
        break;
    case KeyCode::U:
        outULKeyCode = ultralight::KeyCodes::GK_U;
        break;
    case KeyCode::V:
        outULKeyCode = ultralight::KeyCodes::GK_V;
        break;
    case KeyCode::W:
        outULKeyCode = ultralight::KeyCodes::GK_W;
        break;
    case KeyCode::X:
        outULKeyCode = ultralight::KeyCodes::GK_X;
        break;
    case KeyCode::Y:
        outULKeyCode = ultralight::KeyCodes::GK_Y;
        break;
    case KeyCode::Z:
        outULKeyCode = ultralight::KeyCodes::GK_Z;
        break;
    case KeyCode::Num1:
        outULKeyCode = ultralight::KeyCodes::GK_1;
        break;
    case KeyCode::Num2:
        outULKeyCode = ultralight::KeyCodes::GK_2;
        break;
    case KeyCode::Num3:
        outULKeyCode = ultralight::KeyCodes::GK_3;
        break;
    case KeyCode::Num4:
        outULKeyCode = ultralight::KeyCodes::GK_4;
        break;
    case KeyCode::Num5:
        outULKeyCode = ultralight::KeyCodes::GK_5;
        break;
    case KeyCode::Num6:
        outULKeyCode = ultralight::KeyCodes::GK_6;
        break;
    case KeyCode::Num7:
        outULKeyCode = ultralight::KeyCodes::GK_7;
        break;
    case KeyCode::Num8:
        outULKeyCode = ultralight::KeyCodes::GK_8;
        break;
    case KeyCode::Num9:
        outULKeyCode = ultralight::KeyCodes::GK_9;
        break;
    case KeyCode::Num0:
        outULKeyCode = ultralight::KeyCodes::GK_0;
        break;
    case KeyCode::Space:
        outULKeyCode = ultralight::KeyCodes::GK_SPACE; // Broken
        break;
    case KeyCode::Minus:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_MINUS; //??
        break;
    case KeyCode::Equals:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_PLUS;
        break;
    case KeyCode::LeftBracket:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_4;
        break;
    case KeyCode::RightBracket:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_6;
        break;
    case KeyCode::Backslash:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_5;
        break;
    case KeyCode::NonUSHash: // ??
        break;
    case KeyCode::SemiColon:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_1;
        break;
    case KeyCode::Apostrophe:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_7;
        break;
    case KeyCode::Tilde:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_3;
        break;
    case KeyCode::SDL_SCANCODE_COMMA:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_COMMA;
        break;
    case KeyCode::SDL_SCANCODE_PERIOD:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_PERIOD;
        break;
    case KeyCode::SDL_SCANCODE_SLASH:
        outULKeyCode = ultralight::KeyCodes::GK_OEM_2;
        break;
    case KeyCode::SDL_SCANCODE_KP_DIVIDE:
        break;
    case KeyCode::SDL_SCANCODE_KP_MULTIPLY:
        break;
    case KeyCode::SDL_SCANCODE_KP_MINUS:
        break;
    case KeyCode::SDL_SCANCODE_KP_PLUS:
        break;
    case KeyCode::SDL_SCANCODE_KP_ENTER:
        break;
    case KeyCode::SDL_SCANCODE_KP_1:
        break;
    case KeyCode::SDL_SCANCODE_KP_2:
        break;
    case KeyCode::SDL_SCANCODE_KP_3:
        break;
    case KeyCode::SDL_SCANCODE_KP_4:
        break;
    case KeyCode::SDL_SCANCODE_KP_5:
        break;
    case KeyCode::SDL_SCANCODE_KP_6:
        break;
    case KeyCode::SDL_SCANCODE_KP_7:
        break;
    case KeyCode::SDL_SCANCODE_KP_8:
        break;
    case KeyCode::SDL_SCANCODE_KP_9:
        break;
    case KeyCode::SDL_SCANCODE_KP_0:
        break;
    case KeyCode::SDL_SCANCODE_KP_PERIOD:
        break;
    case KeyCode::SDL_SCANCODE_NONUSBACKSLASH:
        break;
    case KeyCode::SDL_SCANCODE_KP_EQUALS:
        break;
    case KeyCode::SDL_SCANCODE_KP_COMMA:
        break;
    case KeyCode::SDL_SCANCODE_KP_EQUALSAS400:
        break;
    case KeyCode::SDL_SCANCODE_SEPARATOR:
        break;
    case KeyCode::SDL_SCANCODE_KP_00:
        break;
    case KeyCode::SDL_SCANCODE_KP_000:
        break;
    case KeyCode::SDL_SCANCODE_THOUSANDSSEPARATOR:
        break;
    case KeyCode::SDL_SCANCODE_DECIMALSEPARATOR:
        break;
    case KeyCode::SDL_SCANCODE_CURRENCYUNIT:
        break;
    case KeyCode::SDL_SCANCODE_CURRENCYSUBUNIT:
        break;
    case KeyCode::SDL_SCANCODE_KP_LEFTBRACE:
        break;
    case KeyCode::SDL_SCANCODE_KP_RIGHTBRACE:
        break;
    case KeyCode::SDL_SCANCODE_KP_TAB: // ??
        break;
    case KeyCode::SDL_SCANCODE_KP_A:
        break;
    case KeyCode::SDL_SCANCODE_KP_B:
        break;
    case KeyCode::SDL_SCANCODE_KP_C:
        break;
    case KeyCode::SDL_SCANCODE_KP_D:
        break;
    case KeyCode::SDL_SCANCODE_KP_E:
        break;
    case KeyCode::SDL_SCANCODE_KP_F:
        break;
    case KeyCode::SDL_SCANCODE_KP_XOR:
        break;
    case KeyCode::SDL_SCANCODE_KP_POWER:
        break;
    case KeyCode::SDL_SCANCODE_KP_PERCENT:
        break;
    case KeyCode::SDL_SCANCODE_KP_LESS:
        break;
    case KeyCode::SDL_SCANCODE_KP_GREATER:
        break;
    case KeyCode::SDL_SCANCODE_KP_AMPERSAND:
        break;
    case KeyCode::SDL_SCANCODE_KP_DBLAMPERSAND:
        break;
    case KeyCode::SDL_SCANCODE_KP_VERTICALBAR:
        break;
    case KeyCode::SDL_SCANCODE_KP_DBLVERTICALBAR:
        break;
    case KeyCode::SDL_SCANCODE_KP_COLON:
        break;
    case KeyCode::SDL_SCANCODE_KP_HASH:
        break;
    case KeyCode::SDL_SCANCODE_KP_SPACE:
        break;
    case KeyCode::SDL_SCANCODE_KP_AT:
        break;
    case KeyCode::SDL_SCANCODE_KP_EXCLAM:
        break;
    case KeyCode::SDL_NUM_SCANCODES:
    case KeyCode::Unknown:
    default:
        return false;
    }
    return true;
}

bool UIUtils::ConvertToUL( KeyCode inKeyCode, int& outULKeyCode )
{
    if( ConvertToULChar( inKeyCode, outULKeyCode ) )
    {
        return true;
    }

    switch( inKeyCode )
    {
    case KeyCode::Unknown:
        break;
    case KeyCode::Enter:
        outULKeyCode = ultralight::KeyCodes::GK_RETURN;
        break;
    case KeyCode::Escape:
        outULKeyCode = ultralight::KeyCodes::GK_ESCAPE;
        break;
    case KeyCode::Backspace:
        outULKeyCode = ultralight::KeyCodes::GK_BACK;
        break;
    case KeyCode::Tab: // ??
        outULKeyCode = ultralight::KeyCodes::GK_TAB;
        break;
    case KeyCode::SDL_SCANCODE_CAPSLOCK:
        outULKeyCode = ultralight::KeyCodes::GK_CAPITAL;
        break;
    case KeyCode::F1:
        outULKeyCode = ultralight::KeyCodes::GK_F1;
        break;
    case KeyCode::F2:
        outULKeyCode = ultralight::KeyCodes::GK_F2;
        break;
    case KeyCode::F3:
        outULKeyCode = ultralight::KeyCodes::GK_F3;
        break;
    case KeyCode::F4:
        outULKeyCode = ultralight::KeyCodes::GK_F4;
        break;
    case KeyCode::F5:
        outULKeyCode = ultralight::KeyCodes::GK_F5;
        break;
    case KeyCode::F6:
        outULKeyCode = ultralight::KeyCodes::GK_F6;
        break;
    case KeyCode::F7:
        outULKeyCode = ultralight::KeyCodes::GK_F7;
        break;
    case KeyCode::F8:
        outULKeyCode = ultralight::KeyCodes::GK_F8;
        break;
    case KeyCode::F9:
        outULKeyCode = ultralight::KeyCodes::GK_F9;
        break;
    case KeyCode::F10:
        outULKeyCode = ultralight::KeyCodes::GK_F10;
        break;
    case KeyCode::F11:
        outULKeyCode = ultralight::KeyCodes::GK_F11;
        break;
    case KeyCode::F12:
        outULKeyCode = ultralight::KeyCodes::GK_F12;
        break;
    case KeyCode::SDL_SCANCODE_PRINTSCREEN:
        break;
    case KeyCode::SDL_SCANCODE_SCROLLLOCK:
        break;
    case KeyCode::SDL_SCANCODE_PAUSE:
        break;
    case KeyCode::SDL_SCANCODE_INSERT:
        break;
    case KeyCode::SDL_SCANCODE_HOME:
        outULKeyCode = ultralight::KeyCodes::GK_HOME;
        break;
    case KeyCode::SDL_SCANCODE_PAGEUP:
        outULKeyCode = ultralight::KeyCodes::GK_PRIOR;
        break;
    case KeyCode::Delete:
        outULKeyCode = ultralight::KeyCodes::GK_DELETE;
        break;
    case KeyCode::SDL_SCANCODE_END:
        outULKeyCode = ultralight::KeyCodes::GK_END;
        break;
    case KeyCode::SDL_SCANCODE_PAGEDOWN:
        outULKeyCode = ultralight::KeyCodes::GK_NEXT;
        break;
    case KeyCode::SDL_SCANCODE_RIGHT:
        outULKeyCode = ultralight::KeyCodes::GK_RIGHT;
        break;
    case KeyCode::SDL_SCANCODE_LEFT:
        outULKeyCode = ultralight::KeyCodes::GK_LEFT;
        break;
    case KeyCode::SDL_SCANCODE_DOWN:
        outULKeyCode = ultralight::KeyCodes::GK_DOWN;
        break;
    case KeyCode::SDL_SCANCODE_UP:
        outULKeyCode = ultralight::KeyCodes::GK_UP;
        break;
    case KeyCode::SDL_SCANCODE_NUMLOCKCLEAR:
        break;
    case KeyCode::SDL_SCANCODE_KP_ENTER:
        break;
    case KeyCode::SDL_SCANCODE_APPLICATION:
        break;
    case KeyCode::SDL_SCANCODE_POWER:
        break;
    case KeyCode::SDL_SCANCODE_F13:
        break;
    case KeyCode::SDL_SCANCODE_F14:
        break;
    case KeyCode::SDL_SCANCODE_F15:
        break;
    case KeyCode::SDL_SCANCODE_F16:
        break;
    case KeyCode::SDL_SCANCODE_F17:
        break;
    case KeyCode::SDL_SCANCODE_F18:
        break;
    case KeyCode::SDL_SCANCODE_F19:
        break;
    case KeyCode::SDL_SCANCODE_F20:
        break;
    case KeyCode::SDL_SCANCODE_F21:
        break;
    case KeyCode::SDL_SCANCODE_F22:
        break;
    case KeyCode::SDL_SCANCODE_F23:
        break;
    case KeyCode::SDL_SCANCODE_F24:
        break;
    case KeyCode::SDL_SCANCODE_EXECUTE:
        break;
    case KeyCode::SDL_SCANCODE_HELP:
        break;
    case KeyCode::SDL_SCANCODE_MENU:
        break;
    case KeyCode::SDL_SCANCODE_SELECT:
        break;
    case KeyCode::SDL_SCANCODE_STOP:
        break;
    case KeyCode::SDL_SCANCODE_AGAIN:
        break;
    case KeyCode::SDL_SCANCODE_UNDO:
        break;
    case KeyCode::SDL_SCANCODE_CUT:
        break;
    case KeyCode::SDL_SCANCODE_COPY:
        break;
    case KeyCode::SDL_SCANCODE_PASTE:
        break;
    case KeyCode::SDL_SCANCODE_FIND:
        break;
    case KeyCode::SDL_SCANCODE_MUTE:
        break;
    case KeyCode::SDL_SCANCODE_VOLUMEUP:
        break;
    case KeyCode::SDL_SCANCODE_VOLUMEDOWN:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL1:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL2:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL3:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL4:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL5:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL6:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL7:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL8:
        break;
    case KeyCode::SDL_SCANCODE_INTERNATIONAL9:
        break;
    case KeyCode::SDL_SCANCODE_LANG1:
        break;
    case KeyCode::SDL_SCANCODE_LANG2:
        break;
    case KeyCode::SDL_SCANCODE_LANG3:
        break;
    case KeyCode::SDL_SCANCODE_LANG4:
        break;
    case KeyCode::SDL_SCANCODE_LANG5:
        break;
    case KeyCode::SDL_SCANCODE_LANG6:
        break;
    case KeyCode::SDL_SCANCODE_LANG7:
        break;
    case KeyCode::SDL_SCANCODE_LANG8:
        break;
    case KeyCode::SDL_SCANCODE_LANG9:
        break;
    case KeyCode::SDL_SCANCODE_ALTERASE:
        break;
    case KeyCode::SDL_SCANCODE_SYSREQ:
        break;
    case KeyCode::SDL_SCANCODE_CANCEL:
        break;
    case KeyCode::SDL_SCANCODE_CLEAR:
        break;
    case KeyCode::SDL_SCANCODE_PRIOR:
        break;
    case KeyCode::SDL_SCANCODE_RETURN2:
        break;
    case KeyCode::SDL_SCANCODE_SEPARATOR:
        break;
    case KeyCode::SDL_SCANCODE_OUT:
        break;
    case KeyCode::SDL_SCANCODE_OPER:
        break;
    case KeyCode::SDL_SCANCODE_CLEARAGAIN:
        break;
    case KeyCode::SDL_SCANCODE_CRSEL:
        break;
    case KeyCode::SDL_SCANCODE_EXSEL:
        break;
    case KeyCode::SDL_SCANCODE_KP_00:
        break;
    case KeyCode::SDL_SCANCODE_KP_000:
        break;
    case KeyCode::SDL_SCANCODE_THOUSANDSSEPARATOR:
        break;
    case KeyCode::SDL_SCANCODE_DECIMALSEPARATOR:
        break;
    case KeyCode::SDL_SCANCODE_CURRENCYUNIT:
        break;
    case KeyCode::SDL_SCANCODE_CURRENCYSUBUNIT:
        break;
    case KeyCode::SDL_SCANCODE_KP_LEFTPAREN:
        break;
    case KeyCode::SDL_SCANCODE_KP_RIGHTPAREN:
        break;
    case KeyCode::SDL_SCANCODE_KP_LEFTBRACE:
        break;
    case KeyCode::SDL_SCANCODE_KP_RIGHTBRACE:
        break;
    case KeyCode::SDL_SCANCODE_KP_TAB:
        break;
    case KeyCode::SDL_SCANCODE_KP_BACKSPACE:
        break;
    case KeyCode::SDL_SCANCODE_KP_A:
        break;
    case KeyCode::SDL_SCANCODE_KP_B:
        break;
    case KeyCode::SDL_SCANCODE_KP_C:
        break;
    case KeyCode::SDL_SCANCODE_KP_D:
        break;
    case KeyCode::SDL_SCANCODE_KP_E:
        break;
    case KeyCode::SDL_SCANCODE_KP_F:
        break;
    case KeyCode::SDL_SCANCODE_KP_XOR:
        break;
    case KeyCode::SDL_SCANCODE_KP_POWER:
        break;
    case KeyCode::SDL_SCANCODE_KP_PERCENT:
        break;
    case KeyCode::SDL_SCANCODE_KP_LESS:
        break;
    case KeyCode::SDL_SCANCODE_KP_GREATER:
        break;
    case KeyCode::SDL_SCANCODE_KP_AMPERSAND:
        break;
    case KeyCode::SDL_SCANCODE_KP_DBLAMPERSAND:
        break;
    case KeyCode::SDL_SCANCODE_KP_VERTICALBAR:
        break;
    case KeyCode::SDL_SCANCODE_KP_DBLVERTICALBAR:
        break;
    case KeyCode::SDL_SCANCODE_KP_COLON:
        break;
    case KeyCode::SDL_SCANCODE_KP_HASH:
        break;
    case KeyCode::SDL_SCANCODE_KP_SPACE:
        break;
    case KeyCode::SDL_SCANCODE_KP_AT:
        break;
    case KeyCode::SDL_SCANCODE_KP_EXCLAM:
        break;
    case KeyCode::SDL_SCANCODE_KP_MEMSTORE:
        break;
    case KeyCode::SDL_SCANCODE_KP_MEMRECALL:
        break;
    case KeyCode::SDL_SCANCODE_KP_MEMCLEAR:
        break;
    case KeyCode::SDL_SCANCODE_KP_MEMADD:
        break;
    case KeyCode::SDL_SCANCODE_KP_MEMSUBTRACT:
        break;
    case KeyCode::SDL_SCANCODE_KP_MEMMULTIPLY:
        break;
    case KeyCode::SDL_SCANCODE_KP_MEMDIVIDE:
        break;
    case KeyCode::SDL_SCANCODE_KP_PLUSMINUS:
        break;
    case KeyCode::SDL_SCANCODE_KP_CLEAR:
        break;
    case KeyCode::SDL_SCANCODE_KP_CLEARENTRY:
        break;
    case KeyCode::SDL_SCANCODE_KP_BINARY:
        break;
    case KeyCode::SDL_SCANCODE_KP_OCTAL:
        break;
    case KeyCode::SDL_SCANCODE_KP_DECIMAL:
        break;
    case KeyCode::SDL_SCANCODE_KP_HEXADECIMAL:
        break;
    case KeyCode::LeftControl:
        break;
    case KeyCode::LeftShift:
        break;
    case KeyCode::LeftAlt:
        break;
    case KeyCode::SDL_SCANCODE_LGUI:
        break;
    case KeyCode::RightControl:
        break;
    case KeyCode::SDL_SCANCODE_RSHIFT:
        break;
    case KeyCode::RightAlt:
        break;
    case KeyCode::SDL_SCANCODE_RGUI:
        break;
    case KeyCode::SDL_SCANCODE_MODE:
        break;
    case KeyCode::SDL_SCANCODE_AUDIONEXT:
        break;
    case KeyCode::SDL_SCANCODE_AUDIOPREV:
        break;
    case KeyCode::SDL_SCANCODE_AUDIOSTOP:
        break;
    case KeyCode::SDL_SCANCODE_AUDIOPLAY:
        break;
    case KeyCode::SDL_SCANCODE_AUDIOMUTE:
        break;
    case KeyCode::SDL_SCANCODE_MEDIASELECT:
        break;
    case KeyCode::SDL_SCANCODE_WWW:
        break;
    case KeyCode::SDL_SCANCODE_MAIL:
        break;
    case KeyCode::SDL_SCANCODE_CALCULATOR:
        break;
    case KeyCode::SDL_SCANCODE_COMPUTER:
        break;
    case KeyCode::SDL_SCANCODE_AC_SEARCH:
        break;
    case KeyCode::SDL_SCANCODE_AC_HOME:
        break;
    case KeyCode::SDL_SCANCODE_AC_BACK:
        break;
    case KeyCode::SDL_SCANCODE_AC_FORWARD:
        break;
    case KeyCode::SDL_SCANCODE_AC_STOP:
        break;
    case KeyCode::SDL_SCANCODE_AC_REFRESH:
        break;
    case KeyCode::SDL_SCANCODE_AC_BOOKMARKS:
        break;
    case KeyCode::SDL_SCANCODE_BRIGHTNESSDOWN:
        break;
    case KeyCode::SDL_SCANCODE_BRIGHTNESSUP:
        break;
    case KeyCode::SDL_SCANCODE_DISPLAYSWITCH:
        break;
    case KeyCode::SDL_SCANCODE_KBDILLUMTOGGLE:
        break;
    case KeyCode::SDL_SCANCODE_KBDILLUMDOWN:
        break;
    case KeyCode::SDL_SCANCODE_KBDILLUMUP:
        break;
    case KeyCode::SDL_SCANCODE_EJECT:
        break;
    case KeyCode::SDL_SCANCODE_SLEEP:
        break;
    case KeyCode::SDL_SCANCODE_APP1:
        break;
    case KeyCode::SDL_SCANCODE_APP2:
        break;
    case KeyCode::SDL_SCANCODE_AUDIOREWIND:
        break;
    case KeyCode::SDL_SCANCODE_AUDIOFASTFORWARD:
        break;
    case KeyCode::SDL_NUM_SCANCODES:
        break;
    default:
        return false;
    }
    return false;
}

#endif