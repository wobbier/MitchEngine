#include <doctest/doctest.h>
#include "Graphics/ModelResource.h"
#include "Path.h"
#include "Components/Graphics/Model.h"
#include "Components/Transform.h"
#include "Components/Animation/Animator.h"
#include "Components/Animation/IKConstraints.h"
#include "Cores/AnimationCore.h"
#include "World/SceneSerializer.h"
#include "Engine/World.h"
#include "Graphics/ImportedMaterial.h"
#include "Graphics/ImportedMaterialMerge.h"
#include "Graphics/Material.h"
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

TEST_CASE( "Models: combined FBX animation keys are local to preserved helper chains" )
{
    const std::string path = ".tmp/Tests/Models/AnimatedPivot.fbx";
    std::filesystem::create_directories( ".tmp/Tests/Models" );
    aiScene scene;
    scene.mRootNode = new aiNode( "RootNode" );
    auto* helper = new aiNode( "Bone_$AssimpFbx$_PreRotation" );
    auto* bone = new aiNode( "Bone" );
    scene.mRootNode->addChildren( 1, &helper );
    helper->addChildren( 1, &bone );
    aiMatrix4x4::RotationZ( 0.5f, helper->mTransformation );
    helper->mTransformation.a4 = 2.f;
    helper->mTransformation.b4 = 3.f;
    scene.mNumAnimations = 1;
    scene.mAnimations = new aiAnimation*[1]{ new aiAnimation() };
    aiAnimation& animation = *scene.mAnimations[0];
    animation.mTicksPerSecond = 1.0;
    animation.mDuration = 1.0;
    animation.mNumChannels = 1;
    animation.mChannels = new aiNodeAnim*[1]{ new aiNodeAnim() };
    aiNodeAnim& channel = *animation.mChannels[0];
    channel.mNodeName = aiString( "Bone" );
    channel.mNumPositionKeys = 2;
    channel.mPositionKeys = new aiVectorKey[2]{ { 0.0, aiVector3D( 2.f, 3.f, 0.f ) },
        { 1.0, helper->mTransformation * aiVector3D( 1.f, 0.f, 0.f ) } };
    channel.mNumRotationKeys = 1;
    channel.mRotationKeys = new aiQuatKey[1]{ { 0.0, aiQuaternion( aiVector3D( 0.f, 0.f, 1.f ), 0.5f ) } };
    Assimp::Exporter exporter;
    REQUIRE( exporter.Export( &scene, "assbin", path + ".assbin" ) == AI_SUCCESS );
    ModelResource model{ Path( path ) };
    REQUIRE( model.LoadAsync() );
    REQUIRE( model.GetAnimations().size() == 1 );
    const auto& imported = model.GetAnimations()[0].Channels[0];
    Vector3 position, scale( 1.f );
    Quaternion rotation;
    Moonlight::AnimationClip::Sample( imported, 0.5f, position, rotation, scale );
    CHECK( position.x == doctest::Approx( 0.5f ) );
    CHECK( position.y == doctest::Approx( 0.f ).epsilon( 0.00001 ) );
    CHECK( std::abs( rotation.w ) == doctest::Approx( 1.f ) );
    CHECK( rotation.z == doctest::Approx( 0.f ).epsilon( 0.00001 ) );

    // Explicit helper channels already contain local keys and must be left alone.
    channel.mNodeName = helper->mName;
    REQUIRE( exporter.Export( &scene, "assbin", path + ".assbin" ) == AI_SUCCESS );
    ModelResource explicitHelper{ Path( path ) };
    REQUIRE( explicitHelper.LoadAsync() );
    CHECK( explicitHelper.GetAnimations()[0].Channels[0].Positions[0].x == doctest::Approx( 2.f ) );
}

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
    component.Deserialize( json{ { "ModelPath", "" } } ); // legacy expanded import
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
    single.Deserialize( json{ { "ModelPath", "" } } );
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
    component.Deserialize( json{ { "ModelPath", "" } } );
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
    other.Deserialize( json{ { "ModelPath", "" } } );
    other.ModelHandle = component.ModelHandle;
    other.Init();
    CHECK( editedFirst->GetComponent<Transform>().GetPosition().x == doctest::Approx( 100.f ) );
    CHECK( editedSecond->GetComponent<Transform>().GetPosition().y == doctest::Approx( 2.f ) );
}

