#include <doctest/doctest.h>

#include "World/SceneSerializer.h"
#include "World/SceneStreaming.h"
#include "Resource/AssetDatabase.h"
#include "Engine/World.h"
#include "ECS/Core.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Graphics/DynamicSky.h"
#include <cmath>
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


TEST_CASE( "Scenes: additive scenes load tagged, never save with the main scene, and unload together" )
{
    // A level chunk on disk: a root with a child that references it.
    auto source = MakeWorld();
    EntityHandle chunk = source->CreateEntity( "Chunk" );
    chunk->AddComponent<Transform>();
    EntityHandle rock = source->CreateEntity( "Rock" );
    rock->AddComponent<Transform>().SetParent( chunk->GetComponent<Transform>() );
    rock->AddComponent<Link>().Target = chunk;
    std::filesystem::create_directories( ".tmp/Tests/Scenes" );
    const std::string path = ".tmp/Tests/Scenes/Chunk.lvl";
    std::ofstream( path ) << SceneSerializer::SerializeEntities( *source, { chunk.Get() } ).dump( 4 );

    // The main scene: a scene root (SceneCore's, in the engine) with one entity.
    auto world = MakeWorld();
    EntityHandle sceneRoot = world->CreateEntity( "Scene Root" );
    Transform& rootTransform = sceneRoot->AddComponent<Transform>();
    EntityHandle main = world->CreateEntity( "Main" );
    main->AddComponent<Transform>().SetParent( rootTransform );

    SceneStreaming streaming;
    const uint16_t first = streaming.Load( *world, path );
    REQUIRE( first != 0 );
    CHECK( streaming.GetState( first ) == SceneStreaming::State::Loaded );
    EntityHandle loadedChunk = world->FindEntityByName( "Chunk" );
    REQUIRE( loadedChunk );
    loadedChunk->GetComponent<Transform>().SetParent( rootTransform );
    CHECK( world->GetEntityScene( loadedChunk->GetId() ) == first );
    CHECK( world->GetEntityScene( main->GetId() ) == 0 );
    Transform* loadedRock = loadedChunk->GetComponent<Transform>().GetChildren()[0];
    CHECK( world->GetEntityScene( loadedRock->Parent->GetId() ) == first );
    CHECK( loadedRock->Parent->GetComponent<Link>().Target == loadedChunk );

    // Saving the main scene leaves the additive one out.
    const json saved = SceneSerializer::SerializeWorld( *world, &rootTransform );
    REQUIRE( saved["Entities"].size() == 1 );
    CHECK( saved["Entities"][0]["Name"] == "Main" );

    // The same scene again, in the background: fresh GUIDs, its own references.
    const uint16_t second = streaming.LoadAsync( path );
    CHECK( second != first );
    CHECK( streaming.Wait( *world ) == std::vector<uint16_t>{ second } );
    CHECK( streaming.GetState( second ) == SceneStreaming::State::Loaded );
    std::vector<uint64_t> chunkGUIDs;
    world->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetName() == "Chunk" )
        {
            chunkGUIDs.push_back( InEntity.GetGUID() );
        }
    } );
    REQUIRE( chunkGUIDs.size() == 2 );
    CHECK( chunkGUIDs[0] != chunkGUIDs[1] );
    size_t chunks = 0;

    // Unloading destroys just that scene's entities, at the next sync point.
    CHECK( streaming.Unload( *world, first ) );
    world->Simulate();
    CHECK_FALSE( loadedChunk );
    CHECK( main );
    chunks = 0;
    world->ForEachEntity( [&]( Entity& InEntity ) { chunks += InEntity.GetName() == "Chunk" ? 1 : 0; } );
    CHECK( chunks == 1 );
    CHECK( streaming.GetState( first ) == SceneStreaming::State::None );
    CHECK( streaming.GetLoaded() == std::vector<uint16_t>{ second } );

    // A missing file fails to load (sync) or reports Failed (async).
    CHECK( streaming.Load( *world, ".tmp/Tests/Scenes/Missing.lvl" ) == 0 );
    const uint16_t missing = streaming.LoadAsync( ".tmp/Tests/Scenes/Missing.lvl" );
    streaming.Wait( *world );
    CHECK( streaming.GetState( missing ) == SceneStreaming::State::Failed );
}

