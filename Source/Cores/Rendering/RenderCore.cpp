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
#include "Components/Lighting/DirectionalLight.h"
#include "Work/Burst.h"
#include "Renderer.h"
#include <Core/JobSystem.h>
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

    if( Renderables.empty() )
    {
        return;
    }

    SimpleJobSystem& simpleJobSystem = engine.GetJobSystem();

    std::vector<std::pair<int, int>> batches;
    Burst::GenerateChunks( Renderables.size(), simpleJobSystem.GetNumWorkers(), batches );

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

    for( auto& batch : batches )
    {
        OPTICK_CATEGORY( "Create Render Jobs", Optick::Category::Debug );
        int batchBegin = batch.first;
        int batchEnd = batch.second;
        int batchSize = batchEnd - batchBegin;

        auto meshJob = [&renderer, &Renderables, &cameras, batchBegin, batchEnd, batchSize
#if USING( ME_EDITOR )
            , &editorCamera
#endif
        ]()
            {
                OPTICK_CATEGORY( "Mesh Job", Optick::Category::Debug );
                for( int entIndex = batchBegin; entIndex < batchEnd; ++entIndex )
                {
                    OPTICK_CATEGORY( "Update Transform", Optick::Category::Scene );
                    auto& InEntity = Renderables[entIndex];
                    {
                        Transform& transform = InEntity.GetComponent<Transform>();
                        Mesh& model = InEntity.GetComponent<Mesh>();
                        bool isVisible = false;
                        const glm::mat4& meshMatrix = transform.GetLocalToWorldMatrix().GetInternalMatrix();

                        {
                            OPTICK_CATEGORY( "Culling", Optick::Category::Visibility );
                            glm::vec4 point = glm::vec4( meshMatrix[3] );
                            for( Moonlight::CameraData& cam : cameras.Commands )
                            {
                                if( !cam.ShouldCull )
                                {
                                    isVisible = true;
                                    continue;
                                }

                                if( cam.ViewFrustum.IsPointInFrustum( point ) )
                                {
                                    isVisible = true;
                                    cam.VisibleFlags[entIndex] = true;
                                }
                            }
#if USING( ME_EDITOR )
                            //if( !cam.ShouldCull )
                            //{
                            //    isVisible = true;
                            //}

                            if( editorCamera.ViewFrustum.IsPointInFrustum( point ) )
                            {
                                isVisible = true;
                                editorCamera.VisibleFlags[entIndex] = true;
                            }
#endif
                        }

                        if( isVisible )
                        {
                            OPTICK_CATEGORY( "Submit", Optick::Category::Rendering );
                            Moonlight::MeshCommand command;
                            command.SingleMesh = model.MeshReferece;
                            command.MeshMaterial = model.MeshMaterial;
                            command.Transform = meshMatrix;
                            command.Type = model.GetType();
                            command.VisibilityIndex = entIndex;
                            command.ID = InEntity.GetId().Value();
                            renderer.GetMeshCache().Update( model.GetId(), command);
                        }
                    }
                }
            };
        simpleJobSystem.submit( meshJob );
        //meshJob();
    }
    simpleJobSystem.waitForAllJobs( true );

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
    if( ImGui::Button( "MSAA None" ) )
    {
        GetEngine().GetRenderer().SetMSAALevel( BGFXRenderer::MSAALevel::None );
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