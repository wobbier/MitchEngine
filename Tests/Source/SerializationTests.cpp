#include <doctest/doctest.h>

#include "World/SceneSerializer.h"
#include "Resource/AssetDatabase.h"
#include "Engine/World.h"
#include "ECS/Core.h"
#include "Components/Transform.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace SerializationTest
{
    class Link
        : public Component<Link>
    {
        ME_REFLECTABLE( Link )
    public:
        Link() : Component( "SerializationTest::Link" ) {}
        EntityHandle Target;
        int Score = 0;
    };

    std::shared_ptr<World> MakeWorld()
    {
        auto world = std::make_shared<World>();
        world->IsLoading = false;
        return world;
    }

    size_t CountV1Entities( const json& objects )
    {
        size_t count = 0;
        std::function<void( const json& )> visit = [&]( const json& object ) {
            if( !object.is_object() )
            {
                return;
            }
            ++count;
            if( object.contains( "Children" ) )
            {
                const json& children = object["Children"];
                if( children.is_array() )
                {
                    for( const json& child : children )
                    {
                        visit( child );
                    }
                }
                else
                {
                    visit( children );
                }
            }
        };
        for( const json& object : objects )
        {
            visit( object );
        }
        return count;
    }
}

using namespace SerializationTest;

ME_REFLECT_BEGIN( SerializationTest::Link )
    ME_FIELD( Target );
    ME_FIELD( Score ).FormerName( "Points" ).FormerName( "Points2" );
ME_REFLECT_END()
ME_REGISTER_COMPONENT( SerializationTest::Link )
ME_REGISTER_COMPONENT_ALIAS( SerializationTest::Link, "OldLink" )
ME_REGISTER_CORE_ALIAS( NavigationCore, "TestFormerNavigationCore" )


TEST_CASE( "Serialization: every scene and prefab in the repo migrates to v2" )
{
    namespace fs = std::filesystem;
    size_t files = 0;
    for( const char* root : { "Assets", "Engine/Assets" } )
    {
        std::error_code ec;
        if( !fs::exists( root, ec ) )
        {
            continue;
        }
        for( const auto& entry : fs::recursive_directory_iterator( root, ec ) )
        {
            const std::string extension = entry.path().extension().string();
            if( extension != ".lvl" && extension != ".prefab" )
            {
                continue;
            }
            std::ifstream stream( entry.path() );
            const json original = json::parse( stream, nullptr, false );
            DOCTEST_INFO( entry.path().string() );
            REQUIRE_FALSE( original.is_discarded() );
            ++files;

            const json migrated = SceneSerializer::MigrateToLatest( original );
            CHECK( migrated["Version"] == SceneSerializer::kVersion );
            REQUIRE( migrated["Entities"].is_array() );

            size_t expected = 0;
            if( original.contains( "Scene" ) )
            {
                expected = CountV1Entities( original["Scene"] );
            }
            else if( original.contains( "Components" ) )
            {
                expected = CountV1Entities( json::array( { original } ) );
            }
            else if( original.contains( "Entities" ) )
            {
                expected = original["Entities"].size();
            }
            CHECK( migrated["Entities"].size() == expected );

            // GUIDs are unique and every parent reference points at an earlier entity.
            std::unordered_set<uint64_t> seen;
            for( const json& entity : migrated["Entities"] )
            {
                const uint64_t guid = SceneSerializer::GUIDFromJson( entity["GUID"] );
                CHECK( guid != 0 );
                CHECK( seen.insert( guid ).second );
                if( entity.contains( "Parent" ) )
                {
                    CHECK( seen.count( SceneSerializer::GUIDFromJson( entity["Parent"] ) ) == 1 );
                }
            }
        }
    }
    CHECK( files > 0 );
}


TEST_CASE( "Serialization: GUID strings round-trip" )
{
    const uint64_t guid = 0x9f3c00112233aabbull;
    const std::string text = SceneSerializer::GUIDToString( guid );
    CHECK( text == "9f3c00112233aabb" );
    CHECK( SceneSerializer::GUIDFromJson( text ) == guid );
    CHECK( SceneSerializer::GUIDFromJson( json( guid ) ) == guid );
    CHECK( SceneSerializer::GUIDFromJson( json( nullptr ) ) == 0 );
}


