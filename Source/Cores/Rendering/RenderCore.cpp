#include "PCH.h"
#include "RenderCore.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Graphics/Model.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "CLog.h"
#include "Graphics/ShaderCommand.h"
#include "Resource/Resource.h"

#include "Components/Camera.h"
#include <iostream>
#include "Components/Physics/Rigidbody.h"
#include "Resource/ResourceCache.h"
#include "Graphics/ModelResource.h"
#include "RenderCommands.h"
#include "Components/Lighting/Light.h"
#include "Components/Graphics/Decal.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Graphics/Terrain.h"
#include "Materials/StandardMaterial.h"
#include "Debug/DebugDraw.h"
#include "Utils/BGFXUtils.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Engine/Engine.h"
#include "Renderer.h"
#include "Camera/CameraData.h"
#include "RenderPasses/PickingPass.h"
#include "Utils/ImGuiUtils.h"
#include "Core/CommandLine.h"
#include "Profiling/FrameStats.h"
#include <limits>
#include <unordered_set>


struct RenderCore::TerrainDraw
{
    std::shared_ptr<const TerrainData::ResidentTerrain> Source;
    std::vector<std::unique_ptr<Moonlight::MeshData>> Geometry;
    std::vector<std::unique_ptr<Mesh>> Parts;
    SharedPtr<StandardMaterial> Surface;
    std::vector<TerrainData::PatchLodChoice> Choices;
    uint16_t RetryFrames = 0;
    std::string RetryError;
};

void RenderCore::RemoveTerrainDraw( uint64_t InEntityID )
{
    auto found = m_terrainDraws.find( InEntityID );
    if( found == m_terrainDraws.end() ) return;
    // Commands contain raw geometry pointers: retire them before destroying their buffers.
    if( Moonlight::IsGpuAlive() )
        for( auto& mesh : found->second->Parts ) GetEngine().GetRenderer().GetMeshCache().Pop( mesh->GetId() );
    m_terrainDraws.erase( found );
}

void RenderCore::ClearTerrainDraws()
{
    while( !m_terrainDraws.empty() ) RemoveTerrainDraw( m_terrainDraws.begin()->first );
    m_terrainTopology.clear();
}

void RenderCore::OnRemovedFromWorld() { ClearTerrainDraws(); }

