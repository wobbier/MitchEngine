#include "PCH.h"
#include "SaveGame.bindings.h"

#if USING( ME_SCRIPTING )

#include "Engine/SaveGames.h"

#include <cstring>

// Save games for scripts (PLAY-4): C# SaveGame wraps these.

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


static bool Eng_SaveGame_Save( const uint8_t* inSlot, const uint8_t* inName ) noexcept
{
    return SaveGames::Get().Save( Text( inSlot ), Text( inName ) );
}


static bool Eng_SaveGame_Load( const uint8_t* inSlot ) noexcept
{
    return SaveGames::Get().Load( Text( inSlot ) );
}


static bool Eng_SaveGame_Delete( const uint8_t* inSlot ) noexcept
{
    return SaveGames::Get().Delete( Text( inSlot ) );
}


static int Eng_SaveGame_List( uint8_t* outJson, int inSize ) noexcept
{
    try
    {
        json slots = json::array();
        for( const SaveGames::SlotInfo& info : SaveGames::Get().List() )
        {
            slots.push_back( info.ToJson() );
        }
        return CopyOut( slots.dump(), outJson, inSize );
    }
    catch( const std::exception& e )
    {
        YIKES( std::string( "[Scripts] SaveGame_List threw: " ) + e.what() );
        return -1;
    }
}


static bool Eng_SaveGame_IsWriting() noexcept
{
    return SaveGames::Get().IsWriting();
}


static float Eng_SaveGame_GetPlaytime() noexcept
{
    return static_cast<float>( SaveGames::Get().GetPlaytime() );
}


void Register_SaveGameBindings( ScriptEngineAPI& inAPI )
{
    inAPI.SaveGame_Save        = Eng_SaveGame_Save;
    inAPI.SaveGame_Load        = Eng_SaveGame_Load;
    inAPI.SaveGame_Delete      = Eng_SaveGame_Delete;
    inAPI.SaveGame_List        = Eng_SaveGame_List;
    inAPI.SaveGame_IsWriting   = Eng_SaveGame_IsWriting;
    inAPI.SaveGame_GetPlaytime = Eng_SaveGame_GetPlaytime;
}

#endif
