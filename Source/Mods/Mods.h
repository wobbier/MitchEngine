#pragma once
#include "Mods/ModResolver.h"
#include <string>
#include <vector>

// MOD-1: finds mods, resolves them against the player's choices and mounts the ones that load.
//
// Discovery: every folder with a mod.json directly under <project>/Mods, user://Mods and any
// --mod-dir <folder> (tests, a Mod Kit). Each loaded mod's folder mounts at the VFS root at
// priority 200 + its place in the order, above the game (100) and a pak (150), so its files
// replace the game's and a later mod's replace an earlier one's. Game builds apply mods at
// start-up (--no-mods skips them); the editor only with --mods, so it edits the game's own files.
// Mods are trusted: MOD-2 loads their assemblies with full .NET (see Docs/Modding.md).
class ModManager
{
public:
    static ModManager& Get();

    static constexpr int kBasePriority = 200;
    static constexpr const char* kSettingsPath = "user://Config/Mods.json";

    // Every mod.json directly under InDirectories' subfolders, in that order (problems logged).
    static std::vector<ModManifest> Discover( const std::vector<std::string>& InDirectories, std::vector<std::string>* OutProblems = nullptr );

    // Discover (project, user and --mod-dir folders), resolve against user://Config/Mods.json,
    // mount the result under InPrefix ("" in the engine). Replaces a previous Apply's mounts.
    void Apply( const std::string& InPrefix = "" );
    // For tests: mount an already resolved set.
    void Mount( const ModResolution& InResolution, const std::string& InPrefix );
    void UnmountAll();

    const std::vector<ModManifest>& GetDiscovered() const { return m_discovered; }
    const std::vector<std::string>& GetLoadedIds() const { return m_loaded; }
    const std::vector<std::string>& GetProblems() const { return m_problems; }

    ModSettings LoadSettings() const;
    bool SaveSettings( const ModSettings& InSettings ) const;

private:
    std::vector<ModManifest> m_discovered;
    std::vector<std::string> m_loaded;
    std::vector<std::string> m_problems;
    std::vector<std::shared_ptr<class IMountSource>> m_mounts;
};