RenderCore::TerrainDraw* RenderCore::PrepareTerrainDraw( Entity& InEntity, Terrain& InTerrain, std::span<const TerrainData::LodView> views )
{
    auto data = InTerrain.GetData();
    const uint64_t id = InEntity.GetId().Value();
    auto previous = m_terrainDraws.find( id );
    if( previous != m_terrainDraws.end() && ( previous->second->Source->AssetGUID != data->AssetGUID || previous->second->Parts.size() != data->Patches.size() ) ) RemoveTerrainDraw( id );
    auto& draw = m_terrainDraws[id];
    if( !draw )
    {
        draw = std::make_unique<TerrainDraw>();
        draw->Surface = MakeShared<StandardMaterial>();
        draw->Surface->Init();
        draw->Geometry.resize( data->Patches.size() );
        draw->Parts.resize( data->Patches.size() );
    }
    if( draw->Source != data )
    {
        OPTICK_EVENT( "RenderCore::UpdateTerrainPatches" );
        for( size_t i = 0; i < data->Patches.size(); ++i )
        {
            const auto& patch = data->Patches[i];
            if( draw->Source && draw->Source->Patches[i] == patch ) continue;
            // Retire the old command before releasing its raw geometry pointer and GPU buffers.
            if( draw->Parts[i] ) GetEngine().GetRenderer().GetMeshCache().Pop( draw->Parts[i]->GetId() );
            draw->Parts[i].reset(); draw->Geometry[i].reset();
            std::vector<Moonlight::PosNormTexTanBiVertex> vertices;
            vertices.reserve( patch->Mesh.Vertices.size() );
            for( const auto& v : patch->Mesh.Vertices )
            {
                Moonlight::PosNormTexTanBiVertex vertex;
                vertex.Position = v.Position; vertex.Normal = v.Normal;
                vertex.TextureCoord = Vector2( v.U, v.V );
                const auto n = v.Normal.InternalVector;
                auto tangent = glm::vec3( 1.f, 0.f, 0.f ) - n * n.x;
                if( glm::dot( tangent, tangent ) < 1e-8f ) tangent = glm::vec3( 0.f, 0.f, 1.f );
                tangent = glm::normalize( tangent );
                vertex.Tangent = Vector3( tangent ); vertex.BiTangent = Vector3( glm::normalize( glm::cross( tangent, n ) ) );
                vertices.push_back( vertex );
            }
            auto geometry = std::make_unique<Moonlight::MeshData>( std::move( vertices ), patch->Mesh.Lods.front().Indices, draw->Surface );
            auto mesh = std::make_unique<Mesh>( geometry.get() );
            mesh->Id = GetEngine().GetRenderer().GetMeshCache().Push( Moonlight::MeshCommand() );
            draw->Geometry[i] = std::move( geometry );
            draw->Parts[i] = std::move( mesh );
        }
        draw->Source = data;
    }
    const auto translation = InEntity.GetComponent<Transform>().GetWorldPosition();
    TerrainData::LodSettings settings{ InTerrain.LodPixelError, InTerrain.LodHysteresis, InTerrain.ForcedLod };
    std::vector<TerrainData::PatchLodChoice> choices;
    std::string error;
    bool ready = TerrainData::SelectTerrainLods( *data, translation, views, settings, draw->Choices, choices, error );
    if( ready && draw->RetryFrames ) { --draw->RetryFrames; ready = false; error = draw->RetryError; }
    std::vector<std::shared_ptr<Moonlight::MeshIndexData>> topologies( data->Patches.size() );
    const uint32_t q = data->Heights->GetSettings().PatchQuads;
    if( ready ) for( size_t i = 0; i < choices.size(); ++i )
    {
        const auto choice = choices[i];
        if( !choice.Level && !choice.Edges ) continue; // Existing full-detail buffer.
        const uint64_t key = ( static_cast<uint64_t>( q ) << 16 ) | ( static_cast<uint64_t>( choice.Level ) << 8 ) | choice.Edges;
        auto found = m_terrainTopology.find( key );
        if( found == m_terrainTopology.end() )
        {
            std::vector<uint32_t> indices;
            if( !TerrainData::BuildStitchedIndices( q, 1u << choice.Level, choice.Edges, indices, error ) ) { ready = false; break; }
            auto topology = std::make_shared<Moonlight::MeshIndexData>( std::move( indices ) );
            if( !bgfx::isValid( topology->GetBuffer() ) )
            {
                error = "Terrain topology upload failed; drawing full detail"; ready = false;
                draw->RetryFrames = 120; draw->RetryError = error; break;
            }
            found = m_terrainTopology.emplace( key, std::move( topology ) ).first;
        }
        topologies[i] = found->second;
    }
    if( !ready )
    {
        choices.assign( data->Patches.size(), {} );
        for( auto& topology : topologies ) topology.reset(); // Uniform fallback is seam safe.
    }
    InTerrain.m_lodError = error;
    draw->Choices = std::move( choices ); InTerrain.m_patchLods = draw->Choices;
    InTerrain.m_lodTriangles = InTerrain.m_fullTriangles = 0;
    static const Vector3 colors[] = { { 0.2f, 0.85f, 0.3f }, { 0.95f, 0.8f, 0.15f }, { 1.f, 0.4f, 0.15f }, { 0.2f, 0.65f, 1.f }, { 0.65f, 0.3f, 1.f }, { 0.95f, 0.3f, 0.7f } };
    for( size_t i = 0; i < draw->Parts.size(); ++i )
    {
        auto& part = draw->Parts[i];
        draw->Geometry[i]->SetIndexTopology( std::move( topologies[i] ) );
        InTerrain.m_lodTriangles += draw->Geometry[i]->GetIndexCount() / 3;
        InTerrain.m_fullTriangles += data->Patches[i]->Mesh.Lods.front().Indices.size() / 3;
        // Materials prepare themselves in parallel jobs, so each patch keeps its own instance.
        auto& material = static_cast<StandardMaterial&>( *part->MeshMaterial );
        material.DiffuseColor = InTerrain.ShowLods ? colors[draw->Choices[i].Level % 6] : InTerrain.Color;
        material.Roughness = std::clamp( InTerrain.Roughness, 0.f, 1.f );
        part->CastShadows = InTerrain.CastShadows;
        if( InTerrain.ShowPatches )
        {
            AABB bounds = data->Patches[i]->Mesh.Bounds;
            bounds.Min += data->Patches[i]->Offset + translation; bounds.Max += data->Patches[i]->Offset + translation;
            const auto color = colors[draw->Choices[i].Level % 6];
            DebugDraw::Box( bounds, Vector4( color.x, color.y, color.z, 1.f ), 0, DebugDraw::EditorOnly );
        }
    }
    return draw.get();
}

RenderCore::RenderCore()
    : Base( ComponentFilter().Requires<Transform>().RequiresOneOf<Mesh>().RequiresOneOf<Model>().RequiresOneOf<Terrain>() )
{
    SetIsSerializable( false );
    //m_renderer = &GetEngine().GetRenderer();
    //m_renderer->RegisterDeviceNotify(this);
}

