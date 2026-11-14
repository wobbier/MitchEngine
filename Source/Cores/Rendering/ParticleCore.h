#pragma once
#include "ECS/Core.h"

namespace Moonlight { struct ParticleBatch; }

// Simulates every ParticleSystem (in parallel, one job per system) and hands their particles to the
// renderer; GPU-simulated systems only run their emitter here and the renderer dispatches the
// rest. Owned by the engine and updated every frame, also in the editor, so effects preview
// without entering play mode; pausing the game freezes them.
class ParticleCore final
    : public Core<ParticleCore>
{
public:
    ParticleCore();

    void Update( const UpdateContext& inUpdateContext ) final;
    void OnStop() final;

    size_t GetLiveParticleCount() const { return m_liveParticles; }

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    // Empties a batch, letting go of its GPU buffers.
    static void ClearBatch( Moonlight::ParticleBatch& InBatch );

    size_t m_liveParticles = 0;
};