TEST_CASE( "Models: compact instances retain pivots and expose only requested nodes" )
{
    World world;
    world.IsLoading = false;
    auto instance = AddNode( world, {}, "Instance", Vector3( 10.f, 0.f, 0.f ) );
    instance->GetComponent<Transform>().SetScale( 2.f );
    Model& model = instance->AddComponent<Model>();
    model.ModelHandle = EmptyModel();
    auto& root = model.ModelHandle->RootNode;
    root.Position = Vector3( 3.f, 0.f, 0.f );
    root.Scale = Vector3( 0.5f );
    root.Rotation = Quaternion::FromEulerDegrees( Vector3( 0.f, 0.f, 90.f ) );
    root.Nodes.resize( 3 );
    for( auto& child : root.Nodes ) { child.Name = "Head"; child.Position = Vector3( 4.f, 0.f, 0.f ); }
    root.Nodes[2].Name = "Head/#%";
    model.Init();
    CHECK( model.IsCompact() );
    CHECK( instance->GetComponent<Transform>().GetChildren().empty() );
    REQUIRE( model.GetNodes().size() == 4 );
    CHECK( model.GetNodes()[1].Path != model.GetNodes()[2].Path );
    CHECK( model.GetNodes()[3].Path.find( "%2F%23%25" ) != std::string::npos );
    CHECK( model.GetNodeWorld( 1 ).GetPosition().x == doctest::Approx( 16.f ) );
    CHECK( model.GetNodeWorld( 1 ).GetPosition().y == doctest::Approx( 4.f ) );
    auto exposed = model.ExposeNode( model.GetNodes()[1].Path );
    REQUIRE( exposed );
    CHECK( instance->GetComponent<Transform>().GetChildren().size() == 1 );
    CHECK( model.ExposeNode( model.GetNodes()[1].Path ) == exposed );
    const uint64_t guid = exposed->GetGUID();
    exposed->SetName( "Camera mount" );
    CHECK( model.ExposeNode( model.GetNodes()[1].Path )->GetGUID() == guid );
    auto camera = AddNode( world, exposed, "Camera", Vector3( 0.f, 1.f, 0.f ) );
    CHECK( camera->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 15.f ) );
    exposed->GetComponent<Transform>().Translate( Vector3( 2.f, 0.f, 0.f ) );
    Model::CaptureExposedEdits( world );
    Model::UpdateExposedNodes( world );
    CHECK( exposed->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 18.f ) );
    json attachment;
    exposed->GetComponent<ModelNode>().Serialize( attachment );
    CHECK( attachment["NodePath"] == model.GetNodes()[1].Path );
    CHECK( root.Nodes[0].Position.x == doctest::Approx( 4.f ) ); // resource is immutable
}

TEST_CASE( "Models: compact animation drives attachments and keeps instance poses independent" )
{
    AnimationCore core;
    World world;
    world.IsLoading = false;
    world.AddCore( core );
    auto resource = EmptyModel();
    resource->RootNode.Nodes.emplace_back();
    resource->RootNode.Nodes[0].Name = "Head";
    resource->RootNode.Nodes[0].Position = Vector3( 0.f, 5.f, 0.f );
    auto first = AddNode( world, {}, "First", Vector3( 10.f, 0.f, 0.f ) );
    auto second = AddNode( world, {}, "Second", Vector3() );
    for( auto entity : { first, second } )
    {
        auto& model = entity->AddComponent<Model>(); model.ModelHandle = resource; model.Init();
    }
    auto& model = first->GetComponent<Model>();
    auto head = model.ExposeNode( "Head" );
    auto camera = AddNode( world, head, "Camera", Vector3( 0.f, 1.f, 0.f ) );
    auto clips = std::make_shared<std::vector<Moonlight::AnimationClip>>();
    Moonlight::AnimationClip clip;
    clip.Name = "Move"; clip.Duration = 2.f;
    Moonlight::AnimationChannel channel; channel.NodeName = "Head";
    channel.PositionTimes = { 0.f, 2.f }; channel.Positions = { Vector3( 0.f, 5.f, 0.f ), Vector3( 4.f, 5.f, 0.f ) };
    clip.Channels.push_back( channel ); clips->push_back( clip );
    Animator& animator = first->AddComponent<Animator>(); animator.UseClips( clips );
    world.Simulate(); core.OnStart(); core.Advance( 0.5f );
    CHECK( camera->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 11.f ) );
    CHECK( camera->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 6.f ) );
    CHECK( second->GetComponent<Model>().GetNodeTransform( 1 )->GetPosition().x == doctest::Approx( 0.f ) );
    CHECK( resource->RootNode.Nodes[0].Position.x == doctest::Approx( 0.f ) );
    core.OnStop(); animator.InvalidateBindings(); animator.StartPreview( "Move" ); animator.PreviewPlaying = false; animator.PreviewTime = 1.f;
    core.UpdatePreviews( 0.f );
    CHECK( camera->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 12.f ) );
    core.StopAllPreviews();
    // Preview restores the pose it bound to, which was the last authored pose in this fixture.
    CHECK( camera->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 11.f ) );
    CHECK( model.GetNodes().size() == 2 );
    CHECK( first->GetComponent<Transform>().GetChildren().size() == 1 );
}

