#pragma once
#include "MonoFwd.h"
#include <string>

namespace MonoUtils
{
    enum class Accessibility : uint8_t
    {
        None = 0,
        Private = ( 1 << 0 ),
        Internal = ( 1 << 1 ),
        Protected = ( 1 << 2 ),
        Public = ( 1 << 3 )
    };

    // Gets the accessibility level of the given field
    uint8_t GetFieldAccessibility( MonoClassField* field );

    // Gets the accessibility level of the given property
    uint8_t GetPropertyAccessibility( MonoProperty* property );

    bool CheckMonoError( MonoError& error );

    std::string MonoStringToUTF8( MonoString* monoString );
};