TEST_CASE( "Camera: metre defaults and clipping settings survive serialization" )
{
    Camera camera;
    CHECK( camera.Near == doctest::Approx( 0.1f ) );
    CHECK( camera.Far == doctest::Approx( 1000.f ) );
    camera.Deserialize( json{ { "Near", 0.05 }, { "Far", 250.0 }, { "OrthographicSize", 2.5 }, { "FieldOfView", 75.0 } } );
    json saved;
    camera.Serialize( saved );
    Camera restored;
    restored.Deserialize( saved );
    CHECK( restored.Near == doctest::Approx( 0.05f ) );
    CHECK( restored.Far == doctest::Approx( 250.f ) );
    CHECK( restored.OrthographicSize == doctest::Approx( 2.5f ) );
    CHECK( restored.GetFOV() == doctest::Approx( 75.f ) );

    restored.Deserialize( json{ { "Near", -1.0 }, { "Far", -5.0 } } );
    CHECK( restored.Near >= 0.01f );
    CHECK( restored.Far > restored.Near );
    restored.Near = 500.f;
    restored.OnPropertyChanged( "Near" );
    CHECK( restored.Far > restored.Near );
}

TEST_CASE( "Camera: procedural sky settings round-trip independently of the renderer and background mode" )
{
    Camera camera;
    camera.Deserialize( json{
        { "ClearType", "Procedural" },
        { "ProceduralSky", { { "Time", 6.25f }, { "TimeScale", 0.2f }, { "Latitude", 43.7f }, { "Turbidity", 4.5f }, { "Month", 9 } } }
    } );
    json saved;
    camera.Serialize( saved );
    CHECK( saved["ProceduralSky"]["Time"].get<float>() == doctest::Approx( 6.25f ) );

    Camera restored;
    restored.Deserialize( saved );
    CHECK( restored.ClearType == Moonlight::ClearColorType::Procedural );
    CHECK( restored.ProceduralSky.Time == doctest::Approx( 6.25f ) );
    CHECK( restored.ProceduralSky.TimeScale == doctest::Approx( 0.2f ) );
    CHECK( restored.ProceduralSky.Latitude == doctest::Approx( 43.7f ) );
    CHECK( restored.ProceduralSky.Turbidity == doctest::Approx( 4.5f ) );
    CHECK( restored.ProceduralSky.Month == 9 );

    // Switching backgrounds keeps the authored sky, including through save/load.
    restored.ClearType = Moonlight::ClearColorType::Color;
    json solid;
    restored.Serialize( solid );
    CHECK( solid["ProceduralSky"] == saved["ProceduralSky"] );
    Camera other;
    other.Deserialize( solid );
    other.ClearType = Moonlight::ClearColorType::Procedural;
    CHECK( other.GetSkySettings().Time == doctest::Approx( 6.25f ) );

    // Older scenes have no sky data and retain the original renderer defaults.
    Camera legacy;
    legacy.Deserialize( json{ { "ClearType", "Procedural" } } );
    CHECK( legacy.ProceduralSky.Time == doctest::Approx( 15.f ) );
    CHECK( legacy.ProceduralSky.TimeScale == doctest::Approx( 0.f ) );
    CHECK( legacy.ProceduralSky.Latitude == doctest::Approx( -6.f ) );
    CHECK( legacy.ProceduralSky.Turbidity == doctest::Approx( 1.9f ) );
    CHECK( legacy.ProceduralSky.Month == 5 );
}

