#pragma once
#include "TreeRecipe.h"
#include <string>
#include <vector>

namespace MitchTree
{
    // Built-in starting points (the editor's New Tree menu and Engine/Assets/Trees/Presets).
    const std::vector<std::string>& PresetNames();
    bool MakePreset( const std::string& InName, TreeRecipe& OutRecipe );
}
