#include <doctest/doctest.h>
#include "Graphics/ModelResource.h"
#include "Path.h"
#include "Components/Graphics/Model.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <filesystem>
#include <limits>
#include <set>

#if USING( ME_TOOLS )

namespace ModelImportTest
{
    struct Extents
    {
        aiVector3D Min{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        aiVector3D Max{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };

        float Height() const
        {
            return Max.y - Min.y;
        }
    };

    void Accumulate( const aiScene& InScene, const aiNode& InNode, const aiMatrix4x4& InParent, Extents& OutExtents )
    {
        const aiMatrix4x4 world = InParent * InNode.mTransformation;
        for( unsigned int m = 0; m < InNode.mNumMeshes; ++m )
        {
            const aiMesh& mesh = *InScene.mMeshes[InNode.mMeshes[m]];
            for( unsigned int v = 0; v < mesh.mNumVertices; ++v )
            {
                const aiVector3D p = world * mesh.mVertices[v];
                OutExtents.Min = aiVector3D( std::min( OutExtents.Min.x, p.x ), std::min( OutExtents.Min.y, p.y ), std::min( OutExtents.Min.z, p.z ) );
                OutExtents.Max = aiVector3D( std::max( OutExtents.Max.x, p.x ), std::max( OutExtents.Max.y, p.y ), std::max( OutExtents.Max.z, p.z ) );
            }
        }
        for( unsigned int c = 0; c < InNode.mNumChildren; ++c )
        {
            Accumulate( InScene, *InNode.mChildren[c], world, OutExtents );
        }
    }

    // The world-space extents of the cooked model at InPath.
    Extents CookedExtents( const std::string& InPath )
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile( InPath + ".assbin", 0 );
        Extents extents;
        if( scene && scene->mRootNode )
        {
            Accumulate( *scene, *scene->mRootNode, aiMatrix4x4(), extents );
        }
        return extents;
    }

    SharedPtr<ModelResource> EmptyModel( float InScale = 0.f )
    {
        // No GPU objects are needed to test expansion of the imported transform hierarchy.
        const std::string path = ".tmp/Tests/ModelHierarchy/Empty.fbx";
        std::filesystem::create_directories( ".tmp/Tests/ModelHierarchy" );
        aiScene scene;
        scene.mRootNode = new aiNode( "RootNode" );
        if( InScale > 0.f )
        {
            scene.mRootNode->mMetaData = aiMetadata::Alloc( 1 );
            scene.mRootNode->mMetaData->Set( 0, "MitchEngineImportScale", InScale );
        }
        Assimp::Exporter exporter;
        REQUIRE( exporter.Export( &scene, "assbin", path + ".assbin" ) == AI_SUCCESS );
        auto model = MakeShared<ModelResource>( Path( path ) );
        REQUIRE( model->LoadAsync() );
        REQUIRE( model->FinishAsyncLoad() );
        model->RootNode.Name = "RootNode";
        return model;
    }

    EntityHandle AddNode( World& InWorld, EntityHandle InParent, const std::string& InName, const Vector3& InPosition )
    {
        EntityHandle entity = InWorld.CreateEntity( InName );
        Transform& transform = entity->AddComponent<Transform>();
        transform.SetPosition( InPosition );
        if( InParent ) transform.SetParent( InParent->GetComponent<Transform>() );
        return entity;
    }
}

using namespace ModelImportTest;

TEST_CASE( "Models: import scale and unit conversion are baked into the cooked model" )
{
    namespace fs = std::filesystem;
    const std::string source = "Assets/Models/Synty/ExplorerKit/FBX/SM_Prop_Torch_01.fbx";
    if( !fs::exists( source ) )
    {
        MESSAGE( "skipped: the Synty sample isn't in this checkout" );
        return;
    }
    fs::create_directories( ".tmp/Tests/Models" );
    const std::string path = ".tmp/Tests/Models/Torch.fbx";
    fs::copy_file( source, path, fs::copy_options::overwrite_existing );

    ModelResourceMetadata meta{ Path( path ) };
    CHECK( meta.ConvertUnits );
    meta.ConvertUnits = false;
    meta.Export();
    const Extents raw = CookedExtents( path );
    REQUIRE( raw.Height() > 10.f );     // authored in centimetres: about a hundred units tall

    meta.ConvertUnits = true;
    meta.Export();
    const Extents metres = CookedExtents( path );
    CHECK( metres.Height() == doctest::Approx( raw.Height() * 0.01f ).epsilon( 0.001 ) );
    CHECK( metres.Min.x == doctest::Approx( raw.Min.x * 0.01f ).epsilon( 0.001 ) );
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile( path + ".assbin", 0 );
        REQUIRE( scene );
        REQUIRE( scene->mRootNode->mMetaData );
        float scale = 0.f;
        REQUIRE( scene->mRootNode->mMetaData->Get( "MitchEngineImportScale", scale ) );
        CHECK( scale == doctest::Approx( 0.01f ) );
    }

    meta.ImportScale = 2.f;
    meta.Export();
    CHECK( CookedExtents( path ).Height() == doctest::Approx( raw.Height() * 0.02f ).epsilon( 0.001 ) );

    // Conversion is always recorded so an explicit opt-out survives the new default.
    json saved;
    meta.OnSerialize( saved );
    ModelResourceMetadata loaded{ Path( path ) };
    loaded.OnDeserialize( saved );
    CHECK( loaded.ConvertUnits );
    CHECK( loaded.ImportScale == doctest::Approx( 2.f ) );
    json defaults;
    ModelResourceMetadata{ Path( path ) }.OnSerialize( defaults );
    CHECK_FALSE( defaults.contains( "ImportScale" ) );
    CHECK( defaults["ConvertUnits"].get<bool>() );
    CHECK( meta.GetBakedScale( 0.0 ) == doctest::Approx( 2.f ) );   // no units in the file: metres
    fs::remove_all( ".tmp/Tests/Models" );
}

