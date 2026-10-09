#pragma once
#include <string>
#include <vector>

namespace Moonlight
{
    // Hot reload support: turns changed shader includes (.sh, via the depfiles shaderc writes next to
    // each binary) and varying definitions (.var) into the shader sources that depend on them, so
    // those recompile and reload too. Appends to InOutChangedPaths (absolute paths).
    void ExpandShaderChanges( std::vector<std::string>& InOutChangedPaths );
}
