#include "PCH.h"
#include "UI/Rml/RmlInput.h"

#if USING( ME_RMLUI )

#include <SDL_scancode.h>

namespace RmlInput
{
    Rml::Input::KeyIdentifier ToKeyIdentifier( uint32_t InScancode )
    {
        using namespace Rml::Input;
        switch( InScancode )
        {
        case SDL_SCANCODE_A: return KI_A;
        case SDL_SCANCODE_B: return KI_B;
        case SDL_SCANCODE_C: return KI_C;
        case SDL_SCANCODE_D: return KI_D;
        case SDL_SCANCODE_E: return KI_E;
        case SDL_SCANCODE_F: return KI_F;
        case SDL_SCANCODE_G: return KI_G;
        case SDL_SCANCODE_H: return KI_H;
        case SDL_SCANCODE_I: return KI_I;
        case SDL_SCANCODE_J: return KI_J;
        case SDL_SCANCODE_K: return KI_K;
        case SDL_SCANCODE_L: return KI_L;
        case SDL_SCANCODE_M: return KI_M;
        case SDL_SCANCODE_N: return KI_N;
        case SDL_SCANCODE_O: return KI_O;
        case SDL_SCANCODE_P: return KI_P;
        case SDL_SCANCODE_Q: return KI_Q;
        case SDL_SCANCODE_R: return KI_R;
        case SDL_SCANCODE_S: return KI_S;
        case SDL_SCANCODE_T: return KI_T;
        case SDL_SCANCODE_U: return KI_U;
        case SDL_SCANCODE_V: return KI_V;
        case SDL_SCANCODE_W: return KI_W;
        case SDL_SCANCODE_X: return KI_X;
        case SDL_SCANCODE_Y: return KI_Y;
        case SDL_SCANCODE_Z: return KI_Z;
        case SDL_SCANCODE_0: return KI_0;
        case SDL_SCANCODE_1: return KI_1;
        case SDL_SCANCODE_2: return KI_2;
        case SDL_SCANCODE_3: return KI_3;
        case SDL_SCANCODE_4: return KI_4;
        case SDL_SCANCODE_5: return KI_5;
        case SDL_SCANCODE_6: return KI_6;
        case SDL_SCANCODE_7: return KI_7;
        case SDL_SCANCODE_8: return KI_8;
        case SDL_SCANCODE_9: return KI_9;
        case SDL_SCANCODE_RETURN: return KI_RETURN;
        case SDL_SCANCODE_ESCAPE: return KI_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return KI_BACK;
        case SDL_SCANCODE_TAB: return KI_TAB;
        case SDL_SCANCODE_SPACE: return KI_SPACE;
        case SDL_SCANCODE_DELETE: return KI_DELETE;
        case SDL_SCANCODE_HOME: return KI_HOME;
        case SDL_SCANCODE_END: return KI_END;
        case SDL_SCANCODE_PAGEUP: return KI_PRIOR;
        case SDL_SCANCODE_PAGEDOWN: return KI_NEXT;
        case SDL_SCANCODE_LEFT: return KI_LEFT;
        case SDL_SCANCODE_RIGHT: return KI_RIGHT;
        case SDL_SCANCODE_UP: return KI_UP;
        case SDL_SCANCODE_DOWN: return KI_DOWN;
        case SDL_SCANCODE_MINUS: return KI_OEM_MINUS;
        case SDL_SCANCODE_EQUALS: return KI_OEM_PLUS;
        case SDL_SCANCODE_COMMA: return KI_OEM_COMMA;
        case SDL_SCANCODE_PERIOD: return KI_OEM_PERIOD;
        case SDL_SCANCODE_SLASH: return KI_OEM_2;
        case SDL_SCANCODE_SEMICOLON: return KI_OEM_1;
        case SDL_SCANCODE_APOSTROPHE: return KI_OEM_7;
        case SDL_SCANCODE_LEFTBRACKET: return KI_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET: return KI_OEM_6;
        case SDL_SCANCODE_BACKSLASH: return KI_OEM_5;
        case SDL_SCANCODE_GRAVE: return KI_OEM_3;
        case SDL_SCANCODE_LSHIFT: return KI_LSHIFT;
        case SDL_SCANCODE_RSHIFT: return KI_RSHIFT;
        case SDL_SCANCODE_LCTRL: return KI_LCONTROL;
        case SDL_SCANCODE_RCTRL: return KI_RCONTROL;
        case SDL_SCANCODE_LALT: return KI_LMENU;
        case SDL_SCANCODE_RALT: return KI_RMENU;
        default: return KI_UNKNOWN;
        }
    }


    char ToCharacter( uint32_t InScancode, bool InShift )
    {
        if( InScancode >= SDL_SCANCODE_A && InScancode <= SDL_SCANCODE_Z )
        {
            const char base = static_cast<char>( 'a' + ( InScancode - SDL_SCANCODE_A ) );
            return InShift ? static_cast<char>( base - 'a' + 'A' ) : base;
        }
        if( InScancode >= SDL_SCANCODE_1 && InScancode <= SDL_SCANCODE_9 )
        {
            static const char* shifted = "!@#$%^&*(";
            return InShift ? shifted[InScancode - SDL_SCANCODE_1] : static_cast<char>( '1' + ( InScancode - SDL_SCANCODE_1 ) );
        }
        switch( InScancode )
        {
        case SDL_SCANCODE_0: return InShift ? ')' : '0';
        case SDL_SCANCODE_SPACE: return ' ';
        case SDL_SCANCODE_MINUS: return InShift ? '_' : '-';
        case SDL_SCANCODE_EQUALS: return InShift ? '+' : '=';
        case SDL_SCANCODE_COMMA: return InShift ? '<' : ',';
        case SDL_SCANCODE_PERIOD: return InShift ? '>' : '.';
        case SDL_SCANCODE_SLASH: return InShift ? '?' : '/';
        case SDL_SCANCODE_SEMICOLON: return InShift ? ':' : ';';
        case SDL_SCANCODE_APOSTROPHE: return InShift ? '"' : '\'';
        case SDL_SCANCODE_LEFTBRACKET: return InShift ? '{' : '[';
        case SDL_SCANCODE_RIGHTBRACKET: return InShift ? '}' : ']';
        case SDL_SCANCODE_BACKSLASH: return InShift ? '|' : '\\';
        case SDL_SCANCODE_GRAVE: return InShift ? '~' : '`';
        default: return 0;
        }
    }
}

#endif
