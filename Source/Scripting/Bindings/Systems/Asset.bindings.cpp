#include "PCH.h"
#include "Asset.bindings.h"

#if USING( ME_SCRIPTING )

#include "VFS/VFS.h"

#include <cstring>

// Project files for scripts (CS-7 data assets), through the VFS so paks and cooked trees serve them.

static int Eng_Asset_ReadText( const uint8_t* inPath, uint8_t* outText, int inSize )
{
    std::string text;
    if( inPath == nullptr || !VFS::Get().ReadText( reinterpret_cast<const char*>( inPath ), text ) )
    {
        return -1;
    }
    if( outText != nullptr && inSize > 0 )
    {
        const int length = std::min( static_cast<int>( text.size() ), inSize - 1 );
        std::memcpy( outText, text.data(), static_cast<size_t>( length ) );
        outText[length] = 0;
    }
    return static_cast<int>( text.size() );
}


static bool Eng_Asset_WriteText( const uint8_t* inPath, const uint8_t* inText )
{
    return inPath != nullptr && inText != nullptr && VFS::Get().WriteText( std::string( reinterpret_cast<const char*>( inPath ) ), std::string( reinterpret_cast<const char*>( inText ) ) );
}


void Register_AssetBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Asset_ReadText = Eng_Asset_ReadText;
    inAPI.Asset_WriteText = Eng_Asset_WriteText;
}

#endif
