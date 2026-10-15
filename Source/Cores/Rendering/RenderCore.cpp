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
#include "Components/Graphics/Mesh.h"
#include "Engine/Engine.h"
#include "Work/Burst.h"
#include "Renderer.h"
#include "Camera/CameraData.h"
#include "RenderPasses/PickingPass.h"
#include "Utils/ImGuiUtils.h"
#include "Core/CommandLine.h"
#include "Profiling/FrameStats.h"


RenderCore::RenderCore()
    : Base( ComponentFilter().Requires<Transform>().Requires<Mesh>() )
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
    GetEngine().GetRenderer().ClearMeshes();
}

void RenderCore::OnEntityAdded( Entity& NewEntity )
{
    if( NewEntity.HasComponent<Mesh>() )
    {
        NewEntity.GetComponent<Mesh>().Id = GetEngine().GetRenderer().GetMeshCache().Push( Moonlight::MeshCommand() );
    }
}

void RenderCore::OnEntityRemoved( Entity& InEntity )
{
    if( InEntity.HasComponent<Mesh>() )
    {
        Mesh& mesh = InEntity.GetComponent<Mesh>();
        GetEngine().GetRenderer().GetMeshCache().Pop( mesh.Id );
    }
}

RenderCore::~RenderCore()
{
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

    if( Renderables.empty() )
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
    for( Entity& entity : Renderables )
    {
        Mesh& mesh = entity.GetComponent<Mesh>();
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
    editorCamera.VisibleFlags.resize( Renderables.size() );
#endif

    for( Moonlight::CameraData& cam : cameras.Commands )
    {
        cam.VisibleFlags.clear();
        cam.VisibleFlags.resize( Renderables.size() );
    }

    engine.GetJobSystem().ParallelFor( static_cast<uint32_t>( Renderables.size() ), 128, [&]( uint32_t batchBegin, uint32_t batchEnd )
            {
                OPTICK_CATEGORY( "Mesh Job", Optick::Category::Debug );
                for( uint32_t entIndex = batchBegin; entIndex < batchEnd; ++entIndex )
                {
                    //OPTICK_CATEGORY( "Update Transform", Optick::Category::Scene );
                    auto& InEntity = Renderables[entIndex];
                    {
                        Transform& transform = InEntity.GetComponent<Transform>();
                        Mesh& model = InEntity.GetComponent<Mesh>();
                        // Meshes without geometry (e.g. a Model still loading) have nothing to draw.
                        if( !model.MeshReferece || !model.MeshMaterial )
                        {
                            continue;
                        }

                        bool isVisible = false;
                        const glm::mat4& meshMatrix = transform.GetLocalToWorldMatrix().GetInternalMatrix();
                        // Cull the mesh's world-space bounding box, not just its origin: big
                        // objects whose pivot leaves the view must still be drawn.
                        // Skinned meshes are bounded by their bones' reach in the current pose.
                        const bool skinned = model.IsSkinned();
                        const AABB worldBounds = model.HasVertexAnimation() ? model.GetWorldBounds( transform.GetLocalToWorldMatrix() )
                            : skinned ? model.UpdateSkin( meshMatrix ) : model.MeshReferece->Bounds.Transformed( transform.GetLocalToWorldMatrix() );

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
                            command.Visible = isVisible;
                            command.WorldBounds = worldBounds;
                            command.CastShadows = model.CastShadows;
                            command.SingleMesh = model.MeshReferece;
                            command.MeshMaterial = model.MeshMaterial;
                            command.Transform = meshMatrix;
                            command.Type = model.GetType();
                            command.VisibilityIndex = entIndex;
                            command.ID = InEntity.GetId().Value();

                            if( model.MeshMaterial )
                            {
                                command.IsTransparent = model.MeshMaterial->IsTransparent();
                                command.SupportsInstancing = model.MeshMaterial->SupportsInstancing;
                                command.BatchKey = model.MeshMaterial->GetInstanceBatchKey();
                                command.AlphaCutoff = model.MeshMaterial->GetAlphaCutoff();
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

    if( ImGui::CollapsingHeader( "Rendering" ) )
    {
        Base::OnEditorInspect();
        if( caps )
        {
            ImGui::Text( "Backend: %s", bgfx::getRendererName( caps->rendererType ) );
        }
        ImGui::Text( "Cameras: %zu | Lights: %zu", renderer.GetCameraCache().Commands.size(), renderer.GetLights().size() );
        if( ImGui::Checkbox( "Enable Debug Draw", &EnableDebugDraw ) )
        {
            renderer.SetDebugDrawEnabled( EnableDebugDraw );
        }
        ImGui::SliderFloat( "LOD Bias", &LodBias, 0.f, 4.f, "%.2f" );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Scales mesh screen size before choosing LOD: above 1 keeps detail longer; 0 draws the coarsest level" );
        }
    }

    if( ImGui::CollapsingHeader( "Presentation" ) )
    {
        bool vsync = renderer.IsVSyncEnabled();
        if( ImGui::Checkbox( "VSync", &vsync ) )
        {
            renderer.SetVSyncEnabled( vsync );
        }
        ImGui::Checkbox( "Composite Game UI", &renderer.EnableUIComposite );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Composites the HTML UI over the main camera" );
        }
        const char* levels[] = { "Off", "2x", "4x", "8x", "16x" };
        int level = static_cast<int>( renderer.GetMSAALevel() );
        if( ImGui::Combo( "Backbuffer MSAA", &level, levels, 5 ) )
        {
            renderer.SetMSAALevel( static_cast<BGFXRenderer::MSAALevel>( level ) );
        }
        ImGui::TextWrapped( "MSAA requests samples for the backbuffer. Camera HDR targets are single-sample; use FXAA or TAA in Camera Post-processing for scene edges. The backend may reduce the requested sample count." );
        if( stats )
        {
            ImGui::Text( "Backbuffer: %u x %u", stats->width, stats->height );
        }
        ImGui::TextDisabled( "Global rendering controls apply to this session." );
    }

    if( ImGui::CollapsingHeader( "Shadows" ) )
    {
        BGFXRenderer::ShadowSettings& shadows = renderer.Shadows;
        ImGui::Checkbox( "Enable Shadows", &shadows.Enabled );
        if( !renderer.m_shadowsSupported )
        {
            ImGui::TextWrapped( "Shadow comparison sampling is unavailable on this backend." );
        }
        ImGui::BeginDisabled( !shadows.Enabled || !renderer.m_shadowsSupported );
        ImGui::Checkbox( "Show Shadow Cascades", &shadows.DebugCascades );
        // Resolutions are per cascade/light/face. Check the complete atlas against the GPU limit.
        auto resolutionCombo = [&]( const char* label, uint16_t& value, int kind ) {
            if( ImGui::BeginCombo( label, std::to_string( value ).c_str() ) )
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
        ImGui::Text( "Sun atlas: %u x %u", shadows.CascadeResolution * 2u, shadows.CascadeResolution * 2u );
        ImGui::Text( "Local atlas: %u x %u", shadows.SpotResolution * 2u + shadows.PointResolution * 4u,
            std::max( shadows.SpotResolution * 2u, shadows.PointResolution * 4u ) );
        ImGui::TextWrapped( "Four sun cascades, up to four shadowed spots and two shadowed points. Light components control shadow distance and bias." );
        ImGui::EndDisabled();
    }

    if( ImGui::CollapsingHeader( "Environment Lighting" ) )
    {
        ImGui::Checkbox( "Enable Environment Lighting", &renderer.EnableEnvironmentLighting );
        ImGui::BeginDisabled( !renderer.EnableEnvironmentLighting );
        ImGui::SliderFloat( "Environment Intensity", &renderer.EnvironmentIntensity, 0.f, 4.f, "%.2f" );
        ImGui::TextWrapped( "Ambient diffuse and reflections come from each camera's sky, skybox or clear color." );
        ImGui::EndDisabled();
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
    if( ImGui::CollapsingHeader( "Performance" ) )
    {
        if( stats && ImGui::BeginTable( "RenderStats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp ) )
        {
            auto row = []( const char* label ) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( label );
                ImGui::TableSetColumnIndex( 1 );
            };
            row( "CPU Frame" );
            drawTime( stats->cpuTimeFrame, stats->cpuTimerFreq );
            row( "Render Thread" );
            drawTime( stats->cpuTimeEnd - stats->cpuTimeBegin, stats->cpuTimerFreq );
            row( "GPU Frame" );
            drawTime( stats->gpuTimeEnd - stats->gpuTimeBegin, stats->gpuTimerFreq );
            row( "Draw Calls" );
            ImGui::Text( "%u", stats->numDraw );
            row( "Triangles" );
            ImGui::Text( "%u", stats->numPrims[bgfx::Topology::TriList] );
            row( "Compute / Blit Calls" );
            ImGui::Text( "%u / %u", stats->numCompute, stats->numBlit );
            row( "Textures / Framebuffers" );
            ImGui::Text( "%u / %u", stats->numTextures, stats->numFrameBuffers );
            row( "Texture Memory" );
            ImGui::Text( "%.1f MiB", stats->textureMemoryUsed / ( 1024.0 * 1024.0 ) );
            row( "Render Target Memory" );
            ImGui::Text( "%.1f MiB", stats->rtMemoryUsed / ( 1024.0 * 1024.0 ) );
            row( "GPU Memory" );
            if( stats->gpuMemoryUsed >= 0 )
            {
                ImGui::Text( "%.1f MiB", stats->gpuMemoryUsed / ( 1024.0 * 1024.0 ) );
            }
            else
            {
                ImGui::TextDisabled( "Unavailable" );
            }
            ImGui::EndTable();
        }
        else if( !stats )
        {
            ImGui::TextDisabled( "Renderer statistics are unavailable." );
        }
    }
    if( ImGui::CollapsingHeader( "View Stats" ) )
    {
        ImGui::TextWrapped( "Open the Profiler window to collect per-view GPU timings." );
        if( stats && stats->viewStats && ImGui::BeginTable( "ViewStats", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) )
        {
            ImGui::TableSetupColumn( "View" );
            ImGui::TableSetupColumn( "CPU ms" );
            ImGui::TableSetupColumn( "GPU ms" );
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
    }
    if( ImGui::CollapsingHeader( "Picking Pass" ) && renderer.m_pickingPass )
    {
        auto& pickingPass = renderer.m_pickingPass;
        if( bgfx::isValid( pickingPass->m_pickingRT ) )
        {
            const float width = std::max( 1.f, std::min( ImGui::GetContentRegionAvail().x, pickingPass->m_width * 4.f ) );
            const float height = width * pickingPass->m_height / std::max( 1.f, static_cast<float>( pickingPass->m_width ) );
            ImGui::Image( pickingPass->m_pickingRT, ImVec2( width, height ) );
        }
        ImGui::SliderFloat( "Picking Field of View", &pickingPass->m_fov, 1.0f, 60.0f );
        ImGui::Checkbox( "Force Picking Draw", &pickingPass->ForceDraw );
    }
    ImGui::PopID();
}

#endif