TEST_CASE( "Serialization: hierarchy, components and references round-trip" )
{
    auto source = MakeWorld();
    EntityHandle root = source->CreateEntity( "Root" );
    Transform& rootTransform = root->AddComponent<Transform>();
    rootTransform.SetPosition( Vector3( 1.f, 2.f, 3.f ) );
    EntityHandle child = source->CreateEntity( "Child" );
    Transform& childTransform = child->AddComponent<Transform>();
    childTransform.SetParent( rootTransform );
    childTransform.SetPosition( Vector3( 0.f, 5.f, 0.f ) );
    childTransform.SetRotation( Vector3( 0.f, 45.f, 0.f ) );
    Link& link = child->AddComponent<Link>();
    link.Target = root;
    link.Score = 7;
    child->SetActive( false );
    child->SetLayer( 3 );

    const json saved = SceneSerializer::SerializeEntities( *source, { root.Get() } );
    REQUIRE( saved["Entities"].size() == 2 );
    CHECK( saved["Entities"][0]["Name"] == "Root" );
    CHECK( saved["Entities"][1]["Parent"] == saved["Entities"][0]["GUID"] );
    CHECK( saved["Entities"][1]["Active"] == false );

    // Load into a fresh world keeping GUIDs.
    auto loadedWorld = MakeWorld();
    SceneSerializer::LoadOptions options;
    std::vector<EntityHandle> roots = SceneSerializer::Deserialize( *loadedWorld, saved, options );
    REQUIRE( roots.size() == 1 );
    CHECK( roots[0]->GetGUID() == root->GetGUID() );
    Transform& loadedRoot = roots[0]->GetComponent<Transform>();
    REQUIRE( loadedRoot.GetChildren().size() == 1 );
    Transform* loadedChild = loadedRoot.GetChildren()[0];
    CHECK( loadedChild->GetName() == "Child" );
    CHECK( loadedChild->GetWorldPosition().y == doctest::Approx( 7.f ) );
    CHECK( loadedChild->GetRotationEuler().y == doctest::Approx( 45.f ).epsilon( 0.01 ) );
    Entity* loadedChildEntity = loadedChild->Parent.Get();
    CHECK_FALSE( loadedChildEntity->IsActiveSelf() );
    CHECK( loadedChildEntity->GetLayer() == 3 );
    Link& loadedLink = loadedChildEntity->GetComponent<Link>();
    CHECK( loadedLink.Score == 7 );
    CHECK( loadedLink.Target == roots[0] );
}


TEST_CASE( "Serialization: instancing twice remaps GUIDs and internal references" )
{
    auto world = MakeWorld();
    EntityHandle root = world->CreateEntity( "Prefab" );
    root->AddComponent<Transform>();
    EntityHandle child = world->CreateEntity( "Part" );
    child->AddComponent<Transform>().SetParent( root->GetComponent<Transform>() );
    child->AddComponent<Link>().Target = root;

    const json data = SceneSerializer::SerializeEntities( *world, { root.Get() } );

    SceneSerializer::LoadOptions options;
    options.RemapGUIDs = true;
    EntityHandle a = SceneSerializer::Deserialize( *world, data, options ).front();
    EntityHandle b = SceneSerializer::Deserialize( *world, data, options ).front();

    CHECK( a->GetGUID() != root->GetGUID() );
    CHECK( a->GetGUID() != b->GetGUID() );

    // Each instance's Link points at its own root, not the original or the other copy.
    Entity* partA = a->GetComponent<Transform>().GetChildren()[0]->Parent.Get();
    Entity* partB = b->GetComponent<Transform>().GetChildren()[0]->Parent.Get();
    CHECK( partA->GetComponent<Link>().Target == a );
    CHECK( partB->GetComponent<Link>().Target == b );
}


TEST_CASE( "Serialization: v1 scene objects migrate with Euler rotation and nested children" )
{
    const json v1 = json::parse( R"({
        "Cores": [ { "Type": "SomeCore" } ],
        "Scene": [ {
            "Name": "A", "DestroyOnLoad": false,
            "Components": [ { "Type": "Transform", "Position": [1, 0, 0], "Rotation": [0, 90, 0], "Scale": [1, 1, 1] } ],
            "Children": [ { "Name": "B", "Components": [ { "Type": "Transform", "Position": [0, 1, 0], "Rotation": [0, 0, 0], "Scale": [1, 1, 1] } ] } ]
        } ]
    })" );

    auto world = MakeWorld();
    SceneSerializer::LoadOptions options;
    options.LoadCores = false;
    std::vector<EntityHandle> roots = SceneSerializer::Deserialize( *world, v1, options );
    REQUIRE( roots.size() == 1 );
    CHECK( roots[0]->GetName() == "A" );
    CHECK_FALSE( roots[0]->GetDestroyOnLoad() );
    Transform& a = roots[0]->GetComponent<Transform>();
    CHECK( a.GetRotationEuler().y == doctest::Approx( 90.f ).epsilon( 0.01 ) );
    REQUIRE( a.GetChildren().size() == 1 );
    CHECK( a.GetChildren()[0]->GetName() == "B" );
}