void RenderCore::Init()
{
    CLog::GetInstance().Log( CLog::LogType::Debug, "RenderCore Initialized..." );
    LodBias = CommandLine::GetFloat( "--lod-bias", LodBias );
    //m_renderer->ClearDebugColliders();
    ClearTerrainDraws();
    GetEngine().GetRenderer().ClearMeshes();
}

void RenderCore::OnEntityAdded( Entity& NewEntity )
{
    if( NewEntity.HasComponent<Mesh>() )
    {
        NewEntity.GetComponent<Mesh>().Id = GetEngine().GetRenderer().GetMeshCache().Push( Moonlight::MeshCommand() );
        m_meshCommands[NewEntity.GetId().Value()] = NewEntity.GetComponent<Mesh>().Id;
    }
}

void RenderCore::OnEntityRemoved( Entity& InEntity )
{
    RemoveTerrainDraw( InEntity.GetId().Value() );
    auto parts = m_compactCommands.find( InEntity.GetId().Value() );
    if( parts != m_compactCommands.end() )
    {
        for( unsigned int id : parts->second ) GetEngine().GetRenderer().GetMeshCache().Pop( id );
        m_compactCommands.erase( parts );
        if( Model* model = InEntity.TryGetComponent<Model>() )
            for( auto& part : model->GetRenderParts() ) part.Renderer->Id = std::numeric_limits<unsigned int>::max();
    }
    auto direct = m_meshCommands.find( InEntity.GetId().Value() );
    if( direct != m_meshCommands.end() )
    {
        GetEngine().GetRenderer().GetMeshCache().Pop( direct->second );
        m_meshCommands.erase( direct );
        if( Mesh* mesh = InEntity.TryGetComponent<Mesh>() ) mesh->Id = std::numeric_limits<unsigned int>::max();
    }
}

RenderCore::~RenderCore()
{
    ClearTerrainDraws();
    CLog::GetInstance().Log( CLog::LogType::Debug, "RenderCore Destroyed..." );
}

