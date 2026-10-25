#include <doctest/doctest.h>
#include "Path.h"
#include "VFS/VFS.h"
#include "Resource/CookedAssets.h"
#include "Resource/ResourceCache.h"
#include "Tools/Cook/Cooker.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace CookTest
{
    struct TempTree
    {
        explicit TempTree( const char* InRelative )
        {
            Root = std::filesystem::absolute( InRelative ).lexically_normal().generic_string();
            std::filesystem::remove_all( Root );
            std::filesystem::create_directories( Root );
        }

        ~TempTree()
        {
            VFS::Get().ResetToDefaults();
            std::error_code ec;
            std::filesystem::remove_all( Root, ec );
        }

        void Write( const std::string& InRelative, const std::string& InText ) const
        {
            const std::filesystem::path path = Root + "/" + InRelative;
            std::filesystem::create_directories( path.parent_path() );
            std::ofstream( path ) << InText;
        }

        std::string Root;
    };


    struct ScopedCookedRegistry
    {
        ~ScopedCookedRegistry()
        {
            CookedAssets::Get().Reset();
        }
    };
}


TEST_CASE( "cook: assets ship in their runtime form" )
{
    using Cook::AssetKind;
    CHECK( Cook::Classify( "Assets/Textures/Wall.png" ) == AssetKind::Texture );
    CHECK( Cook::Classify( "Assets/Textures/Wall.TGA" ) == AssetKind::Texture );
    CHECK( Cook::Classify( "Assets/Models/Crate.fbx" ) == AssetKind::Model );
    CHECK( Cook::Classify( "Assets/Shaders/Standard.frag" ) == AssetKind::ShaderStage );
    CHECK( Cook::Classify( "Assets/Scenes/Level.lvl" ) == AssetKind::Data );
    CHECK( Cook::Classify( "Assets/Sounds/Hit.wav" ) == AssetKind::Data );
    CHECK( Cook::Classify( "Assets/Textures/Wall.png.dds" ) == AssetKind::Data );

    // Never shipped: import settings, build inputs, editor documents and content, dev caches.
    for( const char* stripped : { "Assets/Textures/Wall.png.meta", "Assets/Shaders/Common.sh", "Assets/Shaders/Standard.var",
             "Assets/Shaders/Standard.frag.spirv.bin.d", "Assets/Scripts/Player.cs", "Assets/ShaderGraphs/Fog.shadergraph",
             "Assets/Scenes/Tests/Flows.edscript", "Assets/Havana/ME-LOGO.png", "Assets/Icons/filled/play.png",
             "Assets/Config/imgui.cfg", ".tmp/MetadataCache.json", "user://Engine.cfg" } )
    {
        CHECK_MESSAGE( Cook::Classify( stripped ) == AssetKind::Stripped, stripped );
        CHECK( Cook::ShippedFiles( stripped, Cook::Platform::Linux ).empty() );
    }

    CHECK( Cook::ShippedFiles( "Assets/Textures/Wall.png", Cook::Platform::Linux ) == std::vector<std::string>{ "Assets/Textures/Wall.png.dds" } );
    CHECK( Cook::ShippedFiles( "Assets/Models/Crate.fbx", Cook::Platform::Linux ) == std::vector<std::string>{ "Assets/Models/Crate.fbx.assbin" } );
    CHECK( Cook::ShippedFiles( "Assets/Scenes/Level.lvl", Cook::Platform::Linux ) == std::vector<std::string>{ "Assets/Scenes/Level.lvl" } );

    // One shader profile per platform: the renderer each one boots.
    CHECK( Cook::ShippedFiles( "Assets/Shaders/Standard.vert", Cook::Platform::Linux ) == std::vector<std::string>{ "Assets/Shaders/Standard.vert.spirv.bin" } );
    CHECK( Cook::ShippedFiles( "Assets/Shaders/Standard.vert", Cook::Platform::Windows ) == std::vector<std::string>{ "Assets/Shaders/Standard.vert.dx11.bin" } );
    CHECK( Cook::ShippedFiles( "Assets/Shaders/Standard.vert", Cook::Platform::MacOS ) == std::vector<std::string>{ "Assets/Shaders/Standard.vert.metal.bin" } );
    Cook::Platform parsed = Cook::Platform::Linux;
    CHECK( Cook::ParsePlatform( "macos", parsed ) );
    CHECK( parsed == Cook::Platform::MacOS );
    CHECK_FALSE( Cook::ParsePlatform( "dreamcast", parsed ) );
}


