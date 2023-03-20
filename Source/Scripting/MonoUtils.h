#pragma once
#include "MonoFwd.h"
#include <string>
#include "Path.h"

namespace MonoUtils
{
    enum class ScriptFieldType
    {
        None = 0,
        Float,
        Double,
        Bool,
        Char,
        Byte,
        Short,
        Int,
        Long,
        UByte,
        UShort,
        UInt,
        ULong,
        Vector2,
        Vector3,
        Vector4,
        Entity
    };

    enum Accessibility : uint8_t
    {
        None = 0,
        Private = ( 1 << 0 ),
        Internal = ( 1 << 1 ),
        Protected = ( 1 << 2 ),
        Public = ( 1 << 3 )
    };

    ScriptFieldType MonoTypeToScriptFieldType( MonoType* type );
    std::string ScriptFieldTypeToString( ScriptFieldType type );

    // Gets the accessibility level of the given field
    uint8_t GetFieldAccessibility( MonoClassField* field );

    // Gets the accessibility level of the given property
    uint8_t GetPropertyAccessibility( MonoProperty* property );

    bool CheckMonoError( MonoError& error );

    std::string MonoStringToUTF8( MonoString* monoString );

    MonoAssembly* LoadMonoAssembly( const Path& path, bool loadPDB );
};

