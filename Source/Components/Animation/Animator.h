#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "ECS/EntityHandle.h"
#include "Events/Event.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "Pointers.h"
#include <cstdint>
#include <string>
#include <vector>

class ModelResource;
namespace Moonlight { struct AnimationClip; }

enum class AnimatorParameterType : uint8_t
{
    Float = 0,
    Bool,
    Trigger,    // set by SetTrigger, consumed by the first transition that uses it
};

struct AnimatorParameter
{
    ME_REFLECTABLE( AnimatorParameter )
public:
    std::string Name;
    AnimatorParameterType Type = AnimatorParameterType::Float;
    float Value = 0.f;          // the float, or 0 / 1
};

// One clip of a blend. 1D: the state plays the two clips around its blend parameter's value. 2D
// (a second parameter): each clip sits at (Threshold, ThresholdY) and is weighted by how close the
// parameters are to it (gradient band interpolation).
struct AnimatorBlendClip
{
    ME_REFLECTABLE( AnimatorBlendClip )
public:
    std::string Clip;
    float Threshold = 0.f;
    float ThresholdY = 0.f;
};

struct AnimatorState
{
    ME_REFLECTABLE( AnimatorState )
public:
    std::string Name;
    std::string Clip;           // or a blend over BlendClips by BlendParameter (and BlendParameterY)
    float Speed = 1.f;
    bool Loop = true;
    std::string BlendParameter;
    std::string BlendParameterY;    // set: a 2D blend (e.g. strafe X / forward Y)
    std::vector<AnimatorBlendClip> BlendClips;
    std::string Layer;          // empty = the base layer
};

enum class AnimatorLayerBlending : uint8_t
{
    Override = 0,   // the layer's pose replaces the layers below on its bones
    Additive,       // the layer's motion, relative to its state's first frame, adds on top of them
};

// A layer runs its own states over part of the body (the bones in Mask, each with everything below
// it), blended over the layers below by Weight. Override layers replace the pose: an upper body that
// aims or waves while the base layer walks. Additive layers add their motion: breathing, flinches,
// leaning, aim offsets, on top of whatever plays below.
struct AnimatorLayer
{
    ME_REFLECTABLE( AnimatorLayer )
public:
    std::string Name;
    std::string DefaultState;   // empty = none until Play()
    float Weight = 1.f;
    std::vector<std::string> Mask;  // empty = the whole body
    AnimatorLayerBlending Blending = AnimatorLayerBlending::Override;
};

enum class AnimatorCondition : uint8_t
{
    Always = 0,     // no parameter: use with ExitTime
    Greater,
    Less,
    True,
    False,
    Trigger,
};

struct AnimatorTransition
{
    ME_REFLECTABLE( AnimatorTransition )
public:
    std::string From;           // empty = from any other state
    std::string To;
    std::string Parameter;
    AnimatorCondition Condition = AnimatorCondition::Always;
    float Threshold = 0.f;
    float Duration = 0.2f;      // cross-fade seconds
    float ExitTime = -1.f;      // >= 0: only once the source state reaches this normalized time
};

// A named marker in a state, fired as an AnimationEvent each time playback passes it.
struct AnimatorEventMarker
{
    ME_REFLECTABLE( AnimatorEventMarker )
public:
    std::string State;
    float Time = 0.f;           // normalized (0..1) within the state's clip
    std::string Name;
};

class AnimationEvent
    : public Event<AnimationEvent>
{
public:
    AnimationEvent()
        : Event()
    {
    }

    EntityHandle Entity;
    std::string State;
    std::string Name;
};

