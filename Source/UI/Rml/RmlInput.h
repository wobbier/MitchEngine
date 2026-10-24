#pragma once
#include "Dementia.h"

#if USING( ME_RMLUI )

#include <RmlUi/Core/Input.h>
#include <cstdint>

// SDL-scancode (the engine's KeyCode values) to RmlUi key mapping, plus the printable-character
// synthesis UI text fields need until real SDL_TEXTINPUT routing lands (UI-4).
namespace RmlInput
{
    Rml::Input::KeyIdentifier ToKeyIdentifier( uint32_t InScancode );

    // The character this scancode types, or 0 when it isn't printable.
    char ToCharacter( uint32_t InScancode, bool InShift );
}

#endif