TEST_CASE( "Models: compaction keeps authored children and referenced entities with their GUIDs" )
{
    World world;
    world.IsLoading = false;
    auto instance = AddNode( world, {}, "Instance", Vector3( 10.f, 0.f, 0.f ) );
    auto& model = instance->AddComponent<Model>();
    model.Deserialize( json{ { "ModelPath", "" } } );
    model.ModelHandle = EmptyModel();
    auto& root = model.ModelHandle->RootNode;
    root.Nodes.resize( 3 );
    root.Nodes[0].Name = "Referenced"; root.Nodes[0].Position = Vector3( 1.f, 0.f, 0.f );
    root.Nodes[1].Name = "Authored"; root.Nodes[1].Position = Vector3( 0.f, 2.f, 0.f );
    root.Nodes[2].Name = "Import detail";
    model.Init();
    auto& transform = instance->GetComponent<Transform>();
    EntityHandle referenced = transform.GetChildByName( "Referenced" )->Parent;
    const uint64_t guid = referenced->GetGUID();
    auto custom = AddNode( world, transform.GetChildByName( "Authored" )->Parent, "My camera", Vector3( 0.f, 3.f, 0.f ) );
    auto controller = AddNode( world, {}, "Controller", Vector3() );
    controller->AddComponent<LookAtIK>().Bone = referenced;
    referenced->GetComponent<Transform>().SetPosition( Vector3( 3.f, 0.f, 0.f ) );
    REQUIRE( model.CompactHierarchy() ); world.Simulate();
    CHECK( referenced->GetGUID() == guid );
    CHECK( referenced->HasComponent<ModelNode>() );
    CHECK( controller->GetComponent<LookAtIK>().Bone == referenced );
    CHECK( custom->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 5.f ) );
    CHECK( transform.GetChildren().size() == 2 );
    CHECK( model.GetNodeWorld( model.FindNode( "Referenced" ) ).GetPosition().x == doctest::Approx( 13.f ) );
    json saved; model.Serialize( saved );
    CHECK( saved["Compact"].get<bool>() );
    CHECK( saved["Nodes"].size() == 3 );
    CHECK_FALSE( model.CompactHierarchy() );
}

