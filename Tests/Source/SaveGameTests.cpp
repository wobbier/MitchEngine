#include <doctest/doctest.h>
#include "Engine/SaveGames.h"
#include "JSON.h"
#include "VFS/VFS.h"
#include <filesystem>

TEST_CASE( "Save games: slot names are folder-safe" )
{
    CHECK( SaveGames::IsValidSlot( "slot1" ) );
    CHECK( SaveGames::IsValidSlot( "Chapter-2_autosave" ) );
    CHECK_FALSE( SaveGames::IsValidSlot( "" ) );
    CHECK_FALSE( SaveGames::IsValidSlot( "../escape" ) );
    CHECK_FALSE( SaveGames::IsValidSlot( "a b" ) );
    CHECK_FALSE( SaveGames::IsValidSlot( std::string( 65, 'a' ) ) );
    CHECK( SaveGames::GetSlotDirectory( "slot1" ) == "user://Saves/slot1" );
}


TEST_CASE( "Save games: a slot round-trips its metadata and world atomically" )
{
    const std::string directory = "user://SaveGameTests/slot1";
    const std::filesystem::path physical = VFS::Get().Resolve( directory ).PhysicalPath;
    std::filesystem::remove_all( physical.parent_path() );

    const json world = {
        { "Version", 2 },
        { "Cores", json::array() },
        { "Entities", { { { "GUID", "00000000000000aa" }, { "Name", "Crate" },
            { "Components", { { { "Type", "Transform" }, { "Position", { 1.0, 2.0, 3.0 } } } } } } } },
    };
    const json meta = { { "Name", "Chapter 2" }, { "Scene", "Assets/Scenes/Showcase/Playground.lvl" }, { "Date", "2026-10-29T06:00:00Z" }, { "Playtime", 125.5 } };
    REQUIRE( SaveGames::WriteSlot( directory, meta, world ) );
    CHECK( std::filesystem::exists( physical / "Save.json" ) );
    CHECK( std::filesystem::exists( physical / "Meta.json" ) );
    // Written through temporary files that the renames consumed.
    CHECK_FALSE( std::filesystem::exists( physical / "Save.json.tmp" ) );
    CHECK_FALSE( std::filesystem::exists( physical / "Meta.json.tmp" ) );

    json readMeta;
    json readWorld;
    REQUIRE( SaveGames::ReadSlot( directory, readMeta, readWorld ) );
    CHECK( readMeta["Name"] == "Chapter 2" );
    CHECK( readMeta["Version"] == SaveGames::kVersion );
    CHECK( readWorld["Entities"][0]["Name"] == "Crate" );
    CHECK( readWorld["Entities"][0]["Components"][0]["Position"] == json( { 1.0, 2.0, 3.0 } ) );

    const SaveGames::SlotInfo info = SaveGames::SlotInfo::FromJson( "slot1", readMeta );
    CHECK( info.Name == "Chapter 2" );
    CHECK( info.Playtime == doctest::Approx( 125.5 ) );
    CHECK( info.ToJson()["Scene"] == "Assets/Scenes/Showcase/Playground.lvl" );

    // What isn't a save says so.
    REQUIRE( VFS::Get().WriteText( directory + "/Save.json", "{ \"Version\": 1 }" ) );
    std::string error;
    CHECK_FALSE( SaveGames::ReadSlot( directory, readMeta, readWorld, &error ) );
    CHECK( error.find( "isn't a save" ) != std::string::npos );
    CHECK_FALSE( SaveGames::ReadSlot( "user://SaveGameTests/missing", readMeta, readWorld, &error ) );
    std::filesystem::remove_all( physical.parent_path() );
}


TEST_CASE( "Save games: older and newer documents migrate to the current version" )
{
    // No Version: the first format. Its world (a v1 scene) upgrades through the scene serializer.
    const json old = { { "Meta", { { "Name", "Old" } } }, { "World", { { "Scene", json::array() } } } };
    const json migrated = SaveGames::Migrate( old );
    CHECK( migrated["Version"] == SaveGames::kVersion );
    CHECK( migrated["World"].contains( "Entities" ) );

    std::string notes;
    const json future = SaveGames::Migrate( { { "Version", SaveGames::kVersion + 1 }, { "World", json::object() } }, &notes );
    CHECK( notes.find( "newer" ) != std::string::npos );
    CHECK( future["Version"] == SaveGames::kVersion + 1 );
}