TEST_CASE( "Camera: sky clocks are independent and do not change serialized settings" )
{
    Camera first;
    first.ClearType = Moonlight::ClearColorType::Procedural;
    first.ProceduralSky.Time = 23.5f;
    first.ProceduralSky.TimeScale = 1.f;
    Camera second;
    second.ClearType = Moonlight::ClearColorType::Procedural;
    second.ProceduralSky.Time = 6.f;
    second.ProceduralSky.TimeScale = 0.5f;
    json before;
    first.Serialize( before );
    CHECK( first.GetSkySettings( 2.f ).Time == doctest::Approx( 1.5f ) );
    CHECK( second.GetSkySettings( 2.f ).Time == doctest::Approx( 7.f ) );
    // Additional views read the same clock without advancing it.
    CHECK( first.GetSkySettings().Time == doctest::Approx( 1.5f ) );
    json after;
    first.Serialize( after );
    CHECK( after == before );
    first.ProceduralSky.TimeScale = 0.f;
    CHECK( first.GetSkySettings( 10.f ).Time == doctest::Approx( 1.5f ) );
    first.ProceduralSky.Time = 12.f;
    CHECK( first.GetSkySettings().Time == doctest::Approx( 12.f ) );
    // Undo/load restores the authored time and starts a fresh preview clock.
    first.Deserialize( before );
    CHECK( first.GetSkySettings().Time == doctest::Approx( 23.5f ) );
}

TEST_CASE( "Camera: procedural sky settings survive scene snapshots and clamp invalid values" )
{
    auto source = MakeWorld();
    EntityHandle entity = source->CreateEntity( "Sky Camera" );
    entity->AddComponent<Transform>();
    Camera& camera = entity->AddComponent<Camera>();
    camera.Deserialize( json{ { "ClearType", "Procedural" }, { "ProceduralSky", { { "Time", 9.f }, { "Latitude", 65.f }, { "Month", 11 } } } } );
    const json saved = SceneSerializer::SerializeEntities( *source, { entity.Get() } );
    auto destination = MakeWorld();
    const auto roots = SceneSerializer::Deserialize( *destination, saved, SceneSerializer::LoadOptions() );
    REQUIRE( roots.size() == 1 );
    Camera& restored = roots.front()->GetComponent<Camera>();
    CHECK( restored.ProceduralSky.Time == doctest::Approx( 9.f ) );
    CHECK( restored.ProceduralSky.Latitude == doctest::Approx( 65.f ) );
    CHECK( restored.ProceduralSky.Month == 11 );

    restored.Deserialize( json{ { "ProceduralSky", { { "Time", -4.f }, { "TimeScale", 8.f }, { "Latitude", 120.f }, { "Turbidity", -2.f }, { "Month", 20 },
        { "MoonPhase", -1.f }, { "CloudCoverage", 3.f }, { "CloudAltitude", 0.f }, { "WindDirection", 400.f } } } } );
    CHECK( restored.ProceduralSky.Time == doctest::Approx( 0.f ) );
    CHECK( restored.ProceduralSky.TimeScale == doctest::Approx( 1.f ) );
    CHECK( restored.ProceduralSky.Latitude == doctest::Approx( 90.f ) );
    CHECK( restored.ProceduralSky.Turbidity == doctest::Approx( 1.f ) );
    CHECK( restored.ProceduralSky.Month == 11 );
    CHECK( restored.ProceduralSky.MoonPhase == doctest::Approx( 0.f ) );
    CHECK( restored.ProceduralSky.CloudCoverage == doctest::Approx( 1.f ) );
    CHECK( restored.ProceduralSky.CloudAltitude == doctest::Approx( 100.f ) );
    CHECK( restored.ProceduralSky.WindDirection == doctest::Approx( 360.f ) );
}