TEST_CASE( "Models: compact attachments and inactive roots survive scene serialization" )
{
    World world;
    world.IsLoading = false;
    auto instance = AddNode( world, {}, "Instance", Vector3( 10.f, 0.f, 0.f ) );
    auto& model = instance->AddComponent<Model>();
    auto resource = EmptyModel();
    resource->RootNode.Nodes.emplace_back();
    resource->RootNode.Nodes[0].Name = "Head";
    resource->RootNode.Nodes[0].Position = Vector3( 0.f, 3.f, 0.f );
    model.ModelHandle = resource; model.Init();
    auto exposed = model.ExposeNode( "Head" );
    exposed->SetName( "Head camera" );
    exposed->GetComponent<Transform>().Translate( Vector3( 2.f, 0.f, 0.f ) );
    // Save immediately after editing, before the next animation phase captures the offset.
    const uint64_t guid = exposed->GetGUID();
    auto inactive = AddNode( world, {}, "Inactive model", Vector3() );
    inactive->SetActive( false ); world.Simulate();
    const json saved = SceneSerializer::SerializeWorld( world, nullptr );
    REQUIRE( saved["Entities"].size() == 3 );
    World restored;
    restored.IsLoading = false;
    SceneSerializer::LoadOptions options;
    SceneSerializer::Deserialize( restored, saved, options );
    auto instanceCopy = restored.FindEntityByGUID( instance->GetGUID() );
    auto& loaded = instanceCopy->GetComponent<Model>(); loaded.ModelHandle = resource; loaded.Init();
    Model::UpdateExposedNodes( restored );
    auto attachmentCopy = restored.FindEntityByGUID( guid );
    REQUIRE( attachmentCopy );
    CHECK( loaded.ExposeNode( "Head" )->GetGUID() == guid );
    CHECK( attachmentCopy->GetName() == "Head camera" );
    CHECK( attachmentCopy->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 12.f ) );
    CHECK( attachmentCopy->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 3.f ) );
    CHECK_FALSE( restored.FindEntityByGUID( inactive->GetGUID() )->IsActiveSelf() );
}

TEST_CASE( "Models: compact IK solves internal bones and publishes the solved attachment" )
{
    World world;
    world.IsLoading = false;
    auto instance = AddNode( world, {}, "Rig", Vector3( 10.f, 0.f, 0.f ) );
    auto& model = instance->AddComponent<Model>(); model.ModelHandle = EmptyModel();
    auto& root = model.ModelHandle->RootNode; root.Name = "Hip";
    root.Nodes.emplace_back(); auto& knee = root.Nodes[0]; knee.Name = "Knee"; knee.Position = Vector3( 1.f, 0.f, 0.f );
    knee.Nodes.emplace_back(); auto& foot = knee.Nodes[0]; foot.Name = "Foot"; foot.Position = Vector3( 1.f, 0.f, 0.f );
    model.Init(); auto tip = model.ExposeNode( "Foot" );
    auto target = AddNode( world, {}, "Target", Vector3( 11.f, 1.f, 0.f ) );
    auto& ik = instance->AddComponent<TwoBoneIK>(); ik.Tip = tip; ik.Target = target;
    world.Simulate(); AnimationCore::SolveIK( world );
    CHECK( ik.IsReached() );
    CHECK( tip->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 11.f ) );
    CHECK( tip->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 1.f ) );
    CHECK( instance->GetComponent<Transform>().GetChildren().size() == 1 );
}

TEST_CASE( "Models: Max Physical materials retain PBR maps and use mapped values instead of scalar placeholders" )
{
    aiMaterial material;
    const aiString physical( "PHYSICAL_MTL" ), base( "textures/curtain_red_BaseColor.png" ), rough( "textures/curtain_Roughness.png" ), metal( "textures/curtain_Metalness.png" );
    const aiColor3D tint( 0.18f, 0.18f, 0.18f );
    const float invalid = -2.2f, zero = 0.f;
    material.AddProperty( &physical, "$raw.3dsMax|ORIGINAL_MTL" );
    material.AddProperty( &tint, 1, AI_MATKEY_COLOR_DIFFUSE );
    material.AddProperty( &invalid, 1, AI_MATKEY_ROUGHNESS_FACTOR );
    material.AddProperty( &zero, 1, "$raw.3dsMax|Parameters|roughness" );
    material.AddProperty( &zero, 1, "$raw.3dsMax|Parameters|metalness" );
    material.AddProperty( &zero, 1, "$raw.babylonBackfaceCulling" );
    material.AddProperty( &base, "$raw.3dsMax|Parameters|base_color_map|file", aiTextureType_UNKNOWN );
    material.AddProperty( &rough, "$raw.3dsMax|Parameters|roughness_map|file", aiTextureType_UNKNOWN );
    material.AddProperty( &metal, "$raw.3dsMax|Parameters|metalness_map|file", aiTextureType_UNKNOWN );
    const auto imported = Moonlight::ReadImportedMaterial( material );
    CHECK( imported.Color.x == doctest::Approx( 1.f ) );
    CHECK( imported.Roughness == doctest::Approx( 1.f ) );
    CHECK( imported.Metallic == doctest::Approx( 1.f ) );
    CHECK( imported.Textures[Moonlight::TextureType::Roughness] == rough.C_Str() );
    CHECK( imported.Textures[Moonlight::TextureType::Normal] == "textures/curtain_Normal.png" );
    CHECK( imported.DoubleSided );

    const aiString mask( "textures/dirt_mask.png" );
    const float transparency = 0.65f;
    material.AddProperty( &mask, "$raw.3dsMax|Parameters|cutout_map|file", aiTextureType_UNKNOWN );
    material.AddProperty( &transparency, 1, "$raw.3dsMax|Parameters|transparency" );
    const auto decal = Moonlight::ReadImportedMaterial( material );
    CHECK( decal.Transparent );
    CHECK( decal.Opacity == doctest::Approx( 0.35f ) );
    CHECK( decal.AlphaCutoff == doctest::Approx( 0.f ) );
    CHECK( decal.Textures[Moonlight::TextureType::Opacity] == mask.C_Str() );
    material.AddProperty( &zero, 1, "$raw.3dsMax|Parameters|transparency" );
    const auto cutout = Moonlight::ReadImportedMaterial( material );
    CHECK_FALSE( cutout.Transparent );
    CHECK( cutout.AlphaCutoff == doctest::Approx( 0.5f ) );
}

