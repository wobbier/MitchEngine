#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"

class Animator;

// Plays Animators (engine-owned, while the world is started). Each frame, per Animator:
//   1. bind (once): find the clips (this entity's Model, or ClipSource) and the node entities they
//      animate below the Animator, remembering each node's bind pose;
//   2. state machine: pending Play() calls, then transitions on parameters / exit times;
//   3. sample every layer's current state (a clip, or a 1D / 2D blend) and, while cross-fading, the
//      previous one, on the job system; layers override the base on their masked nodes;
//   4. write the poses to the node Transforms, fire AnimationEvents, and apply root motion.
// RenderCore then skins meshes from those node Transforms.
//
// In edit mode it runs only editor previews (Animator::StartPreview): the previewed state is sampled
// at its preview time and written to the nodes; when a preview ends, the bind pose is written back.
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
    bool IsRunning() const
    {
        return m_running;
    }

    // Editor previews: RestorePreviewPoses writes the authored pose back without ending them (before
    // a save or a play snapshot; the next update poses them again); StopAllPreviews ends them.
    void RestorePreviewPoses();
    void StopAllPreviews();
    // Edit-mode update: poses previewing animators (Update calls this while the world isn't running).
    void UpdatePreviews( float InDeltaSeconds );
    // Updates without the inspector before a preview stops (selecting something else ends it).
    static constexpr int kPreviewIdleUpdates = 30;

private:
    bool m_running = false;

    void Bind( Entity& InEntity, Animator& InAnimator );
    void StepStateMachine( Entity& InEntity, Animator& InAnimator, float InDeltaSeconds );
    static void SamplePose( Animator& InAnimator );
    static void WritePose( Animator& InAnimator );
    static void WriteBindPose( Animator& InAnimator );
    void EndPreview( Animator& InAnimator );
    static void ApplyRootMotion( Animator& InAnimator );
};

ME_REGISTER_CORE( AnimationCore )