TEST_CASE( "cook: recorded reads stand for the asset they belong to" )
{
    CHECK( Cook::OwningAsset( "Assets/Textures/Wall.png.dds" ) == "Assets/Textures/Wall.png" );
    CHECK( Cook::OwningAsset( "Assets/Textures/Wall.png.meta" ) == "Assets/Textures/Wall.png" );
    CHECK( Cook::OwningAsset( "Assets/Models/Crate.fbx.assbin" ) == "Assets/Models/Crate.fbx" );
    CHECK( Cook::OwningAsset( "Assets/Models/Crate.fbx.vertexcache" ) == "Assets/Models/Crate.fbx" );
    CHECK( Cook::OwningAsset( "Assets/Shaders/Post/Fog.frag.spirv.bin" ) == "Assets/Shaders/Post/Fog.frag" );
    CHECK( Cook::OwningAsset( "Assets/Config/Input.inputactions.meta" ) == "Assets/Config/Input.inputactions" );
    // Assets of their own.
    CHECK( Cook::OwningAsset( "Assets/Scenes/Level.lvl" ).empty() );
    CHECK( Cook::OwningAsset( "Assets/UI/HUD/Crosshair.png" ).empty() );
    CHECK( Cook::OwningAsset( "Assets/Data/blob.dds" ).empty() );
    CHECK( Cook::OwningAsset( "Assets/Data/table.bin" ).empty() );
}


TEST_CASE( "cook: the static walk follows scenes, prefabs, script fields and shader graphs" )
{
    CookTest::TempTree tree( ".tmp/Tests/CookWalk" );
    tree.Write( "Assets/CookWalk/Scene.lvl", R"({ "Entities": [
        { "Components": [ { "Type": "Mesh", "Material": { "Textures": { "Diffuse": { "Path": "Assets/CookWalk/Wall.png" } } } },
                          { "Type": "Prefab", "Prefab": "Assets/CookWalk/Thing.prefab" },
                          { "Type": "ScriptComponent", "ScriptFields": "{\"Sound\": \"Assets/CookWalk/Hit.wav\"}" },
                          { "Type": "ShaderGraphMaterial", "Graph": "Assets/CookWalk/Glow.shadergraph" },
                          { "Type": "Mesh", "Note": "Assets/CookWalk/Missing.png" } ] } ] })" );
    tree.Write( "Assets/CookWalk/Thing.prefab", R"({ "AssetGUID": "00000000000000ab", "Entities": [ { "Model": "Assets/CookWalk/Crate.fbx" } ] })" );
    tree.Write( "Assets/CookWalk/Glow.sg/Material.json", R"({ "Programs": { "Forward": [ "@Assets/CookWalk/Stage", "Glow" ] } })" );
    for( const char* file : { "Assets/CookWalk/Wall.png", "Assets/CookWalk/Hit.wav", "Assets/CookWalk/Crate.fbx", "Assets/CookWalk/Glow.shadergraph",
             "Assets/CookWalk/Stage.vert", "Assets/CookWalk/Glow.sg/Glow.frag", "Assets/CookWalk/Unnamed.png" } )
    {
        tree.Write( file, "x" );
    }
    VFS::Get().MountDirectory( "", tree.Root, 300 );

    std::set<std::string> assets;
    Cook::CollectStaticDependencies( "Assets/CookWalk/Scene.lvl", assets );
    const std::set<std::string> expected = {
        "Assets/CookWalk/Wall.png",
        "Assets/CookWalk/Thing.prefab",
        "Assets/CookWalk/Crate.fbx",                // through the prefab
        "Assets/CookWalk/Hit.wav",                  // inside the serialized script fields
        "Assets/CookWalk/Glow.shadergraph",         // stripped later; the walk reports what's named
        "Assets/CookWalk/Glow.sg/Material.json",    // what a graph material loads
        "Assets/CookWalk/Stage.vert",               // "@" stages are project paths
        "Assets/CookWalk/Glow.sg/Glow.frag",        // bare stages sit next to the manifest
    };
    CHECK( assets == expected );
}


