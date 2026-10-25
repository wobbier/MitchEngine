#include "PCH.h"
#include "UIModel.bindings.h"

#if USING( ME_SCRIPTING )

#include "CLog.h"
#include "Engine/Engine.h"
#include "Cores/UI/RmlUiCore.h"

#include <cstring>

// Script view models for RmlUi documents (UI-3). Values live on RmlUiCore, so they survive C#
// hot reloads; without ME_RMLUI every call is a quiet no-op so scripts stay portable.

namespace
{
    RmlUiCore* UiCore()
    {
        return GetEngine().RmlUi;
    }


    const char* Text( const uint8_t* InText )
    {
        return reinterpret_cast<const char*>( InText );
    }


    int CopyOut( const std::string& InValue, uint8_t* OutValue, int InSize )
    {
        if( OutValue == nullptr || InSize <= 0 )
        {
            return static_cast<int>( InValue.size() );
        }
        const int length = std::min( static_cast<int>( InValue.size() ), InSize - 1 );
        std::memcpy( OutValue, InValue.data(), static_cast<size_t>( length ) );
        OutValue[length] = 0;
        return length;
    }
}


static bool Eng_UIModel_Ensure( const uint8_t* inModel )
{
#if USING( ME_RMLUI )
    if( RmlUiCore* core = UiCore() )
    {
        core->ModelEnsure( Text( inModel ) );
        return true;
    }
#endif
    return false;
}


static void Eng_UIModel_SetNumber( const uint8_t* inModel, const uint8_t* inKey, double inValue )
{
#if USING( ME_RMLUI )
    if( RmlUiCore* core = UiCore() )
    {
        core->ModelSetNumber( Text( inModel ), Text( inKey ), inValue );
    }
#endif
}


static void Eng_UIModel_SetString( const uint8_t* inModel, const uint8_t* inKey, const uint8_t* inValue )
{
#if USING( ME_RMLUI )
    if( RmlUiCore* core = UiCore() )
    {
        core->ModelSetString( Text( inModel ), Text( inKey ), Text( inValue ) );
    }
#endif
}


static void Eng_UIModel_RegisterEvent( const uint8_t* inModel, const uint8_t* inKey )
{
#if USING( ME_RMLUI )
    if( RmlUiCore* core = UiCore() )
    {
        core->ModelRegisterEvent( Text( inModel ), Text( inKey ) );
    }
#endif
}


static double Eng_UIModel_GetNumber( const uint8_t* inModel, const uint8_t* inKey )
{
#if USING( ME_RMLUI )
    if( RmlUiCore* core = UiCore() )
    {
        return core->ModelGetNumber( Text( inModel ), Text( inKey ) );
    }
#endif
    return 0.0;
}


static int Eng_UIModel_GetString( const uint8_t* inModel, const uint8_t* inKey, uint8_t* outValue, int inSize )
{
#if USING( ME_RMLUI )
    if( RmlUiCore* core = UiCore() )
    {
        return CopyOut( core->ModelGetString( Text( inModel ), Text( inKey ) ), outValue, inSize );
    }
#endif
    return CopyOut( {}, outValue, inSize );
}


static int Eng_UIModel_PollEvent( const uint8_t* inModel, uint8_t* outName, int inSize )
{
#if USING( ME_RMLUI )
    if( RmlUiCore* core = UiCore() )
    {
        return CopyOut( core->ModelPollEvent( Text( inModel ) ), outName, inSize );
    }
#endif
    return CopyOut( {}, outName, inSize );
}


// BasicUIView left with Ultralight (UI-5). Its ABI slot stays (order is the ABI), so a script
// built against the old API gets a one-time pointer to the replacement instead of a crash.
static void Eng_BasicUIView_ExecuteJS( EntityID, const uint8_t* )
{
    static bool s_warned = false;
    if( !s_warned )
    {
        s_warned = true;
        YIKES( "[UI] BasicUIView.ExecuteJS was removed with Ultralight: use a UIDocument and UI.Model" );
    }
}


void Register_UIModelBindings( ScriptEngineAPI& inAPI )
{
    inAPI.BasicUIView_ExecuteJS = Eng_BasicUIView_ExecuteJS;
    inAPI.UIModel_Ensure        = Eng_UIModel_Ensure;
    inAPI.UIModel_SetNumber     = Eng_UIModel_SetNumber;
    inAPI.UIModel_SetString     = Eng_UIModel_SetString;
    inAPI.UIModel_RegisterEvent = Eng_UIModel_RegisterEvent;
    inAPI.UIModel_GetNumber     = Eng_UIModel_GetNumber;
    inAPI.UIModel_GetString     = Eng_UIModel_GetString;
    inAPI.UIModel_PollEvent     = Eng_UIModel_PollEvent;
}

#endif
