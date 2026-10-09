#pragma once
#include "ECS/Core.h"

// Simulates every ParticleSystem (in parallel, one job per system) and hands their particles to the
// renderer. Owned by the engine and updated every frame, also in the editor, so effects preview
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
    size_t m_liveParticles = 0;
};
