#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include <string>
class TerrainFileAutomation
{
public:
    static bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError );
};
#endif