void RenderCore::Update( const UpdateContext& inUpdateContext )
{
    OPTICK_CATEGORY( "RenderCore::Update", Optick::Category::Rendering );
    auto& Renderables = GetEntities();
    Engine& engine = *inUpdateContext.GetSystem<Engine>();
    BGFXRenderer& renderer = engine.GetRenderer();

    // maybe this is fine?
    // (a few minutes later) it's not...
    renderer.m_time.x = inUpdateContext.GetDeltaTime();
    renderer.m_time.y = inUpdateContext.GetTotalTime();

    // Resolve dirty world matrices first so the light gather and mesh jobs below only read them.
    Transform::UpdateAll( GetWorld() );
    struct RenderItem
    {
        Mesh* Instance;
        Matrix4 World;
        EntityHandle Owner;
    };
    std::vector<RenderItem> items;
    std::unordered_set<uint64_t> compactModels;
    std::unordered_set<uint64_t> terrains;
    // Terrain honors every rendering camera plus World View, using the current late-update snapshot.
    std::vector<TerrainData::LodView> terrainViews;
    const auto addTerrainView = [&]( const Moonlight::CameraData& camera ) {
        if( !camera.ShouldRender || camera.OutputSize.x <= 0 || camera.OutputSize.y <= 0 ) return;
        const auto& projection = camera.ProjectionMatrix.GetInternalMatrix();
        TerrainData::LodView view;
        view.Position = camera.Position; view.Forward = camera.Front; view.Up = camera.Up;
        view.PixelsX = std::abs( projection[0][0] ) * camera.OutputSize.x * 0.5;
        view.PixelsY = std::abs( projection[1][1] ) * camera.OutputSize.y * 0.5;
        view.Near = camera.Near; view.Orthographic = camera.Projection == Moonlight::ProjectionType::Orthographic; view.ForceFine = camera.IsOblique;
        terrainViews.push_back( view );
    };
    for( const auto& camera : renderer.GetCameraCache().Commands ) addTerrainView( camera );
#if USING( ME_EDITOR )
    addTerrainView( engine.EditorCamera );
#endif
    for( Entity& entity : Renderables )
    {
        if( auto* terrain = entity.TryGetComponent<Terrain>(); terrain && terrain->IsEnabled() && entity.IsActiveInHierarchy() && terrain->GetData() )
        {
            terrains.insert( entity.GetId().Value() );
            TerrainDraw* draw = PrepareTerrainDraw( entity, *terrain, terrainViews );
            const auto& world = entity.GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix();
            for( size_t i = 0; i < draw->Parts.size(); ++i )
                items.push_back( { draw->Parts[i].get(), Matrix4( glm::translate( world, draw->Source->Patches[i]->Offset.InternalVector ) ), entity.GetHandle() } );
        }
        Mesh* directMesh = entity.TryGetComponent<Mesh>();
        if( Mesh* mesh = directMesh; mesh && mesh->IsEnabled() )
        {
            if( mesh->Id == std::numeric_limits<unsigned int>::max() ) mesh->Id = renderer.GetMeshCache().Push( Moonlight::MeshCommand() );
            m_meshCommands[entity.GetId().Value()] = mesh->Id;
            items.push_back( { mesh, entity.GetComponent<Transform>().GetLocalToWorldMatrix(), entity.GetHandle() } );
        }
        else
        {
            auto previous = m_meshCommands.find( entity.GetId().Value() );
            if( previous != m_meshCommands.end() )
            {
                renderer.GetMeshCache().Pop( previous->second ); m_meshCommands.erase( previous );
                if( directMesh ) directMesh->Id = std::numeric_limits<unsigned int>::max();
            }
        }
        Model* model = entity.TryGetComponent<Model>();
        if( !model || !model->IsEnabled() || !model->IsCompact() ) continue;
        compactModels.insert( entity.GetId().Value() );
        model->UpdatePoseMatrices();
        auto& ids = m_compactCommands[entity.GetId().Value()];
        for( auto& part : model->GetRenderParts() )
        {
            if( model->IsPartExternal( part ) ) continue;
            Mesh& mesh = *part.Renderer;
            if( mesh.Id == std::numeric_limits<unsigned int>::max() )
            {
                mesh.Id = renderer.GetMeshCache().Push( Moonlight::MeshCommand() );
                ids.push_back( mesh.Id );
            }
            items.push_back( { &mesh, model->GetPartWorld( part ), model->GetNodeEntity( part.NodeIndex ) } );
        }
    }
    for( auto it = m_terrainDraws.begin(); it != m_terrainDraws.end(); )
    {
        const uint64_t id = it->first;
        ++it;
        if( !terrains.contains( id ) ) RemoveTerrainDraw( id );
    }
    // Patterns are shared across instances; release unused patterns after every instance updates.
    for( auto it = m_terrainTopology.begin(); it != m_terrainTopology.end(); )
        if( it->second.use_count() == 1 ) it = m_terrainTopology.erase( it ); else ++it;

    for( auto it = m_compactCommands.begin(); it != m_compactCommands.end(); )
    {
        if( compactModels.count( it->first ) ) { ++it; continue; }
        for( unsigned int id : it->second ) renderer.GetMeshCache().Pop( id );
        EntityHandle entity = GetWorld().FindEntityByIDValue( it->first );
        if( entity ) if( Model* model = entity->TryGetComponent<Model>() )
            for( auto& part : model->GetRenderParts() ) part.Renderer->Id = std::numeric_limits<unsigned int>::max();
        it = m_compactCommands.erase( it );
    }

    // Lights are rebuilt every frame (cheap: a handful of POD commands).
    {
        std::vector<Moonlight::LightCommand>& lights = renderer.GetLights();
        lights.clear();
        GetWorld().Each<Transform, Light>( [&]( Entity& entity, Transform& transform, Light& light ) {
            if( entity.IsActiveInHierarchy() && light.IsEnabled() )
            {
                lights.push_back( light.BuildCommand( transform ) );
            }
        } );
    }

    // Decals likewise. One whose albedo is still streaming in waits rather than drawing a solid tint.
    {
        std::vector<Moonlight::DecalCommand>& decals = renderer.GetDecals();
        decals.clear();
        GetWorld().Each<Transform, Decal>( [&]( Entity& entity, Transform& transform, Decal& decal ) {
            if( entity.IsActiveInHierarchy() && decal.IsEnabled() && decal.Resolve() )
            {
                Moonlight::DecalCommand command = decal.BuildCommand( transform );
                // A zero scale can't be projected through.
                if( std::abs( glm::determinant( command.World ) ) > 1e-12f )
                {
                    decals.push_back( command );
                }
            }
        } );
    }

    if( items.empty() )
    {
        return;
    }

    // Allocation/uploads stay on this thread. Workers fill disjoint vertex ranges so a
    // large material mesh doesn't monopolize one worker, and paused poses do no sampling.
    struct VertexAnimationJob
    {
        Mesh* Instance;
        size_t Begin, End;
        AABB Bounds;
    };
    std::vector<Mesh*> vertexAnimations;
    std::vector<VertexAnimationJob> vertexJobs;
    for( const RenderItem& item : items )
    {
        Mesh& mesh = *item.Instance;
        if( mesh.HasVertexAnimation() || bgfx::isValid( mesh.m_posedVertices ) ) mesh.PrepareVertexAnimation();
        if( mesh.HasVertexAnimation() && mesh.BeginVertexAnimation() )
        {
            vertexAnimations.push_back( &mesh );
            const size_t count = mesh.m_vertexAnimationVertices.size();
            for( size_t begin = 0; begin < count; begin += 4096 )
                vertexJobs.push_back( { &mesh, begin, std::min( begin + 4096, count ), AABB() } );
        }
    }
    engine.GetJobSystem().ParallelFor( static_cast<uint32_t>( vertexJobs.size() ), 1,
        [&]( uint32_t begin, uint32_t end ) {
            OPTICK_EVENT( "Vertex Animation" );
            for( uint32_t i = begin; i < end; ++i )
            {
                auto& job = vertexJobs[i];
                job.Bounds = job.Instance->SampleVertexAnimation( job.Begin, job.End );
            }
        } );
    size_t job = 0;
    for( Mesh* mesh : vertexAnimations )
    {
        AABB bounds;
        while( job < vertexJobs.size() && vertexJobs[job].Instance == mesh ) bounds.Encapsulate( vertexJobs[job++].Bounds );
        mesh->EndVertexAnimation( bounds );
    }

    auto& cameras = renderer.GetCameraCache();

    // Levels of detail follow the view being looked through: the scene view's camera while
    // editing, else the main camera (one level per mesh per frame, shared by every pass).
    const Moonlight::CameraData* lodCamera = nullptr;
#if USING( ME_EDITOR )
    if( !m_playing )
    {
        lodCamera = &engine.EditorCamera;
    }
#endif
    for( const Moonlight::CameraData& cam : cameras.Commands )
    {
        if( !lodCamera && cam.IsMain && cam.ShouldRender )
        {
            lodCamera = &cam;
        }
    }
    const float lodBias = LodBias;

    // Resize visible flag vector
#if USING( ME_EDITOR )
    Moonlight::CameraData& editorCamera = engine.EditorCamera;
    editorCamera.VisibleFlags.clear();
    editorCamera.VisibleFlags.resize( items.size() );
#endif

    for( Moonlight::CameraData& cam : cameras.Commands )
    {
        cam.VisibleFlags.clear();
        cam.VisibleFlags.resize( items.size() );
    }

    engine.GetJobSystem().ParallelFor( static_cast<uint32_t>( items.size() ), 128, [&]( uint32_t batchBegin, uint32_t batchEnd )
            {
                OPTICK_CATEGORY( "Mesh Job", Optick::Category::Debug );
                for( uint32_t entIndex = batchBegin; entIndex < batchEnd; ++entIndex )
                {
                    //OPTICK_CATEGORY( "Update Transform", Optick::Category::Scene );
                    const RenderItem& item = items[entIndex];
                    {
                        Mesh& model = *item.Instance;
                        if( model.MeshMaterial )
                        {
                            model.MeshMaterial->PrepareForDraw();
                        }
                        // Meshes without geometry (e.g. a Model still loading) or whose material's
                        // shaders are still loading have nothing to draw yet.
                        if( !model.MeshReferece || !model.MeshMaterial || !model.MeshMaterial->MeshShader.IsLoaded() )
                        {
                            continue;
                        }

                        bool isVisible = false;
                        const glm::mat4& meshMatrix = item.World.GetInternalMatrix();
                        // Cull the mesh's world-space bounding box, not just its origin: big
                        // objects whose pivot leaves the view must still be drawn.
                        // Skinned meshes are bounded by their bones' reach in the current pose.
                        const bool skinned = model.IsSkinned();
                        const AABB worldBounds = model.HasVertexAnimation() ? model.GetWorldBounds( item.World )
                            : skinned ? model.UpdateSkin( meshMatrix ) : model.MeshReferece->Bounds.Transformed( item.World );

                        {
                            for( Moonlight::CameraData& cam : cameras.Commands )
                            {
                                if( !cam.ShouldCull )
                                {
                                    isVisible = true;
                                    continue;
                                }

                                if( cam.ViewFrustum.Intersects( worldBounds ) )
                                {
                                    isVisible = true;
                                    cam.VisibleFlags[entIndex] = 1;
                                }
                            }
#if USING( ME_EDITOR )
                            //if( !cam.ShouldCull )
                            //{
                            //    isVisible = true;
                            //}

                            if( editorCamera.ViewFrustum.Intersects( worldBounds ) )
                            {
                                isVisible = true;
                                editorCamera.VisibleFlags[entIndex] = 1;
                            }
#endif
                        }

                        // Every mesh gets a fresh command each frame, visible or not: shadow passes
                        // need casters outside the camera views, and VisibilityIndex must always
                        // match this frame's visibility flags.
                        {
                            Moonlight::MeshCommand command;
                            command.Visible = isVisible && item.Owner->IsActiveInHierarchy();
                            command.WorldBounds = worldBounds;
                            command.CastShadows = model.CastShadows && item.Owner->IsActiveInHierarchy() && ( !model.MeshMaterial || model.MeshMaterial->CastsShadows() );
                            command.SingleMesh = model.MeshReferece;
                            command.MeshMaterial = model.MeshMaterial;
                            command.Transform = meshMatrix;
                            command.Type = model.GetType();
                            command.VisibilityIndex = entIndex;
                            command.ID = item.Owner->GetId().Value();

                            if( model.MeshMaterial )
                            {
                                command.IsTransparent = model.MeshMaterial->IsTransparent();
                                command.SupportsInstancing = model.MeshMaterial->SupportsInstancing;
                                command.BatchKey = model.MeshMaterial->GetInstanceBatchKey();
                                command.AlphaCutoff = model.MeshMaterial->GetAlphaCutoff();
                                command.CustomShadow = bgfx::isValid( model.MeshMaterial->GetShadowProgram( false ) );
                            }
                            if( model.MeshReferece )
                            {
                                const uint8_t lodCount = model.MeshReferece->GetLodCount();
                                if( lodCount > 0 && lodCamera )
                                {
                                    const float radius = worldBounds.GetExtents().Length();
                                    const float distance = ( worldBounds.GetCenter() - lodCamera->Position ).Length();
                                    const float height = Moonlight::RelativeScreenHeight( radius, distance, lodCamera->FOV, lodCamera->Projection == Moonlight::ProjectionType::Orthographic, lodCamera->OrthographicSize );
                                    command.Lod = Moonlight::SelectLod( height, model.MeshReferece->GetLodHeights(), lodCount, lodBias );
                                }
                                command.VertexBufferIdx = model.MeshReferece->GetVertexBuffer().idx;
                                command.IndexBufferIdx = model.MeshReferece->GetIndexBuffer( command.Lod ).idx;
                            }
                            if( model.HasVertexAnimation() )
                            {
                                command.PosedVertices = model.m_posedVertices;
                                command.SupportsInstancing = false;
                            }
                            if( skinned )
                            {
                                command.SkinPalette = model.m_skinPalette.data();
                                command.SkinBoneCount = static_cast<uint16_t>( model.m_skinPalette.size() );
                                command.SupportsInstancing = false;
                            }

                            renderer.GetMeshCache().Update( model.GetId(), command);
                        }
                    }
                }
            } );

    for( Mesh* mesh : vertexAnimations ) mesh->UploadVertexAnimation();

#if USING( ME_EDITOR )
    renderer.SetDebugDrawEnabled( EnableDebugDraw );
#endif
}