TEST_CASE( "Models: saved material defaults migrate while user edits and removed texture slots survive" )
{
    json previous = { { "Roughness", -2.2f }, { "DiffuseColor", { 0.18f, 0.18f, 0.18f } }, { "Opacity", 1.f },
        { "Textures", { { "Diffuse", { { "Path", "red.png" } } } } } };
    json imported = { { "Roughness", 1.f }, { "DiffuseColor", { 1.f, 1.f, 1.f } }, { "Opacity", 0.35f },
        { "Textures", { { "Diffuse", { { "Path", "red.png" } } }, { "Normal", { { "Path", "normal.png" } } } } } };
    json edited = previous;
    edited["Opacity"] = 0.7f;
    const json merged = Moonlight::MergeImportedMaterial( edited, previous, imported );
    CHECK( merged["Roughness"].get<float>() == doctest::Approx( 1.f ) );
    CHECK( merged["Opacity"].get<float>() == doctest::Approx( 0.7f ) );
    CHECK( merged["DiffuseColor"] == imported["DiffuseColor"] );
    CHECK( merged["Textures"]["Normal"]["Path"] == "normal.png" );
    CHECK( Moonlight::MergeImportedMaterial( merged, imported, imported ) == merged );
    json removed = imported;
    removed["Textures"].erase( "Normal" );
    CHECK_FALSE( Moonlight::MergeImportedMaterial( removed, imported, imported )["Textures"].contains( "Normal" ) );
}

TEST_CASE( "Models: glTF alpha factors are not applied twice and cutout maps stay in the opaque pass" )
{
    aiMaterial source;
    const aiColor4D base( 1.f, 1.f, 1.f, 0.35f );
    const float opacity = 0.35f;
    const aiString mask( "MASK" );
    source.AddProperty( &base, 1, AI_MATKEY_BASE_COLOR );
    source.AddProperty( &opacity, 1, AI_MATKEY_OPACITY );
    source.AddProperty( &mask, "$mat.gltf.alphaMode" );
    const auto imported = Moonlight::ReadImportedMaterial( source );
    CHECK( imported.Opacity == doctest::Approx( 0.35f ) );
    CHECK_FALSE( imported.Transparent );
    CHECK( imported.AlphaCutoff == doctest::Approx( 0.5f ) );

    struct AlphaMaterial : Moonlight::Material
    {
        AlphaMaterial() : Material( "AlphaTest", "" ) {}
        void Init() override {}
        void Use() override {}
        SharedPtr<Moonlight::Material> CreateInstance() override { return nullptr; }
        float GetAlphaCutoff() const override { return Cutoff; }
        float Cutoff = 0.f;
    };
    AlphaMaterial material;
    material.SetTexture( Moonlight::TextureType::Opacity, MakeShared<Moonlight::Texture>( Path( "" ) ) );
    CHECK( material.IsTransparent() );
    material.Cutoff = 0.5f;
    CHECK_FALSE( material.IsTransparent() );
    material.RenderMode = Moonlight::RenderingMode::Transparent;
    CHECK( material.IsTransparent() );
}

#endif