TEST_CASE( "Serialization: asset references follow assets that moved" )
{
    namespace fs = std::filesystem;
    const std::string root = ".tmp/Tests/AssetRefs";
    fs::remove_all( root );
    fs::create_directories( root + "/Old" );
    fs::create_directories( root + "/New" );
    const std::string oldPath = root + "/Old/Crate.png";
    const std::string newPath = root + "/New/Crate.png";
    const std::string stayPath = root + "/Old/Stays.wav";
    std::ofstream( oldPath ) << "png";
    std::ofstream( stayPath ) << "wav";
    const uint64_t crate = 0x5eed000000000001ull;
    const uint64_t stays = 0x5eed000000000002ull;
    AssetDatabase::Get().Register( oldPath, crate );
    AssetDatabase::Get().Register( stayPath, stays );

    json scene = json::parse( R"({
        "Version": 2,
        "Entities": [ { "GUID": "00000000000000a1", "Name": "Crate",
            "Components": [ { "Type": "Mesh", "Textures": { "Diffuse": ")" + oldPath + R"(" } },
                            { "Type": "AudioSource", "FilePath": ")" + stayPath + R"(" },
                            { "Type": "Note", "Text": "not/a path to anything.txt" } ] } ]
    })" );

    // Saving records every known asset path by GUID.
    SceneSerializer::PrepareForSave( scene, root + "/Scene.lvl", false );
    REQUIRE( scene.contains( "AssetReferences" ) );
    CHECK( scene["AssetReferences"].size() == 2 );
    CHECK( scene["AssetReferences"][SceneSerializer::GUIDToString( crate )] == oldPath );
    CHECK( scene["AssetReferences"][SceneSerializer::GUIDToString( stays )] == stayPath );

    // Nothing moved: loading changes nothing.
    json unchanged = scene;
    CHECK( SceneSerializer::RemapAssetReferences( unchanged, "test" ) == 0 );
    CHECK( unchanged == scene );

    // The crate moves (outside the editor; a later scan finds its .meta at the new path).
    fs::rename( oldPath, newPath );
    AssetDatabase::Get().Unregister( oldPath );
    AssetDatabase::Get().Register( newPath, crate );
    json loaded = scene;
    CHECK( SceneSerializer::RemapAssetReferences( loaded, "test" ) == 2 );   // the component and the table
    const json& components = loaded["Entities"][0]["Components"];
    CHECK( components[0]["Textures"]["Diffuse"] == newPath );
    CHECK( components[1]["FilePath"] == stayPath );
    CHECK( components[2]["Text"] == "not/a path to anything.txt" );
    CHECK( loaded["AssetReferences"][SceneSerializer::GUIDToString( crate )] == newPath );

    // Moved in the editor: the database keeps the former path, so a scene saved before it reloads
    // still records the asset (under its old path) and the next load fixes it.
    AssetDatabase::Get().Move( newPath, oldPath );
    fs::rename( newPath, oldPath );
    CHECK( AssetDatabase::Get().FindGUID( newPath ) == crate );
    CHECK( AssetDatabase::Get().FindMovedPath( newPath ) == oldPath );
    json stale = loaded;
    SceneSerializer::PrepareForSave( stale, root + "/Scene.lvl", false );
    CHECK( stale["AssetReferences"][SceneSerializer::GUIDToString( crate )] == newPath );
    CHECK( SceneSerializer::RemapAssetReferences( stale, "test" ) == 2 );
    CHECK( stale["Entities"][0]["Components"][0]["Textures"]["Diffuse"] == oldPath );

    // Prefabs carry a stable asset GUID of their own.
    const std::string prefabPath = root + "/Thing.prefab";
    json prefab = json::parse( R"({ "Version": 2, "Entities": [] })" );
    SceneSerializer::PrepareForSave( prefab, prefabPath, true );
    const uint64_t prefabGUID = SceneSerializer::GUIDFromJson( prefab["AssetGUID"] );
    CHECK( prefabGUID != 0 );
    CHECK( AssetDatabase::Get().FindGUID( SceneSerializer::NormalizePrefabPath( prefabPath ) ) == prefabGUID );
    json resaved = json::parse( R"({ "Version": 2, "Entities": [] })" );
    SceneSerializer::PrepareForSave( resaved, prefabPath, true );
    CHECK( SceneSerializer::GUIDFromJson( resaved["AssetGUID"] ) == prefabGUID );   // same file, same GUID

    AssetDatabase::Get().Unregister( root );
    fs::remove_all( root );
}

