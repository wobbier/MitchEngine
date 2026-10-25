#pragma once

#include <cstdint>
#include <string>

// The editor installs richer implementations of the script inspector's reference rows here
// (ReflectionUI's entity and asset rows, with hierarchy/browser drag-drop, select and pick).
// The engine-side bindings fall back to plain rows when nothing is installed, so game-tools
// builds still draw something sensible.
namespace ScriptInspectorRows
{
    using EntityRowFn = bool ( * )( const char* InLabel, uint64_t& InOutGuid );
    using AssetRowFn = bool ( * )( const char* InLabel, std::string& InOutPath, const char* InExtensions );

    inline EntityRowFn EntityRow = nullptr;
    inline AssetRowFn AssetRow = nullptr;
}
