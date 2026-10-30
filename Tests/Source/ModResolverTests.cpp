#include "PCH.h"
#include <doctest/doctest.h>
#include "Mods/ModResolver.h"
#include "Mods/Mods.h"
#include "VFS/VFS.h"
#include <filesystem>
#include <fstream>

// MOD-1: mod.json, version ranges, the resolver (order, cycles, missing and mismatched
// dependencies, engine ranges, the player's order and disabled list) and mods as VFS layers.
namespace ModTest
{
    ModManifest Mod( const std::string& InId, const std::string& InVersion = "1.0.0", json InExtra = json::object() )
    {
        json source = InExtra;
        source["id"] = InId;
        source["version"] = InVersion;
        ModManifest manifest;
        std::string error;
        REQUIRE( ModManifest::Parse( source, manifest, error ) );
        manifest.Root = "/mods/" + InId;
        return manifest;
    }

    std::vector<std::string> Ids( const ModResolution& InResolution )
    {
        std::vector<std::string> ids;
        for( const ModManifest* manifest : InResolution.Load )
        {
            ids.push_back( manifest->Id );
        }
        return ids;
    }

    bool Mentions( const ModResolution& InResolution, const std::string& InText )
    {
        for( const std::string& problem : InResolution.Problems )
        {
            if( problem.find( InText ) != std::string::npos )
            {
                return true;
            }
        }
        return false;
    }

    const SemVer kEngine{ 0, 1, 0 };
}

using namespace ModTest;

TEST_CASE( "Mods: versions and ranges" )
{
    SemVer version;
    CHECK( SemVer::Parse( "1.2.3", version ) );
    CHECK( version.ToString() == "1.2.3" );
    CHECK( SemVer::Parse( "2", version ) );
    CHECK( version == SemVer{ 2, 0, 0 } );
    CHECK( SemVer::Parse( "1.4.0-beta+7", version ) );
    CHECK( version == SemVer{ 1, 4, 0 } );
    CHECK_FALSE( SemVer::Parse( "one", version ) );
    CHECK_FALSE( SemVer::Parse( "1.x", version ) );
    CHECK( SemVer{ 1, 2, 3 } < SemVer{ 1, 10, 0 } );

    auto holds = []( const std::string& InRange, const std::string& InVersion ) {
        VersionRange range;
        SemVer version;
        REQUIRE( VersionRange::Parse( InRange, range ) );
        REQUIRE( SemVer::Parse( InVersion, version ) );
        return range.Contains( version );
    };
    CHECK( holds( "*", "9.9.9" ) );
    CHECK( holds( "", "0.0.1" ) );
    CHECK( holds( ">=1.2 <2", "1.9.9" ) );
    CHECK_FALSE( holds( ">=1.2 <2", "2.0.0" ) );
    CHECK_FALSE( holds( ">=1.2 <2", "1.1.9" ) );
    CHECK( holds( "^1.2.3", "1.9.0" ) );
    CHECK_FALSE( holds( "^1.2.3", "2.0.0" ) );
    CHECK_FALSE( holds( "^1.2.3", "1.2.2" ) );
    CHECK( holds( "^0.2.3", "0.2.9" ) );
    CHECK_FALSE( holds( "^0.2.3", "0.3.0" ) );
    CHECK( holds( "~1.2", "1.2.7" ) );
    CHECK_FALSE( holds( "~1.2", "1.3.0" ) );
    CHECK( holds( "1.2", "1.2.5" ) );
    CHECK_FALSE( holds( "1.2", "1.3.0" ) );
    CHECK( holds( "=1.2.3", "1.2.3" ) );
    CHECK_FALSE( holds( "=1.2.3", "1.2.4" ) );
    CHECK( holds( "<=2.1 >1", "2.1.0" ) );
    VersionRange bad;
    CHECK_FALSE( VersionRange::Parse( ">=banana", bad ) );
}


