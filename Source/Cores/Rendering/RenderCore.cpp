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
        Moonlight::MeshCommand command;
        Mesh& model = NewEntity.GetComponent<Mesh>();
        command.SingleMesh = model.MeshReferece;
        command.MeshMaterial = model.MeshMaterial;
        command.Transform = NewEntity.GetComponent<Transform>().GetMatrix().GetInternalMatrix();
        command.Type = model.GetType();
        model.Id = GetEngine().GetRenderer().GetMeshCache().Push( command );
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
    if( Renderables.empty() )
    {
        return;
    }

    std::vector<std::pair<int, int>> batches;
    Burst::GenerateChunks( Renderables.size(), 44, batches );

    Engine& engine = GetEngine();
    JobSystem& jobSystem = engine.m_jobSystem;
    BGFXRenderer& renderer = engine.GetRenderer();
    for( auto& batch : batches )
    {
        OPTICK_CATEGORY( "Create Render Jobs", Optick::Category::Debug );
        int batchBegin = batch.first;
        int batchEnd = batch.second;
        int batchSize = batchEnd - batchBegin;

        auto meshJob = [&renderer, &Renderables, batchBegin, batchEnd, batchSize]( ) {
            for( int entIndex = batchBegin; entIndex < batchEnd; ++entIndex )
            {
                auto& InEntity = Renderables[entIndex];
                Transform& transform = InEntity.GetComponent<Transform>();
                Mesh& model = InEntity.GetComponent<Mesh>();

                OPTICK_EVENT_DYNAMIC( transform.GetName().c_str(), Optick::Category::Scene );
                renderer.UpdateMeshMatrix( model.GetId(), transform.GetMatrix().GetInternalMatrix() );
            }
        };
        jobSystem.AddWork( meshJob, false );
        jobSystem.SignalWorkAvailable();
    }
    jobSystem.Wait();

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
    GetEngine().GetRenderer().GetMeshCache().Pop( InMesh->Id );

    Moonlight::MeshCommand command;
    command.SingleMesh = InMesh->MeshReferece;
    command.MeshMaterial = InMesh->MeshMaterial;
    command.Type = InMesh->GetType();
    InMesh->Id = GetEngine().GetRenderer().GetMeshCache().Push( command );
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


}

#endif