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

// One clip of a 1D blend: the state plays the two clips around its blend parameter's value.
struct AnimatorBlendClip
{
    ME_REFLECTABLE( AnimatorBlendClip )
public:
    std::string Clip;
    float Threshold = 0.f;
};

struct AnimatorState
{
    ME_REFLECTABLE( AnimatorState )
public:
    std::string Name;
    std::string Clip;           // or a 1D blend over BlendClips by BlendParameter
    float Speed = 1.f;
    bool Loop = true;
    std::string BlendParameter;
    std::vector<AnimatorBlendClip> BlendClips;
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
// A small state machine picks what plays: states (a clip or a 1D blend), transitions on parameters
// and exit times with cross-fades, and event markers. Without states, every clip is a state.
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

    // Switches to a state (or, without states, a clip), cross-fading over InFadeSeconds.
    void Play( const std::string& InState, float InFadeSeconds = 0.f );
    void Stop();
    bool IsPlaying() const { return m_playing; }

    void SetFloat( const std::string& InName, float InValue );
    float GetFloat( const std::string& InName ) const;
    void SetBool( const std::string& InName, bool InValue );
    bool GetBool( const std::string& InName ) const;
    void SetTrigger( const std::string& InName );
    void ResetTrigger( const std::string& InName );

    std::string GetCurrentState() const;
    float GetStateTime() const { return m_time; }
    // Playback position in the current state's clip, 0..1 (keeps counting past 1 when looping).
    float GetNormalizedTime() const;
    bool IsInTransition() const { return m_previous >= 0; }
    std::vector<std::string> GetClipNames() const;
    // Plays these clips instead of a model's (procedural or code-built animation). Rebinds.
    void UseClips( SharedPtr<std::vector<Moonlight::AnimationClip>> InClips );

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

    // Resolved by AnimationCore.
    SharedPtr<ModelResource> m_clipModel;
    SharedPtr<std::vector<Moonlight::AnimationClip>> m_ownClips;
    const std::vector<Moonlight::AnimationClip>* m_clips = nullptr;
    std::vector<AnimatorState> m_states;    // States, or one per clip
    std::vector<Binding> m_bindings;
    std::vector<Pose> m_pose;
    bool m_bound = false;

    // Playback
    bool m_playing = false;
    int m_current = -1;
    float m_time = 0.f;
    int m_previous = -1;
    float m_previousTime = 0.f;
    float m_fadeElapsed = 0.f;
    float m_fadeDuration = 0.f;
    std::string m_pendingPlay;
    float m_pendingFade = 0.f;
    bool m_hasPendingPlay = false;

    int FindState( const std::string& InName ) const;
    AnimatorParameter* FindParameter( const std::string& InName );
    const AnimatorParameter* FindParameter( const std::string& InName ) const;
    int FindClip( const std::string& InName ) const;
    float StateDuration( int InState ) const;
    void BeginState( int InState, float InFade );
    void Unbind();

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( Animator, "Animation" )