TEST_CASE( "Procedural sky: the sun rises in the east, stands in the south at noon and sets in the west" )
{
    // 35 degrees north in June (+X east, +Y up, +Z north).
    Moonlight::ProceduralSkySettings settings;
    settings.Latitude = 35.f;
    settings.Month = 5;
    settings.Time = 12.f;
    const bx::Vec3 noon = Moonlight::ComputeSkyCelestial( settings ).Sun;
    CHECK( noon.y > 0.95f );    // ~78 degrees up
    CHECK( noon.z < 0.f );      // towards the south
    CHECK( std::abs( noon.x ) < 1e-3f );
    settings.Time = 7.f;
    const bx::Vec3 morning = Moonlight::ComputeSkyCelestial( settings ).Sun;
    CHECK( morning.x > 0.5f );
    CHECK( morning.y > 0.f );
    settings.Time = 17.f;
    const bx::Vec3 evening = Moonlight::ComputeSkyCelestial( settings ).Sun;
    CHECK( evening.x == doctest::Approx( -morning.x ).epsilon( 1e-3 ) );
    settings.Time = 0.f;
    CHECK( Moonlight::ComputeSkyCelestial( settings ).Sun.y < -0.3f );

    // South of the equator the noon sun is in the north.
    settings.Latitude = -35.f;
    settings.Time = 12.f;
    CHECK( Moonlight::ComputeSkyCelestial( settings ).Sun.z > 0.f );
}

TEST_CASE( "Procedural sky: the moon's place and light follow its phase" )
{
    Moonlight::ProceduralSkySettings settings;
    settings.MoonOrbitInclination = 0.f; // exact phase fractions for a coplanar orbit
    settings.Latitude = 35.f;
    settings.Time = 0.f;
    settings.MoonPhase = 0.5f;
    const Moonlight::SkyCelestial full = Moonlight::ComputeSkyCelestial( settings );
    CHECK( bx::dot( full.Sun, full.Moon ) < -0.99f );   // opposite the sun, up at midnight
    CHECK( full.Moon.y > 0.f );
    CHECK( full.MoonLitFraction == doctest::Approx( 1.f ).epsilon( 1e-3 ) );
    CHECK( full.MoonBrightness == doctest::Approx( 1.f ).epsilon( 1e-3 ) );

    settings.MoonPhase = 0.25f;   // first quarter: half lit, 90 degrees east of the sun
    const Moonlight::SkyCelestial quarter = Moonlight::ComputeSkyCelestial( settings );
    CHECK( std::abs( bx::dot( quarter.Sun, quarter.Moon ) ) < 0.02f );
    CHECK( quarter.MoonLitFraction == doctest::Approx( 0.5f ).epsilon( 0.02 ) );
    CHECK( quarter.MoonBrightness == doctest::Approx( 1.f / 3.14159265f ).epsilon( 0.02 ) );

    settings.MoonPhase = 0.f;
    CHECK( Moonlight::ComputeSkyCelestial( settings ).MoonBrightness < 1e-3f );
    CHECK( bx::length( full.MoonUp ) == doctest::Approx( 1.f ) );
    CHECK( std::abs( bx::dot( full.MoonUp, full.Moon ) ) < 1e-4f );
}

TEST_CASE( "Procedural sky: the atmosphere dims and reddens a low sun and the planet hides a set one" )
{
    const float sunRadius = 0.0047f;
    const bx::Vec3 overhead = Moonlight::ComputeSkyTransmittance( 0.01f, 1.f, 8.f, sunRadius );
    CHECK( overhead.y > 0.8f );
    CHECK( overhead.y < 1.f );
    CHECK( overhead.x > overhead.z );   // blue scatters most
    const bx::Vec3 low = Moonlight::ComputeSkyTransmittance( 0.01f, 0.05f, 8.f, sunRadius );
    CHECK( low.y < overhead.y * 0.5f );
    CHECK( low.x > low.z * 4.f );
    const bx::Vec3 set = Moonlight::ComputeSkyTransmittance( 0.01f, -0.05f, 8.f, sunRadius );
    CHECK( set.x == 0.f );
    // Up a mountain the light still reaches below the horizontal.
    CHECK( Moonlight::ComputeSkyTransmittance( 3.f, -0.02f, 8.f, sunRadius ).x > 0.f );
    // Haze dims the sun.
    CHECK( Moonlight::ComputeSkyTransmittance( 0.01f, 1.f, 60.f, sunRadius ).y < overhead.y );
}