TEST_CASE( "Assets: the database follows moved folders and remembers former paths" )
{
    AssetDatabase& database = AssetDatabase::Get();
    database.Register( "Tests/AssetDb/Folder/a.png", 0x5eed0000000000a1ull );
    database.Register( "Tests/AssetDb/Folder/Sub/b.wav", 0x5eed0000000000a2ull );
    database.Register( "Tests/AssetDb/c.fbx", 0x5eed0000000000a3ull );

    database.Move( "Tests/AssetDb/Folder", "Tests/AssetDb/Renamed" );
    CHECK( database.FindPath( 0x5eed0000000000a1ull ) == "Tests/AssetDb/Renamed/a.png" );
    CHECK( database.FindPath( 0x5eed0000000000a2ull ) == "Tests/AssetDb/Renamed/Sub/b.wav" );
    CHECK( database.FindPath( 0x5eed0000000000a3ull ) == "Tests/AssetDb/c.fbx" );
    CHECK( database.FindGUID( "Tests/AssetDb/Folder/a.png" ) == 0x5eed0000000000a1ull );
    CHECK( database.FindMovedPath( "Tests/AssetDb/Folder/Sub/b.wav" ) == "Tests/AssetDb/Renamed/Sub/b.wav" );
    CHECK( database.FindMovedPath( "Tests/AssetDb/c.fbx" ).empty() );

    // Moved twice: the first location resolves to the latest.
    database.Move( "Tests/AssetDb/Renamed/a.png", "Tests/AssetDb/a.png" );
    CHECK( database.FindMovedPath( "Tests/AssetDb/Folder/a.png" ) == "Tests/AssetDb/a.png" );

    database.Unregister( "Tests/AssetDb" );
    CHECK( database.FindPath( 0x5eed0000000000a1ull ).empty() );
    CHECK( database.FindPath( 0x5eed0000000000a2ull ).empty() );
    CHECK( database.FindPath( 0x5eed0000000000a3ull ).empty() );
}


TEST_CASE( "Serialization: renamed component types, core types and fields still load" )
{
    json data = json::parse( R"({ "Version": 2, "Entities": [
        { "GUID": "00000000000000b1", "Name": "Old", "Components": [ { "Type": "OldLink", "Points": 42 } ] },
        { "GUID": "00000000000000b2", "Name": "Older", "Components": [ { "Type": "OldLink", "Points2": 5 } ] },
        { "GUID": "00000000000000b3", "Name": "Both", "Components": [ { "Type": "OldLink", "Points": 1, "Score": 9 } ] } ] })" );
    auto world = MakeWorld();
    SceneSerializer::LoadOptions options;
    options.LoadCores = false;
    std::vector<EntityHandle> roots = SceneSerializer::Deserialize( *world, data, options );
    REQUIRE( roots.size() == 3 );
    REQUIRE( roots[0]->HasComponent<Link>() );
    CHECK( roots[0]->GetComponent<Link>().Score == 42 );
    CHECK( roots[1]->GetComponent<Link>().Score == 5 );     // any former name
    CHECK( roots[2]->GetComponent<Link>().Score == 9 );     // the current key wins

    // Saving writes the current names.
    const json saved = SceneSerializer::SerializeEntities( *world, { roots[0].Get() } );
    const json& component = saved["Entities"][0]["Components"][0];
    CHECK( component["Type"] == "SerializationTest::Link" );
    CHECK( component["Score"] == 42 );
    CHECK_FALSE( component.contains( "Points" ) );

    // Name lookups follow the aliases too.
    CHECK( roots[0]->GetComponentByName( "OldLink" ) == &roots[0]->GetComponent<Link>() );
    CHECK( FindComponentInfo( "OldLink" ) == GetComponentRegistry().find( "SerializationTest::Link" ) );
    CHECK( FindCoreFactory( "TestFormerNavigationCore" ) == GetCoreRegistry().find( "NavigationCore" ) );
    CHECK( FindCoreFactory( "NoSuchCore" ) == GetCoreRegistry().end() );
}