TEST_CASE( "Mods: mod.json is read and checked" )
{
    ModManifest manifest;
    std::string error;
    CHECK( ModManifest::Parse( json::parse( R"({ "id": "com.example.crates", "name": "Crates", "version": "1.2.0", "engine": ">=0.1 <1",
        "dependencies": { "com.example.core": "^1.0" }, "loadAfter": [ "a" ], "loadBefore": [ "b" ], "assemblies": [ "Crates.dll" ],
        "netRequired": true })" ), manifest, error ) );
    CHECK( manifest.Id == "com.example.crates" );
    CHECK( manifest.Version == SemVer{ 1, 2, 0 } );
    CHECK( manifest.Dependencies.at( "com.example.core" ) == "^1.0" );
    CHECK( manifest.LoadAfter == std::vector<std::string>{ "a" } );
    CHECK( manifest.LoadBefore == std::vector<std::string>{ "b" } );
    CHECK( manifest.Assemblies == std::vector<std::string>{ "Crates.dll" } );
    CHECK( manifest.NetRequired );

    CHECK_FALSE( ModManifest::Parse( json::parse( R"({ "name": "No id" })" ), manifest, error ) );
    CHECK( error.find( "id" ) != std::string::npos );
    CHECK_FALSE( ModManifest::Parse( json::parse( R"({ "id": "has space" })" ), manifest, error ) );
    CHECK_FALSE( ModManifest::Parse( json::parse( R"({ "id": "x", "version": "soon" })" ), manifest, error ) );
    CHECK_FALSE( ModManifest::Parse( json::parse( R"({ "id": "x", "dependencies": { "y": ">=nope" } })" ), manifest, error ) );
    CHECK_FALSE( ModManifest::Parse( json::array(), manifest, error ) );
}


TEST_CASE( "Mods: the resolver orders by dependencies and load hints, then the player's order" )
{
    const std::vector<ModManifest> mods = {
        Mod( "ui" ),
        Mod( "core" ),
        Mod( "weapons", "1.0.0", { { "dependencies", { { "core", "^1" } } } } ),
        Mod( "skins", "1.0.0", { { "loadAfter", { "weapons" } } } ),
        Mod( "early", "1.0.0", { { "loadBefore", { "core" } } } ),
    };
    ModSettings settings;
    settings.Order = { "skins", "weapons", "ui", "core", "early" };
    const ModResolution resolution = ModResolver::Resolve( mods, settings, kEngine );
    CHECK( resolution.Problems.empty() );
    // early before core (loadBefore), core before weapons (dependency), weapons before skins
    // (loadAfter); ui wherever the player put it among what's free.
    CHECK( Ids( resolution ) == std::vector<std::string>{ "ui", "early", "core", "weapons", "skins" } );

    // With no order given: by id among what's free (core is held back until early has loaded).
    const ModResolution plain = ModResolver::Resolve( mods, ModSettings(), kEngine );
    CHECK( Ids( plain ) == std::vector<std::string>{ "early", "core", "ui", "weapons", "skins" } );
}


TEST_CASE( "Mods: missing, disabled and mismatched dependencies drop the mod and what needs it" )
{
    const std::vector<ModManifest> mods = {
        Mod( "core", "1.4.0" ),
        Mod( "weapons", "2.0.0", { { "dependencies", { { "core", ">=1.2 <2" } } } } ),
        Mod( "skins", "1.0.0", { { "dependencies", { { "weapons", "^2" } } } } ),
        Mod( "lonely", "1.0.0", { { "dependencies", { { "nowhere", "*" } } } } ),
        Mod( "picky", "1.0.0", { { "dependencies", { { "core", "^2" } } } } ),
        Mod( "future", "1.0.0", { { "engine", ">=5" } } ),
    };
    ModResolution resolution = ModResolver::Resolve( mods, ModSettings(), kEngine );
    CHECK( Ids( resolution ) == std::vector<std::string>{ "core", "weapons", "skins" } );
    CHECK( Mentions( resolution, "lonely: needs nowhere, which isn't installed" ) );
    CHECK( Mentions( resolution, "picky: needs core ^2, found 1.4.0" ) );
    CHECK( Mentions( resolution, "future: needs engine >=5" ) );

    // Disabling core takes weapons, then skins, with it.
    ModSettings settings;
    settings.Disabled = { "core" };
    resolution = ModResolver::Resolve( mods, settings, kEngine );
    CHECK( Ids( resolution ).empty() );
    CHECK( Mentions( resolution, "weapons: needs core, which is disabled or can't load" ) );
    CHECK( Mentions( resolution, "skins: needs weapons, which is disabled or can't load" ) );
}


TEST_CASE( "Mods: cycles and duplicate ids are reported, the rest still load" )
{
    const std::vector<ModManifest> mods = {
        Mod( "a", "1.0.0", { { "loadAfter", { "b" } } } ),
        Mod( "b", "1.0.0", { { "loadAfter", { "a" } } } ),
        Mod( "c", "1.0.0", { { "dependencies", { { "a", "*" } } } } ),
        Mod( "free" ),
        Mod( "free", "2.0.0" ),
    };
    const ModResolution resolution = ModResolver::Resolve( mods, ModSettings(), kEngine );
    CHECK( Ids( resolution ) == std::vector<std::string>{ "free" } );
    CHECK( resolution.Load.front()->Version == SemVer{ 1, 0, 0 } );   // the first one found
    CHECK( Mentions( resolution, "a: its load order is circular" ) );
    CHECK( Mentions( resolution, "b: its load order is circular" ) );
    CHECK( Mentions( resolution, "c: its load order is circular" ) );
    CHECK( Mentions( resolution, "free: another mod with this id was found first" ) );
}


TEST_CASE( "Mods: the player's settings round-trip" )
{
    ModSettings settings;
    settings.Order = { "b", "a" };
    settings.Disabled = { "c" };
    const ModSettings back = ModSettings::FromJson( json::parse( settings.ToJson().dump() ) );
    CHECK( back.Order == settings.Order );
    CHECK( back.Disabled == settings.Disabled );
    CHECK( ModSettings::FromJson( json::parse( "[]" ) ).Order.empty() );
}


TEST_CASE( "Mods: discovered folders mount over the game, later mods over earlier ones" )
{
    namespace fs = std::filesystem;
    const std::string root = fs::absolute( ".tmp/ModTests" ).lexically_normal().generic_string();
    fs::remove_all( root );
    auto write = [&root]( const std::string& InRelative, const std::string& InText ) {
        fs::create_directories( fs::path( root + "/" + InRelative ).parent_path() );
        std::ofstream( root + "/" + InRelative ) << InText;
    };
    write( "Game/Assets/Textures/Crate.png", "game crate" );
    write( "Game/Assets/Textures/Barrel.png", "game barrel" );
    write( "Mods/Red/mod.json", R"({ "id": "red", "version": "1.0.0" })" );
    write( "Mods/Red/Assets/Textures/Crate.png", "red crate" );
    write( "Mods/Red/Assets/Textures/Barrel.png", "red barrel" );
    write( "Mods/Blue/mod.json", R"({ "id": "blue", "version": "1.0.0", "loadAfter": [ "red" ] })" );
    write( "Mods/Blue/Assets/Textures/Crate.png", "blue crate" );
    write( "Mods/Broken/mod.json", "{ not json" );

    std::vector<std::string> problems;
    const std::vector<ModManifest> mods = ModManager::Discover( { root + "/Mods", root + "/Nowhere" }, &problems );
    REQUIRE( mods.size() == 2 );
    REQUIRE( problems.size() == 1 );
    CHECK( problems[0].find( "Broken" ) != std::string::npos );

    VFS::Get().MountDirectory( "modtest://", root + "/Game", 100, false );
    const ModResolution resolution = ModResolver::Resolve( mods, ModSettings(), kEngine );
    ModManager manager;
    manager.Mount( resolution, "modtest://" );
    CHECK( manager.GetLoadedIds() == std::vector<std::string>{ "red", "blue" } );
    std::string text;
    CHECK( VFS::Get().ReadText( std::string( "modtest://Assets/Textures/Crate.png" ), text ) );
    CHECK( text == "blue crate" );      // the later mod wins
    CHECK( VFS::Get().ReadText( std::string( "modtest://Assets/Textures/Barrel.png" ), text ) );
    CHECK( text == "red barrel" );      // only red has one
    manager.UnmountAll();
    CHECK( VFS::Get().ReadText( std::string( "modtest://Assets/Textures/Crate.png" ), text ) );
    CHECK( text == "game crate" );
    VFS::Get().UnmountAll( "modtest://" );
    fs::remove_all( root );
}
