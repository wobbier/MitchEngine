#pragma once
#include "JSON.h"
#include <map>
#include <string>
#include <vector>

// MOD-1: a mod's mod.json and the semantic versions it names.
//
//   { "id": "com.example.tinted-crates", "name": "Tinted Crates", "version": "1.2.0",
//     "engine": ">=0.1 <1", "dependencies": { "com.example.core": "^1.0" },
//     "loadAfter": [ "com.example.textures" ], "loadBefore": [], "assemblies": [ "TintedCrates.dll" ],
//     "netRequired": false }
//
// Its folder mounts over the game's files as a VFS layer: a mod's Assets/Textures/Crate.png
// replaces the game's for every logical path that names it.
struct SemVer
{
    int Major = 0;
    int Minor = 0;
    int Patch = 0;

    // "1", "1.2" or "1.2.3" (missing parts are 0; a pre-release or build suffix is ignored).
    static bool Parse( const std::string& InText, SemVer& OutVersion );
    std::string ToString() const;
    int Compare( const SemVer& InOther ) const;
    bool operator<( const SemVer& InOther ) const { return Compare( InOther ) < 0; }
    bool operator==( const SemVer& InOther ) const { return Compare( InOther ) == 0; }
};


// A version range: space-separated comparators that must all hold. "*" (or empty) is anything;
// ">=1.2", ">1.2", "<2", "<=2.1", "=1.2.3" or a bare "1.2.3"; "^1.2.3" (same major; for 0.x the
// same minor); "~1.2" (same major and minor).
struct VersionRange
{
    static bool Parse( const std::string& InText, VersionRange& OutRange );
    bool Contains( const SemVer& InVersion ) const;

    std::string Text;

private:
    struct Comparator
    {
        char Op = '=';      // '=', '<', '>', 'l' (<=), 'g' (>=)
        SemVer Version;
    };
    std::vector<Comparator> m_comparators;
};


struct ModManifest
{
    std::string Id;
    std::string Name;
    SemVer Version;
    std::string EngineRange = "*";
    std::map<std::string, std::string> Dependencies;   // id -> version range
    std::vector<std::string> LoadAfter;
    std::vector<std::string> LoadBefore;
    std::vector<std::string> Assemblies;
    bool NetRequired = false;
    std::string Root;   // its folder (absolute)

    static bool Parse( const json& InJson, ModManifest& OutManifest, std::string& OutError );
};
