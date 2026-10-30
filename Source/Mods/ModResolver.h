#pragma once
#include "Mods/ModManifest.h"
#include <set>
#include <string>
#include <vector>

// MOD-1: which discovered mods load, in what order, and why the others don't.
//
// The player's choices live in user://Config/Mods.json: { "Order": [ ids ], "Disabled": [ ids ] }.
// A mod not named in Order is enabled and goes after the named ones (by id), so a new mod just
// works. Resolving then:
//   - drops duplicates (the first one discovered wins), disabled mods, mods whose "engine" range
//     doesn't hold this engine, and mods with a dependency that's missing, disabled, dropped or
//     outside its range; dropping cascades to whatever depended on them;
//   - orders the rest so dependencies and "loadAfter" targets come first and "loadBefore" ones
//     later; otherwise the player's order. Mods caught in a cycle are dropped.
struct ModSettings
{
    std::vector<std::string> Order;
    std::set<std::string> Disabled;

    static ModSettings FromJson( const json& InJson );
    json ToJson() const;
};


struct ModResolution
{
    // Loads in this order: later ones win where two mods have the same file.
    std::vector<const ModManifest*> Load;
    // One line per mod that doesn't load, naming it and the reason.
    std::vector<std::string> Problems;
};


namespace ModResolver
{
    ModResolution Resolve( const std::vector<ModManifest>& InDiscovered, const ModSettings& InSettings, const SemVer& InEngine );
}
