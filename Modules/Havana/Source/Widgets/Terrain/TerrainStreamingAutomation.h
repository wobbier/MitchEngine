#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include <string>
namespace TerrainStreamingAutomation { bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError ); }
#endif
