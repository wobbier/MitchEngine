#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"

class Animator;

// Plays Animators (engine-owned, while the world is started). Each frame, per Animator:
//   1. bind (once): find the clips (this entity's Model, or ClipSource) and the node entities they
//      animate below the Animator, remembering each node's bind pose;
//   2. state machine: pending Play() calls, then transitions on parameters / exit times;
//   3. sample the current state (a clip or a 1D blend) and, while cross-fading, the previous one,
//      on the job system;
//   4. write the poses to the node Transforms and fire AnimationEvents.
// RenderCore then skins meshes from those node Transforms.
class AnimationCore final
    : public Core<AnimationCore>
{
public:
    AnimationCore();

    void OnStart() final;
    void OnStop() final;
    void Update( const UpdateContext& inUpdateContext ) final;

    // Advances every animator by InDeltaSeconds (Update calls this with the game delta).
    void Advance( float InDeltaSeconds );

private:
    bool m_running = false;

    void Bind( Entity& InEntity, Animator& InAnimator );
    void StepStateMachine( Entity& InEntity, Animator& InAnimator, float InDeltaSeconds );
    static void SamplePose( Animator& InAnimator );
    static void WritePose( Animator& InAnimator );
};

ME_REGISTER_CORE( AnimationCore )