TEST_CASE( "Models: metres are the default and legacy cooks are invalidated once" )
{
    ModelResourceMetadata meta{ Path( "Engine/Tests/Source/ModelImportTests.cpp" ) };
    CHECK( meta.GetBakedScale( 1.0 ) == doctest::Approx( 0.01f ) );  // centimetres
    CHECK( meta.GetBakedScale( 100.0 ) == doctest::Approx( 1.f ) );  // metres
    CHECK( meta.GetBakedScale( 0.1 ) == doctest::Approx( 0.001f ) ); // millimetres
    CHECK( meta.GetBakedScale( 0.0 ) == doctest::Approx( 1.f ) );    // undeclared units

    meta.OnDeserialize( json::object() );
    CHECK( meta.ConvertUnits );
    CHECK( meta.FlaggedForExport );
    json migrated;
    meta.OnSerialize( migrated );
    ModelResourceMetadata reopened{ meta.FilePath };
    reopened.OnDeserialize( migrated );
    CHECK( reopened.ConvertUnits );
    CHECK_FALSE( reopened.FlaggedForExport );

    meta.OnDeserialize( json{ { "ConvertUnits", false }, { "ImportScale", 2.f } } );
    CHECK_FALSE( meta.ConvertUnits );
    CHECK( meta.GetBakedScale( 1.0 ) == doctest::Approx( 2.f ) );
    json optedOut;
    meta.OnSerialize( optedOut );
    reopened.OnDeserialize( optedOut );
    CHECK_FALSE( reopened.ConvertUnits );
    CHECK_FALSE( reopened.FlaggedForExport );
}

TEST_CASE( "Models: material splits have distinct stable names, including empty and duplicate names" )
{
    Moonlight::Node node;
    node.Name = "Body";
    Moonlight::MeshData meshes[6];
    for( auto& mesh : meshes ) node.Meshes.push_back( &mesh );
    meshes[2].Name = meshes[3].Name = "Armour";
    meshes[4].Name = "Body_Mesh_0"; // collides with an otherwise generated name
    meshes[5].Name = "Sword";
    node.Nodes.emplace_back();
    node.Nodes.back().Name = "Body_Mesh_1";

    std::set<std::string> names;
    for( size_t i = 0; i < node.Meshes.size(); ++i )
    {
        const std::string name = node.GetMeshEntityName( i );
        CHECK_FALSE( name.empty() );
        CHECK( name != node.Nodes[0].Name );
        CHECK( names.insert( name ).second );
        CHECK( name == node.GetMeshEntityName( i ) );
    }
    CHECK( node.GetMeshEntityName( 5 ) == "Sword" );
}

TEST_CASE( "Models: expansion keeps the imported root and sole child's transforms" )
{
    World world;
    world.IsLoading = false;
    EntityHandle instance = AddNode( world, {}, "Instance", Vector3( 10.f, 0.f, 0.f ) );
    instance->GetComponent<Transform>().SetScale( 2.f );
    Model& component = instance->AddComponent<Model>();
    component.ModelHandle = EmptyModel();
    auto& root = component.ModelHandle->RootNode;
    root.Position = Vector3( 3.f, 0.f, 0.f );
    root.Scale = Vector3( 0.5f, 0.5f, 0.5f );
    root.Rotation = Quaternion::FromEulerDegrees( Vector3( 0.f, 0.f, 90.f ) );
    root.Nodes.emplace_back();
    auto& child = root.Nodes.back();
    child.Name = "Geometry";
    child.Position = Vector3( 4.f, 0.f, 0.f );
    child.Scale = Vector3( 3.f, 3.f, 3.f );
    component.Init();

    REQUIRE( component.IsReady() );
    Transform* importedRoot = instance->GetComponent<Transform>().GetChildByName( "RootNode" );
    REQUIRE( importedRoot );
    Transform* geometry = importedRoot->GetChildByName( "Geometry" );
    REQUIRE( geometry );
    CHECK( geometry->GetWorldPosition().x == doctest::Approx( 16.f ) );
    CHECK( geometry->GetWorldPosition().y == doctest::Approx( 4.f ) );
    CHECK( geometry->GetWorldScale().x == doctest::Approx( 3.f ) );
    CHECK( instance->GetComponent<Transform>().GetPosition().x == doctest::Approx( 10.f ) );

    EntityHandle other = AddNode( world, {}, "IdentityRootInstance", Vector3() );
    Model& single = other->AddComponent<Model>();
    single.ModelHandle = EmptyModel();
    single.ModelHandle->RootNode.Nodes.push_back( child );
    single.Init();
    Transform* soleChild = other->GetComponent<Transform>().GetChildByName( "Geometry" );
    REQUIRE( soleChild );
    CHECK( soleChild->GetWorldPosition().x == doctest::Approx( 4.f ) );
    CHECK( soleChild->GetWorldScale().x == doctest::Approx( 3.f ) );
}

