#include "PCH.h"
#include "Localization.bindings.h"

#if USING( ME_SCRIPTING )

#include "Engine/Localization.h"

#include <cstring>

// Localization for scripts (PLAY-6): C# Loc wraps these. The language itself is a player
// setting (Settings.Language), so changing it goes through PlayerSettings.

namespace
{
    // The full length, so a caller whose buffer was too small can retry with a bigger one.
    int CopyOut( const std::string& InValue, uint8_t* OutValue, int InSize )
    {
        if( OutValue != nullptr && InSize > 0 )
        {
            const int length = std::min( static_cast<int>( InValue.size() ), InSize - 1 );
            std::memcpy( OutValue, InValue.data(), static_cast<size_t>( length ) );
            OutValue[length] = 0;
        }
        return static_cast<int>( InValue.size() );
    }
}


static int Eng_Loc_Get( const uint8_t* inKey, uint8_t* outText, int inSize )
{
    return CopyOut( Localization::Get().Get( inKey ? reinterpret_cast<const char*>( inKey ) : "" ), outText, inSize );
}


static int Eng_Loc_GetLanguage( uint8_t* outCode, int inSize )
{
    return CopyOut( Localization::Get().GetLanguage(), outCode, inSize );
}


// Newline-separated codes.
static int Eng_Loc_GetLanguages( uint8_t* outCodes, int inSize )
{
    std::string codes;
    for( const std::string& code : Localization::Get().GetLanguages() )
    {
        codes += ( codes.empty() ? "" : "\n" ) + code;
    }
    return CopyOut( codes, outCodes, inSize );
}


void Register_LocalizationBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Loc_Get          = Eng_Loc_Get;
    inAPI.Loc_GetLanguage  = Eng_Loc_GetLanguage;
    inAPI.Loc_GetLanguages = Eng_Loc_GetLanguages;
}

#endif