TEST_CASE( "cook: a cooked registry stands in for stripped sources, metas and GUIDs" )
{
    CookTest::TempTree tree( ".tmp/Tests/CookRuntime" );
    tree.Write( "Assets/CookRuntime/Wall.png.dds", "dds" );
    VFS::Get().MountDirectory( "", tree.Root, 300 );
    CookTest::ScopedCookedRegistry scoped;

    CHECK_FALSE( Path( "Assets/CookRuntime/Wall.png" ).Exists );
    REQUIRE( CookedAssets::Get().LoadFrom( json::parse( R"({ "Version": 1, "Platform": "linux",
        "Assets": {
            "Assets/CookRuntime/Wall.png": { "Stripped": true, "GUID": "00000000000000aa", "Meta": { "FileType": "png", "GUID": "00000000000000aa" } },
            "Assets/CookRuntime/Crate.fbx": { "Stripped": true, "Textures": { "C:\\Art\\crate_diffuse.psd": "Assets/CookRuntime/Wall.png" } },
            "Assets/CookRuntime/Thing.prefab": { "GUID": "00000000000000ab" } },
        "EngineShaderStages": [ "Assets/Shaders/Standard.vert" ] })" ) ) );
    CHECK( CookedAssets::Get().IsCooked() );
    CHECK( CookedAssets::Get().GetPlatform() == "linux" );

    // The source "exists" (loaders go on to its twin) but serves no bytes.
    const Path stripped( "Assets/CookRuntime/Wall.png" );
    CHECK( stripped.Exists );
    CHECK( stripped.IsFile );
    std::vector<uint8_t> bytes;
    CHECK_FALSE( VFS::Get().ReadBytes( std::string( "Assets/CookRuntime/Wall.png" ), bytes ) );
    CHECK( Path( stripped.FullPath + ".dds" ).Exists );
    CHECK_FALSE( Path( "Assets/CookRuntime/Unknown.png" ).Exists );

    // Import settings come from the registry.
    SharedPtr<MetaBase> meta = ResourceCache::GetInstance().LoadMetadata( stripped );
    REQUIRE( meta );
    CHECK( meta->GUID == 0xaa );
    CHECK( CookedAssets::Get().FindMeta( "Assets/CookRuntime/Crate.fbx" ) == nullptr );

    CHECK( CookedAssets::Get().GetGuids().at( "Assets/CookRuntime/Thing.prefab" ) == 0xab );
    CHECK( CookedAssets::Get().FindTextureReference( "Assets/CookRuntime/Crate.fbx", "C:\\Art\\crate_diffuse.psd" ) == "Assets/CookRuntime/Wall.png" );
    CHECK( CookedAssets::Get().FindTextureReference( "Assets/CookRuntime/Crate.fbx", "other.png" ).empty() );
    CHECK( CookedAssets::Get().GetEngineShaderStages() == std::vector<std::string>{ "Assets/Shaders/Standard.vert" } );

    // Back to a source tree: the stand-ins are gone.
    CookedAssets::Get().Reset();
    CHECK_FALSE( CookedAssets::Get().IsCooked() );
    CHECK_FALSE( Path( "Assets/CookRuntime/Wall.png" ).Exists );
    CHECK( CookedAssets::Get().FindMeta( "Assets/CookRuntime/Wall.png" ) == nullptr );
}
