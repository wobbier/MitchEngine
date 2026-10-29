#include "PCH.h"
#include "Settings.bindings.h"

#if USING( ME_SCRIPTING )

#include "CLog.h"
#include "Engine/Engine.h"
#include "Engine/HardwareBudget.h"
#include "Engine/PlayerSettings.h"
#include "Input/InputActions.h"

#include <cstring>

// The player's settings for scripts (PLAY-2): C# Settings wraps these with typed properties.

namespace
{
    const char* Text( const uint8_t* InText )
    {
        return InText != nullptr ? reinterpret_cast<const char*>( InText ) : "";
    }


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


static int Eng_Settings_Get( const uint8_t* inKey, uint8_t* outJson, int inSize )
{
    return CopyOut( PlayerSettings::Get().GetValue( Text( inKey ) ).dump(), outJson, inSize );
}


static bool Eng_Settings_Set( const uint8_t* inKey, const uint8_t* inJson )
{
    const json value = json::parse( Text( inJson ), nullptr, false );
    if( value.is_discarded() )
    {
        YIKES( std::string( "[Settings] " ) + Text( inKey ) + ": not JSON: " + Text( inJson ) );
        return false;
    }
    std::string error;
    if( !PlayerSettings::Get().SetValue( Text( inKey ), value, &error ) )
    {
        YIKES( "[Settings] " + error );
        return false;
    }
    return true;
}


static bool Eng_Settings_Save()
{
    return PlayerSettings::Get().Save();
}


static void Eng_Settings_Reset()
{
    PlayerSettings::Get().Reset();
}


static bool Eng_Settings_Rebind( const uint8_t* inAction, int inBinding, const uint8_t* inPart, const uint8_t* inControl )
{
    return inBinding >= 0 && PlayerSettings::Get().Rebind( Text( inAction ), static_cast<size_t>( inBinding ), Text( inPart ), Text( inControl ) );
}


static void Eng_Settings_ResetBindings()
{
    PlayerSettings::Get().ResetBindings();
}


static int Eng_Settings_GetBindingDisplay( const uint8_t* inAction, int inBinding, uint8_t* outText, int inSize )
{
    const std::string display = inBinding >= 0 ? GetEngine().GetInput().GetActions().GetBindingDisplay( Text( inAction ), static_cast<size_t>( inBinding ) ) : std::string();
    return CopyOut( display, outText, inSize );
}


// Newline-separated, in file order.
static int Eng_Settings_GetQualityPresets( uint8_t* outNames, int inSize )
{
    std::string names;
    for( const QualityPreset& preset : HardwareBudget::Get().GetQualityPresets() )
    {
        names += ( names.empty() ? "" : "\n" ) + preset.Name;
    }
    return CopyOut( names, outNames, inSize );
}


void Register_SettingsBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Settings_Get               = Eng_Settings_Get;
    inAPI.Settings_Set               = Eng_Settings_Set;
    inAPI.Settings_Save              = Eng_Settings_Save;
    inAPI.Settings_Reset             = Eng_Settings_Reset;
    inAPI.Settings_Rebind            = Eng_Settings_Rebind;
    inAPI.Settings_ResetBindings     = Eng_Settings_ResetBindings;
    inAPI.Settings_GetBindingDisplay = Eng_Settings_GetBindingDisplay;
    inAPI.Settings_GetQualityPresets = Eng_Settings_GetQualityPresets;
}

#endif