void RenderCore::OnDeviceLost()
{
}

void RenderCore::OnDeviceRestored()
{
}

void RenderCore::OnStart()
{
    m_playing = true;
}


void RenderCore::OnStop()
{
    m_playing = false;
    ClearTerrainDraws();
    for( Entity& entity : GetEntities() )
    {
        if( Mesh* mesh = entity.TryGetComponent<Mesh>() ) mesh->Id = std::numeric_limits<unsigned int>::max();
        if( Model* model = entity.TryGetComponent<Model>() )
            for( auto& part : model->GetRenderParts() ) part.Renderer->Id = std::numeric_limits<unsigned int>::max();
    }
    m_compactCommands.clear(); m_meshCommands.clear();
    GetEngine().GetRenderer().ClearMeshes();
    //m_renderer->ClearDebugColliders();
}

#if USING( ME_EDITOR )

void RenderCore::OnEditorInspect()
{
    BGFXRenderer& renderer = GetEngine().GetRenderer();
    const bgfx::Caps* caps = bgfx::getCaps();
    const bgfx::Stats* stats = bgfx::getStats();
    ImGui::PushID( "RenderCore" );

    // Sub-sections of the core's inspector, laid out like a component's categories.
    auto section = []( const char* label ) {
        return ImGui::TreeNodeEx( label, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding );
    };
    auto checkbox = []( const char* label, bool& value ) {
        HavanaUtils::Label( label );
        return ImGui::Checkbox( ( std::string( "##" ) + label ).c_str(), &value );
    };

    if( section( "Rendering" ) )
    {
        Base::OnEditorInspect();
        if( caps )
        {
            HavanaUtils::Label( "Backend" );
            ImGui::TextUnformatted( bgfx::getRendererName( caps->rendererType ) );
        }
        HavanaUtils::Label( "Scene" );
        ImGui::Text( "%zu cameras, %zu lights, %zu decals", renderer.GetCameraCache().Commands.size(), renderer.GetLights().size(), renderer.GetDecals().size() );
        if( checkbox( "Enable Debug Draw", EnableDebugDraw ) )
        {
            renderer.SetDebugDrawEnabled( EnableDebugDraw );
        }
        HavanaUtils::Label( "LOD Bias" );
        ImGui::SliderFloat( "##LODBias", &LodBias, 0.f, 4.f, "%.2f" );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Scales mesh screen size before choosing LOD: above 1 keeps detail longer; 0 draws the coarsest level" );
        }
        ImGui::TreePop();
    }

    if( section( "Presentation" ) )
    {
        bool vsync = renderer.IsVSyncEnabled();
        if( checkbox( "VSync", vsync ) )
        {
            renderer.SetVSyncEnabled( vsync );
        }
        checkbox( "Composite Game UI", renderer.EnableUIComposite );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Composites the HTML UI over the main camera" );
        }
        const char* levels[] = { "Off", "2x", "4x", "8x", "16x" };
        int level = static_cast<int>( renderer.GetMSAALevel() );
        HavanaUtils::Label( "Backbuffer MSAA" );
        if( ImGui::Combo( "##BackbufferMSAA", &level, levels, 5 ) )
        {
            renderer.SetMSAALevel( static_cast<BGFXRenderer::MSAALevel>( level ) );
        }
        if( stats )
        {
            HavanaUtils::Label( "Backbuffer" );
            ImGui::Text( "%u x %u", stats->width, stats->height );
        }
        ImGui::TextWrapped( "MSAA requests samples for the backbuffer. Camera HDR targets are single-sample; use FXAA or TAA in Camera Post-processing for scene edges. The backend may reduce the requested sample count." );
        ImGui::TextDisabled( "Global rendering controls apply to this session." );
        ImGui::TreePop();
    }

    if( section( "Shadows" ) )
    {
        BGFXRenderer::ShadowSettings& shadows = renderer.Shadows;
        checkbox( "Enable Shadows", shadows.Enabled );
        if( !renderer.m_shadowsSupported )
        {
            ImGui::TextWrapped( "Shadow comparison sampling is unavailable on this backend." );
        }
        ImGui::BeginDisabled( !shadows.Enabled || !renderer.m_shadowsSupported );
        checkbox( "Show Shadow Cascades", shadows.DebugCascades );
        // Resolutions are per cascade/light/face. Check the complete atlas against the GPU limit.
        auto resolutionCombo = [&]( const char* label, uint16_t& value, int kind ) {
            HavanaUtils::Label( label );
            if( ImGui::BeginCombo( ( std::string( "##" ) + label ).c_str(), std::to_string( value ).c_str() ) )
            {
                const uint16_t sizes[] = { 128, 256, 512, 1024, 2048, 4096 };
                for( uint16_t size : sizes )
                {
                    if( ( kind == 0 && size < 512 ) || ( kind == 2 && size > 2048 ) )
                    {
                        continue;
                    }
                    const uint32_t spot = kind == 1 ? size : shadows.SpotResolution;
                    const uint32_t point = kind == 2 ? size : shadows.PointResolution;
                    const uint32_t width = kind == 0 ? size * 2 : spot * 2 + point * 4;
                    const uint32_t height = kind == 0 ? size * 2 : std::max( spot * 2, point * 4 );
                    const bool supported = !caps || ( width <= caps->limits.maxTextureSize && height <= caps->limits.maxTextureSize );
                    ImGui::BeginDisabled( !supported );
                    if( ImGui::Selectable( std::to_string( size ).c_str(), value == size ) )
                    {
                        value = size;
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
        };
        resolutionCombo( "Cascade Resolution", shadows.CascadeResolution, 0 );
        resolutionCombo( "Spot Shadow Resolution", shadows.SpotResolution, 1 );
        resolutionCombo( "Point Shadow Resolution", shadows.PointResolution, 2 );
        HavanaUtils::Label( "Sun Atlas" );
        ImGui::Text( "%u x %u", shadows.CascadeResolution * 2u, shadows.CascadeResolution * 2u );
        HavanaUtils::Label( "Local Atlas" );
        ImGui::Text( "%u x %u", shadows.SpotResolution * 2u + shadows.PointResolution * 4u,
            std::max( shadows.SpotResolution * 2u, shadows.PointResolution * 4u ) );
        ImGui::TextWrapped( "Four sun cascades, up to four shadowed spots and two shadowed points. Light components control shadow distance and bias." );
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    if( section( "Environment Lighting" ) )
    {
        checkbox( "Enable Environment Lighting", renderer.EnableEnvironmentLighting );
        ImGui::BeginDisabled( !renderer.EnableEnvironmentLighting );
        HavanaUtils::Label( "Environment Intensity" );
        ImGui::SliderFloat( "##EnvironmentIntensity", &renderer.EnvironmentIntensity, 0.f, 4.f, "%.2f" );
        ImGui::TextWrapped( "Ambient diffuse and reflections come from each camera's sky, skybox or clear color." );
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    // bgfx timestamps are ticks. CPU and GPU timers may have different frequencies.
    auto drawTime = []( int64_t ticks, int64_t frequency ) {
        if( frequency > 0 && ticks >= 0 )
        {
            ImGui::Text( "%.3f ms", static_cast<double>( ticks ) * 1000.0 / static_cast<double>( frequency ) );
        }
        else
        {
            ImGui::TextDisabled( "Unavailable" );
        }
    };
    if( section( "Performance" ) )
    {
        if( stats )
        {
            HavanaUtils::Label( "CPU Frame" );
            drawTime( stats->cpuTimeFrame, stats->cpuTimerFreq );
            HavanaUtils::Label( "Render Thread" );
            drawTime( stats->cpuTimeEnd - stats->cpuTimeBegin, stats->cpuTimerFreq );
            HavanaUtils::Label( "GPU Frame" );
            drawTime( stats->gpuTimeEnd - stats->gpuTimeBegin, stats->gpuTimerFreq );
            HavanaUtils::Label( "Draw Calls" );
            ImGui::Text( "%u", stats->numDraw );
            HavanaUtils::Label( "Triangles" );
            ImGui::Text( "%u", stats->numPrims[bgfx::Topology::TriList] );
            HavanaUtils::Label( "Compute / Blit Calls" );
            ImGui::Text( "%u / %u", stats->numCompute, stats->numBlit );
            HavanaUtils::Label( "Textures / Framebuffers" );
            ImGui::Text( "%u / %u", stats->numTextures, stats->numFrameBuffers );
            HavanaUtils::Label( "Texture Memory" );
            ImGui::Text( "%.1f MiB", stats->textureMemoryUsed / ( 1024.0 * 1024.0 ) );
            HavanaUtils::Label( "Render Target Memory" );
            ImGui::Text( "%.1f MiB", stats->rtMemoryUsed / ( 1024.0 * 1024.0 ) );
            HavanaUtils::Label( "GPU Memory" );
            if( stats->gpuMemoryUsed >= 0 )
            {
                ImGui::Text( "%.1f MiB", stats->gpuMemoryUsed / ( 1024.0 * 1024.0 ) );
            }
            else
            {
                ImGui::TextDisabled( "Unavailable" );
            }
        }
        else
        {
            ImGui::TextDisabled( "Renderer statistics are unavailable." );
        }
        ImGui::TreePop();
    }
    if( section( "View Stats" ) )
    {
        ImGui::TextWrapped( "Open the Profiler window to collect per-view GPU timings." );
        if( stats && stats->viewStats && ImGui::BeginTable( "ViewStats", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) )
        {
            ImGui::TableSetupColumn( "View" );
            ImGui::TableSetupColumn( "CPU ms", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em( 4.f ) );
            ImGui::TableSetupColumn( "GPU ms", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em( 4.f ) );
            ImGui::TableHeadersRow();
            for( uint16_t i = 0; i < stats->numViews; ++i )
            {
                const bgfx::ViewStats& view = stats->viewStats[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( view.name );
                ImGui::TableSetColumnIndex( 1 );
                drawTime( view.cpuTimeEnd - view.cpuTimeBegin, stats->cpuTimerFreq );
                ImGui::TableSetColumnIndex( 2 );
                if( FrameStats::Get().DetailedGpuTimings )
                {
                    drawTime( view.gpuTimeEnd - view.gpuTimeBegin, stats->gpuTimerFreq );
                }
                else
                {
                    ImGui::TextDisabled( "Unavailable" );
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
    if( section( "Picking Pass" ) )
    {
        if( auto& pickingPass = renderer.m_pickingPass )
        {
            if( bgfx::isValid( pickingPass->m_pickingRT ) )
            {
                const float width = std::max( 1.f, std::min( HavanaUtils::RowWidth(), pickingPass->m_width * 4.f ) );
                const float height = width * pickingPass->m_height / std::max( 1.f, static_cast<float>( pickingPass->m_width ) );
                ImGui::Image( pickingPass->m_pickingRT, ImVec2( width, height ) );
            }
            HavanaUtils::Label( "Picking Field of View" );
            ImGui::SliderFloat( "##PickingFOV", &pickingPass->m_fov, 1.0f, 60.0f );
            checkbox( "Force Picking Draw", pickingPass->ForceDraw );
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

#endif
