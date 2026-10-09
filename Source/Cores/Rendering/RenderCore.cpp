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

    auto& cameras = renderer.GetCameraCache();

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
                        const AABB worldBounds = model.MeshReferece->Bounds.Transformed( transform.GetLocalToWorldMatrix() );

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
                                command.VertexBufferIdx = model.MeshReferece->GetVertexBuffer().idx;
                                command.IndexBufferIdx = model.MeshReferece->GetIndexuffer().idx;
                            }

                            renderer.GetMeshCache().Update( model.GetId(), command);
                        }
                    }
                }
            } );

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

void RenderCore::OnStop()
{
    GetEngine().GetRenderer().ClearMeshes();
    //m_renderer->ClearDebugColliders();
}

void RenderCore::UpdateMesh( Mesh* InMesh )
{
    //GetEngine().GetRenderer().GetMeshCache().Pop( InMesh->Id );
    //
    //Moonlight::MeshCommand command;
    //command.SingleMesh = InMesh->MeshReferece;
    //command.MeshMaterial = InMesh->MeshMaterial;
    //command.Type = InMesh->GetType();
    //InMesh->Id = GetEngine().GetRenderer().GetMeshCache().Push( command );
}

#if USING( ME_EDITOR )

void RenderCore::OnEditorInspect()
{
    Base::OnEditorInspect();

    ImGui::Checkbox( "Enable Debug Draw", &EnableDebugDraw );

    BGFXRenderer::ShadowSettings& shadows = GetEngine().GetRenderer().Shadows;
    ImGui::Checkbox( "Shadows", &shadows.Enabled );
    ImGui::Checkbox( "Show Shadow Cascades", &shadows.DebugCascades );
    const uint16_t sizes[] = { 512, 1024, 2048, 4096 };
    auto sizeCombo = []( const char* label, uint16_t& value, const uint16_t* options, int count ) {
        if( ImGui::BeginCombo( label, std::to_string( value ).c_str() ) )
        {
            for( int i = 0; i < count; ++i )
            {
                if( ImGui::Selectable( std::to_string( options[i] ).c_str(), value == options[i] ) )
                {
                    value = options[i];
                }
            }
            ImGui::EndCombo();
        }
    };
    sizeCombo( "Cascade Resolution", shadows.CascadeResolution, sizes, 4 );
    sizeCombo( "Spot Shadow Resolution", shadows.SpotResolution, sizes, 4 );
    if( ImGui::Button( "MSAA None" ) )
    {
        GetEngine().GetRenderer().SetMSAALevel( BGFXRenderer::MSAALevel::None );
    }
    if( ImGui::Button( "MSAA X8" ) )
    {
        GetEngine().GetRenderer().SetMSAALevel( BGFXRenderer::MSAALevel::X8 );
    }
    if( ImGui::Button( "MSAA X16" ) )
    {
        GetEngine().GetRenderer().SetMSAALevel( BGFXRenderer::MSAALevel::X16 );
    }
    const bgfx::Stats* stats = bgfx::getStats();
    HavanaUtils::Label( "CPU Frame Time: " );
    ImGui::Text( ( std::to_string( bgfx::getStats()->cpuTimeFrame / 1000.f ) + " ms" ).c_str() );
    HavanaUtils::Label( "CPU Time: " );
    ImGui::Text( ( std::to_string( ( stats->cpuTimeEnd - stats->cpuTimeBegin ) / 1000.f ) + " ms" ).c_str() );

    HavanaUtils::Label( "GPU Frame: " );
    ImGui::Text( std::to_string( bgfx::getStats()->gpuFrameNum ).c_str() );
    HavanaUtils::Label( "GPU Draw Calls: " );
    ImGui::Text( std::to_string( bgfx::getStats()->numDraw ).c_str() );
    HavanaUtils::Label( "GPU Compute Calls: " );
    ImGui::Text( std::to_string( bgfx::getStats()->numCompute ).c_str() );
    HavanaUtils::Label( "GPU Time: " );
    ImGui::Text( ( std::to_string( ( stats->gpuTimeEnd - stats->gpuTimeBegin ) / 1000.f ) + " ms" ).c_str() );

    static bool shouldStatsClose = true;
    if( ImGui::CollapsingHeader( "View Stats", &shouldStatsClose, ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        for( uint16_t i = 0; i < stats->numViews; ++i )
        {
            bgfx::ViewStats& view = stats->viewStats[i];
            ImGui::Text( &view.name[0] );
            HavanaUtils::Label( "CPU Time: " );
            ImGui::Text( ( std::to_string( ( view.cpuTimeEnd - view.cpuTimeBegin ) / 1000.f ) + " ms" ).c_str() );
            HavanaUtils::Label( "GPU Time: " );
            ImGui::Text( ( std::to_string( ( view.gpuTimeEnd - view.gpuTimeBegin ) / 1000.f ) + " ms" ).c_str() );
        }
    }
    static bool shouldPickingClose = true;
    if( ImGui::CollapsingHeader( "Picking Pass", &shouldPickingClose, ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        auto& pickingPass = GetEngine().GetRenderer().m_pickingPass;

        ImVec2 pickingSize = ImVec2( pickingPass->m_width / 5.0f - 16.0f, pickingPass->m_width / 5.0f - 16.0f );
        ImGui::Image( pickingPass->m_pickingRT, pickingSize );
        ImGui::SliderFloat( "Field of view", &pickingPass->m_fov, 1.0f, 60.0f );
        if( bgfx::isValid( pickingPass->m_blitTex ) )
        {
            //ImGui::Image( m_blitTex, pickingSize );
        }

        ImGui::Checkbox( "Force Draw", &pickingPass->ForceDraw );
    }
}

#endif