TEST_CASE( "Models: saved import scales keep geometry and edited node translations in the same units" )
{
    World world;
    world.IsLoading = false;
    EntityHandle instance = AddNode( world, {}, "Instance", Vector3( 30.f, 0.f, 0.f ) );
    EntityHandle first = AddNode( world, instance, "First", Vector3( 51.f, 0.f, 0.f ) ); // includes a local edit
    EntityHandle second = AddNode( world, first, "Second", Vector3( 0.f, 20.f, 0.f ) );
    Model& component = instance->AddComponent<Model>();
    component.Deserialize( json{ { "ModelPath", "" }, { "BakedImportScale", 100.f } } );
    component.ModelHandle = EmptyModel( 1.f );
    auto& root = component.ModelHandle->RootNode;
    root.Nodes.resize( 2 ); // keep the synthetic root layout uncollapsed
    root.Nodes[0].Name = "First";
    root.Nodes[0].Position = Vector3( 0.5f, 0.f, 0.f );
    root.Nodes[0].Nodes.emplace_back();
    root.Nodes[0].Nodes[0].Name = "Second";
    root.Nodes[0].Nodes[0].Position = Vector3( 0.f, 0.2f, 0.f );
    root.Nodes[1].Name = "Other";
    component.Init();

    CHECK( first->GetComponent<Transform>().GetPosition().x == doctest::Approx( 0.51f ) );
    CHECK( second->GetComponent<Transform>().GetPosition().y == doctest::Approx( 0.2f ) );
    CHECK( instance->GetComponent<Transform>().GetPosition().x == doctest::Approx( 30.f ) );
    json saved;
    component.Serialize( saved );
    CHECK( saved["BakedImportScale"].get<float>() == doctest::Approx( 1.f ) );
    component.Init();
    CHECK( first->GetComponent<Transform>().GetPosition().x == doctest::Approx( 0.51f ) );
}

TEST_CASE( "Models: legacy scenes migrate consistent unit mismatches and keep isolated edits" )
{
    World world;
    world.IsLoading = false;
    EntityHandle instance = AddNode( world, {}, "Instance", Vector3( 10.f, 0.f, 0.f ) );
    auto first = AddNode( world, instance, "First", Vector3( 100.f, 0.f, 0.f ) );
    auto second = AddNode( world, instance, "Second", Vector3( 0.f, 200.f, 0.f ) );
    Model& component = instance->AddComponent<Model>();
    component.ModelHandle = EmptyModel();
    auto& root = component.ModelHandle->RootNode;
    root.Nodes.resize( 2 );
    root.Nodes[0].Name = "First";
    root.Nodes[0].Position = Vector3( 1.f, 0.f, 0.f );
    root.Nodes[1].Name = "Second";
    root.Nodes[1].Position = Vector3( 0.f, 2.f, 0.f );
    component.Init();
    CHECK( first->GetComponent<Transform>().GetPosition().x == doctest::Approx( 1.f ) );
    CHECK( second->GetComponent<Transform>().GetPosition().y == doctest::Approx( 2.f ) );
    json saved;
    component.Serialize( saved );
    CHECK_FALSE( saved.contains( "BakedImportScale" ) ); // legacy cook scale remains unknown

    EntityHandle edited = AddNode( world, {}, "EditedInstance", Vector3() );
    auto editedFirst = AddNode( world, edited, "First", Vector3( 100.f, 0.f, 0.f ) );
    auto editedSecond = AddNode( world, edited, "Second", Vector3( 0.f, 2.f, 0.f ) );
    Model& other = edited->AddComponent<Model>();
    other.ModelHandle = component.ModelHandle;
    other.Init();
    CHECK( editedFirst->GetComponent<Transform>().GetPosition().x == doctest::Approx( 100.f ) );
    CHECK( editedSecond->GetComponent<Transform>().GetPosition().y == doctest::Approx( 2.f ) );
}

#endif
