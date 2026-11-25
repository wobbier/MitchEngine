#include "PCH.h"
#include "ParticleCore.h"
#include "Components/Effects/ParticleSystem.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Renderer.h"
#include "optick.h"

#if USING( ME_EDITOR )
#include <imgui.h>
#endif


ParticleCore::ParticleCore()
    : Base( ComponentFilter().Requires<Transform>().Requires<ParticleSystem>() )
{
    SetIsSerializable( false );
}


void ParticleCore::Update( const UpdateContext& inUpdateContext )
{
#if USING( ME_HEADLESS )
    // Particles are only ever drawn: a dedicated server doesn't simulate them (ME-286).
    return;
#endif
    OPTICK_CATEGORY( "ParticleCore::Update", Optick::Category::Rendering );
    Engine& engine = *inUpdateContext.GetSystem<Engine>();
    std::vector<Moonlight::ParticleBatch>& batches = engine.GetRenderer().GetParticleBatches();
    std::vector<Entity>& entities = GetEntities();

    // Batches are reused frame to frame (their instance arrays keep their capacity). A batch only
    // refers to GPU buffers for the frame its system stepped.
    for( Moonlight::ParticleBatch& batch : batches )
    {
        ClearBatch( batch );
    }
    if( batches.size() < entities.size() )
    {
        batches.resize( entities.size() );
    }
    if( entities.empty() )
    {
        m_liveParticles = 0;
        return;
    }

    Transform::UpdateAll( GetWorld() );
    // Texture loads touch the resource cache, and GPU buffers are bgfx objects: main thread only.
    // The simulation path is decided here too, so it holds for the whole frame.
    const bool computeActive = engine.GetRenderer().IsComputeActive();
    for( Entity& entity : entities )
    {
        ParticleSystem& system = entity.GetComponent<ParticleSystem>();
        system.ResolveTexture();
        system.UpdateSimulationMode( computeActive );
        if( system.UsesGpuSimulation() && entity.IsActiveInHierarchy() && system.IsEnabled() )
        {
            system.EnsureGpuBuffers();
        }
    }

    const float dt = inUpdateContext.GetDeltaTime();
    engine.GetJobSystem().ParallelFor( "Particles", static_cast<uint32_t>( entities.size() ), 1, [&]( uint32_t begin, uint32_t end ) {
        for( uint32_t i = begin; i < end; ++i )
        {
            Entity& entity = entities[i];
            ParticleSystem& system = entity.GetComponent<ParticleSystem>();
            if( !entity.IsActiveInHierarchy() || !system.IsEnabled() )
            {
                continue;
            }
            const glm::mat4& world = entity.GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix();
            // Which cameras draw it (Camera::CullingMask).
            batches[i].LayerBit = 1u << ( entity.GetLayer() & 31u );
            if( system.UsesGpuSimulation() )
            {
                // Only the emitter runs here; the renderer dispatches the step.
                system.StepGpu( dt, world, batches[i] );
            }
            else
            {
                system.Simulate( dt, world );
                system.FillBatch( batches[i], world );
            }
        }
    } );

    m_liveParticles = 0;
    for( const Moonlight::ParticleBatch& batch : batches )
    {
        m_liveParticles += batch.Instances.size() + batch.GpuLiveCount;
    }
}


void ParticleCore::OnStop()
{
    for( Moonlight::ParticleBatch& batch : GetEngine().GetRenderer().GetParticleBatches() )
    {
        ClearBatch( batch );
    }
}


void ParticleCore::ClearBatch( Moonlight::ParticleBatch& InBatch )
{
    InBatch.Instances.clear();
    InBatch.Gpu = nullptr;
    InBatch.GpuStepPending = false;
    InBatch.GpuInstanceCount = 0;
    InBatch.GpuLiveCount = 0;
}


#if USING( ME_EDITOR )
void ParticleCore::OnEditorInspect()
{
    Base::OnEditorInspect();
    ImGui::Text( "Systems: %zu  Particles: %zu", GetEntities().size(), m_liveParticles );
}
#endif