// Plays the animations of the Model on this entity (or of ClipSource) onto the node entities below
// it: bones are entities, so skinned meshes follow them and anything parented to a bone rides along.
// A small state machine picks what plays: states (a clip, or a 1D / 2D blend), transitions on
// parameters and exit times with cross-fades, and event markers. Without states, every clip is a
// state. Layers add masked state machines on top, and root motion can move the entity itself.
class Animator
    : public Component<Animator>
{
    ME_REFLECTABLE( Animator )
    friend class AnimationCore;
public:
    Animator();

    std::string ClipSource;     // optional model file whose clips to use (shared rigs)
    std::string DefaultState;   // empty = the first state
    float Speed = 1.f;
    bool PlayOnStart = true;
    std::vector<AnimatorParameter> Parameters;
    std::vector<AnimatorState> States;
    std::vector<AnimatorTransition> Transitions;
    std::vector<AnimatorEventMarker> Events;
    std::vector<AnimatorLayer> Layers;
    // Root motion: RootBone's horizontal travel moves this entity (through its CharacterController
    // when it has one) instead of the bone, and with RootRotation its turning about the up axis
    // turns the entity. Empty RootBone = the topmost animated node.
    bool ApplyRootMotion = false;
    bool RootRotation = true;
    std::string RootBone;

    // Switches to a state (or, without states, a clip) in that state's layer, cross-fading over
    // InFadeSeconds.
    void Play( const std::string& InState, float InFadeSeconds = 0.f );
    void Stop();
    bool IsPlaying() const;

    void SetFloat( const std::string& InName, float InValue );
    float GetFloat( const std::string& InName ) const;
    void SetBool( const std::string& InName, bool InValue );
    bool GetBool( const std::string& InName ) const;
    void SetTrigger( const std::string& InName );
    void ResetTrigger( const std::string& InName );

    // InLayer: a layer's name; empty = the base layer.
    std::string GetCurrentState( const std::string& InLayer = "" ) const;
    float GetStateTime( const std::string& InLayer = "" ) const;
    // Playback position in the current state's clip, 0..1 (keeps counting past 1 when looping).
    float GetNormalizedTime( const std::string& InLayer = "" ) const;
    bool IsInTransition( const std::string& InLayer = "" ) const;
    void SetLayerWeight( const std::string& InLayer, float InWeight );
    float GetLayerWeight( const std::string& InLayer ) const;
    // Root motion of the last update, in world space (also with ApplyRootMotion off): the travel,
    // and the turn in degrees about world up (positive = clockwise seen from above).
    Vector3 GetRootMotion() const { return m_rootMotionWorld; }
    float GetRootTurn() const { return m_rootTurnDegrees; }
    std::vector<std::string> GetClipNames() const;
    // Plays these clips instead of a model's (procedural or code-built animation). Rebinds.
    void UseClips( SharedPtr<std::vector<Moonlight::AnimationClip>> InClips );

    // Editor preview: in edit mode, poses the model with a state (empty = the default one), playing
    // or held at PreviewTime, without running the state machine, events or root motion. Stopping it,
    // saving the scene or entering play puts the authored pose back, so a preview is never saved.
    void StartPreview( const std::string& InState = "" );
    void StopPreview();
    bool IsPreviewing() const
    {
        return m_previewRequested;
    }
    const std::string& GetPreviewState() const
    {
        return m_previewState;
    }
    float PreviewTime = 0.f;        // seconds into the previewed state
    bool PreviewPlaying = true;
    // The previewed state's length in seconds (0 until it's bound).
    float GetPreviewDuration() const;

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    struct Binding
    {
        EntityHandle Node;
        Vector3 BindPosition;
        Quaternion BindRotation;
        Vector3 BindScale;
        std::vector<int> Channels;  // channel index per clip, -1 = not animated by that clip
    };

    struct Pose
    {
        Vector3 Position;
        Quaternion Rotation;
        Vector3 Scale;
    };

    // One layer's playback (0 = base, then Layers in order).
    struct LayerPlayback
    {
        bool Playing = false;
        int Current = -1;
        float Time = 0.f;
        float TimeBefore = 0.f;         // Time before this frame's advance (root motion)
        int Previous = -1;              // cross-fading from
        float PreviousTime = 0.f;
        float PreviousTimeBefore = 0.f;
        float FadeElapsed = 0.f;
        float FadeDuration = 0.f;
        float Weight = 1.f;
        bool Additive = false;
        std::vector<uint8_t> BoneMask;  // per binding: 1 = this layer drives it
    };

    // Resolved by AnimationCore.
    SharedPtr<ModelResource> m_clipModel;
    SharedPtr<std::vector<Moonlight::AnimationClip>> m_ownClips;
    const std::vector<Moonlight::AnimationClip>* m_clips = nullptr;
    std::vector<AnimatorState> m_states;    // States, or one per clip
    std::vector<Binding> m_bindings;
    std::vector<Pose> m_pose;
    bool m_bound = false;

    // Playback
    std::vector<LayerPlayback> m_layers;
    std::vector<std::pair<std::string, float>> m_pendingPlays;     // Play() before / between updates
    std::vector<std::pair<std::string, float>> m_pendingWeights;   // SetLayerWeight() before binding
    int m_rootBinding = -1;
    Vector3 m_rootMotion;           // last update, in the root bone's parent space (from SamplePose)
    Vector3 m_rootMotionWorld;
    float m_rootTurn = 0.f;         // last update, radians about m_rootUp (from SamplePose)
    float m_rootTurnDegrees = 0.f;
    Vector3 m_rootUp = Vector3( 0.f, 1.f, 0.f );     // world up in the root bone's parent space
    // Editor preview
    bool m_previewRequested = false;
    bool m_previewBound = false;    // bound by a preview (edit mode): restore the bind pose when it ends
    int m_previewIdleUpdates = 0;   // updates since the inspector last showed this animator
    std::string m_previewState;

    int FindState( const std::string& InName ) const;
    int FindLayer( const std::string& InName ) const;   // -1 = unknown; "" = 0
    int StateLayer( int InState ) const;
    AnimatorParameter* FindParameter( const std::string& InName );
    const AnimatorParameter* FindParameter( const std::string& InName ) const;
    int FindClip( const std::string& InName ) const;
    float StateDuration( int InState ) const;
    void BeginState( int InState, float InFade );
    const LayerPlayback* GetLayer( const std::string& InLayer ) const;
    void Unbind();

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( Animator, "Animation" )
