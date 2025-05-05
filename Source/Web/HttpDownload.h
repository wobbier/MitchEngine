#pragma once
#include "Path.h"

namespace Web
{
    bool DownloadFile( const std::string& inURL, const Path& outPath );
    bool SplitUrl( std::string url, std::string& baseUrl, std::string& path, bool& isSecure );
}