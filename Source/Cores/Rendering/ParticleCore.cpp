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
    OPTICK_CATEGORY( "ParticleCore::Update", Optick::Category::Rendering );
    Engine& engine = *inUpdateContext.GetSystem<Engine>();
    std::vector<Moonlight::ParticleBatch>& batches = engine.GetRenderer().GetParticleBatches();
    std::vector<Entity>& entities = GetEntities();

    // Batches are reused frame to frame (their instance arrays keep their capacity).
    for( Moonlight::ParticleBatch& batch : batches )
    {
        batch.Instances.clear();
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
    // Texture loads touch the resource cache: main thread only.
    for( Entity& entity : entities )
    {
        entity.GetComponent<ParticleSystem>().ResolveTexture();
    }

    const float dt = inUpdateContext.GetDeltaTime();
    engine.GetJobSystem().ParallelFor( static_cast<uint32_t>( entities.size() ), 1, [&]( uint32_t begin, uint32_t end ) {
        for( uint32_t i = begin; i < end; ++i )
        {
            Entity& entity = entities[i];
            ParticleSystem& system = entity.GetComponent<ParticleSystem>();
            if( !entity.IsActiveInHierarchy() || !system.IsEnabled() )
            {
                continue;
            }
            const glm::mat4& world = entity.GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix();
            system.Simulate( dt, world );
            system.FillBatch( batches[i], world );
        }
    } );

    m_liveParticles = 0;
    for( const Moonlight::ParticleBatch& batch : batches )
    {
        m_liveParticles += batch.Instances.size();
    }
}


void ParticleCore::OnStop()
{
    for( Moonlight::ParticleBatch& batch : GetEngine().GetRenderer().GetParticleBatches() )
    {
        batch.Instances.clear();
    }
}


#if USING( ME_EDITOR )
void ParticleCore::OnEditorInspect()
{
    Base::OnEditorInspect();
    ImGui::Text( "Systems: %zu  Particles: %zu", GetEntities().size(), m_liveParticles );
}
#endif
