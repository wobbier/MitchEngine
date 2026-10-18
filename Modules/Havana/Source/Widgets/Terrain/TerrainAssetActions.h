#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include <string>

namespace TerrainAssetActions
{
    bool IsSource( const std::string& InPath );
    bool Move( const std::string& InFrom, const std::string& InTo, std::string& OutError );
    bool Duplicate( const std::string& InFrom, const std::string& InTo, std::string& OutError );
    bool Trash( const std::string& InPath, std::string& OutTrashPath, std::string& OutError );
}
#endif
