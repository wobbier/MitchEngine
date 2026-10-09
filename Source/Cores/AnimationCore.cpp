#include "PCH.h"
#include "AnimationCore.h"
#include "Components/Animation/Animator.h"
#include "Components/Graphics/Model.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Engine/Engine.h"
#include "Graphics/ModelResource.h"
#include "Jobs/JobSystem.h"
#include "Resource/ResourceCache.h"
#include "Scene/AnimationClip.h"
#include "optick.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
    float ClipTime( float InTime, float InDuration, bool InLoop )
    {
        if( InDuration <= 0.f )
        {
            return 0.f;
        }
        if( InLoop )
        {
            return InTime - std::floor( InTime / InDuration ) * InDuration;
        }
        return std::clamp( InTime, 0.f, InDuration );
    }

    void CollectNodes( Transform& InNode, std::unordered_map<std::string, Transform*>& OutNodes )
    {
        for( Transform* child : InNode.GetChildren() )
        {
            if( child )
            {
                OutNodes.emplace( child->GetName(), child );   // first match wins (ancestors before descendants)
                CollectNodes( *child, OutNodes );
            }
        }
    }
}


AnimationCore::AnimationCore()
    : Base( ComponentFilter().Requires<Transform>().Requires<Animator>() )
{
    SetIsSerializable( false );
}


void AnimationCore::OnStart()
{
    m_running = true;
    // Bind afresh at play start: the bind pose is the scene as it was authored.
    for( Entity& entity : GetEntities() )
    {
        entity.GetComponent<Animator>().Unbind();
    }
}


void AnimationCore::OnStop()
{
    m_running = false;
}


void AnimationCore::Update( const UpdateContext& inUpdateContext )
{
    if( m_running )
    {
        Advance( inUpdateContext.GetDeltaTime() );
    }
}


void AnimationCore::Bind( Entity& InEntity, Animator& InAnimator )
{
    InAnimator.m_bound = true;
    if( InAnimator.m_ownClips )
    {
        InAnimator.m_clips = InAnimator.m_ownClips.get();
    }
    else
    {
        SharedPtr<ModelResource> model;
        if( !InAnimator.ClipSource.empty() )
        {
            model = ResourceCache::GetInstance().Get<ModelResource>( Path( InAnimator.ClipSource ) );
        }
        else if( Model* component = InEntity.TryGetComponent<Model>() )
        {
            model = component->ModelHandle;
        }
        if( !model )
        {
            return;
        }
        InAnimator.m_clipModel = model;
        InAnimator.m_clips = &model->GetAnimations();
    }
    if( InAnimator.m_clips->empty() )
    {
        InAnimator.m_clips = nullptr;
        return;
    }
    const std::vector<Moonlight::AnimationClip>& clips = *InAnimator.m_clips;

    InAnimator.m_states = InAnimator.States;
    if( InAnimator.m_states.empty() )
    {
        for( const Moonlight::AnimationClip& clip : clips )
        {
            AnimatorState state;
            state.Name = clip.Name;
            state.Clip = clip.Name;
            InAnimator.m_states.push_back( state );
        }
    }

    // Every node any clip animates, found by name below the Animator.
    std::unordered_map<std::string, Transform*> nodes;
    Transform& root = InEntity.GetComponent<Transform>();
    nodes.emplace( root.GetName(), &root );
    CollectNodes( root, nodes );
    std::unordered_map<std::string, size_t> bindingByName;
    for( size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex )
    {
        const Moonlight::AnimationClip& clip = clips[clipIndex];
        for( size_t channelIndex = 0; channelIndex < clip.Channels.size(); ++channelIndex )
        {
            const std::string& name = clip.Channels[channelIndex].NodeName;
            auto node = nodes.find( name );
            if( node == nodes.end() )
            {
                continue;
            }
            auto existing = bindingByName.find( name );
            if( existing == bindingByName.end() )
            {
                Animator::Binding binding;
                binding.Node = node->second->Parent;
                binding.BindPosition = node->second->GetPosition();
                binding.BindRotation = node->second->GetRotation();
                binding.BindScale = node->second->GetScale();
                binding.Channels.assign( clips.size(), -1 );
                existing = bindingByName.emplace( name, InAnimator.m_bindings.size() ).first;
                InAnimator.m_bindings.push_back( std::move( binding ) );
            }
            InAnimator.m_bindings[existing->second].Channels[clipIndex] = static_cast<int>( channelIndex );
        }
    }
    InAnimator.m_pose.resize( InAnimator.m_bindings.size() );

    if( InAnimator.PlayOnStart && !InAnimator.m_hasPendingPlay && !InAnimator.m_states.empty() )
    {
        const int state = InAnimator.DefaultState.empty() ? 0 : InAnimator.FindState( InAnimator.DefaultState );
        InAnimator.BeginState( std::max( state, 0 ), 0.f );
    }
}


void AnimationCore::StepStateMachine( Entity& InEntity, Animator& InAnimator, float InDeltaSeconds )
{
    Animator& a = InAnimator;
    if( a.m_hasPendingPlay )
    {
        a.m_hasPendingPlay = false;
        int state = a.FindState( a.m_pendingPlay );
        if( state < 0 && a.FindClip( a.m_pendingPlay ) >= 0 )
        {
            // A clip without a state of its own: play it as one.
            AnimatorState implicit;
            implicit.Name = a.m_pendingPlay;
            implicit.Clip = a.m_pendingPlay;
            a.m_states.push_back( implicit );
            state = static_cast<int>( a.m_states.size() ) - 1;
        }
        if( state >= 0 )
        {
            a.BeginState( state, a.m_pendingFade );
        }
    }
    if( !a.m_playing || a.m_current < 0 )
    {
        return;
    }

    // Transitions (one at a time: not while cross-fading).
    if( a.m_previous < 0 )
    {
        const std::string& current = a.m_states[a.m_current].Name;
        const float normalized = a.GetNormalizedTime();
        for( const AnimatorTransition& transition : a.Transitions )
        {
            if( ( !transition.From.empty() && transition.From != current ) || transition.To == current )
            {
                continue;
            }
            const int target = a.FindState( transition.To );
            if( target < 0 || ( transition.ExitTime >= 0.f && normalized < transition.ExitTime ) )
            {
                continue;
            }
            AnimatorParameter* parameter = a.FindParameter( transition.Parameter );
            bool pass = false;
            switch( transition.Condition )
            {
            case AnimatorCondition::Always:
                pass = true;
                break;
            case AnimatorCondition::Greater:
                pass = parameter && parameter->Value > transition.Threshold;
                break;
            case AnimatorCondition::Less:
                pass = parameter && parameter->Value < transition.Threshold;
                break;
            case AnimatorCondition::True:
                pass = parameter && parameter->Value != 0.f;
                break;
            case AnimatorCondition::False:
                pass = !parameter || parameter->Value == 0.f;
                break;
            case AnimatorCondition::Trigger:
                pass = parameter && parameter->Value != 0.f;
                if( pass )
                {
                    parameter->Value = 0.f;   // consumed
                }
                break;
            }
            if( pass )
            {
                a.BeginState( target, transition.Duration );
                break;
            }
        }
    }

    // Advance, firing event markers passed on the way.
    const AnimatorState& state = a.m_states[a.m_current];
    const float duration = a.StateDuration( a.m_current );
    const float before = a.m_time;
    a.m_time += InDeltaSeconds * a.Speed * state.Speed;
    if( !state.Loop && duration > 0.f )
    {
        a.m_time = std::clamp( a.m_time, 0.f, duration );
    }
    if( duration > 0.f && a.m_time > before )
    {
        const float from = before / duration;
        const float to = a.m_time / duration;
        for( const AnimatorEventMarker& marker : a.Events )
        {
            if( marker.State != state.Name )
            {
                continue;
            }
            for( float lap = std::floor( from ); lap <= std::floor( to ); lap += 1.f )
            {
                const float at = lap + marker.Time;
                if( at > from && at <= to )
                {
                    AnimationEvent event;
                    event.Entity = InEntity.GetHandle();
                    event.State = state.Name;
                    event.Name = marker.Name;
                    event.Fire();
                }
            }
        }
    }

    if( a.m_previous >= 0 )
    {
        const AnimatorState& previous = a.m_states[a.m_previous];
        a.m_previousTime += InDeltaSeconds * a.Speed * previous.Speed;
        a.m_fadeElapsed += InDeltaSeconds;
        if( a.m_fadeElapsed >= a.m_fadeDuration )
        {
            a.m_previous = -1;
        }
    }
}


void AnimationCore::SamplePose( Animator& InAnimator )
{
    Animator& a = InAnimator;
    const std::vector<Moonlight::AnimationClip>& clips = *a.m_clips;

    struct ClipSample
    {
        int Clip = -1;
        float Time = 0.f;
        float Weight = 0.f;
    };
    // The (up to two) weighted clips a state plays at a time.
    auto resolve = [&a, &clips]( int InState, float InTime, ClipSample OutSamples[2] ) {
        const AnimatorState& state = a.m_states[InState];
        if( state.BlendClips.empty() )
        {
            const int clip = a.FindClip( state.Clip );
            if( clip >= 0 )
            {
                OutSamples[0] = { clip, ClipTime( InTime, clips[clip].Duration, state.Loop ), 1.f };
            }
            return;
        }
        // 1D blend: clips stay in phase (normalized to the first clip's length).
        std::vector<const AnimatorBlendClip*> sorted;
        for( const AnimatorBlendClip& entry : state.BlendClips )
        {
            sorted.push_back( &entry );
        }
        std::sort( sorted.begin(), sorted.end(), []( const AnimatorBlendClip* x, const AnimatorBlendClip* y ) { return x->Threshold < y->Threshold; } );
        const float value = a.GetFloat( state.BlendParameter );
        size_t upper = 0;
        while( upper < sorted.size() && sorted[upper]->Threshold < value )
        {
            ++upper;
        }
        const size_t first = upper == 0 ? 0 : std::min( upper - 1, sorted.size() - 1 );
        const size_t second = std::min( upper, sorted.size() - 1 );
        const float span = sorted[second]->Threshold - sorted[first]->Threshold;
        const float t = first == second || span <= 0.f ? 0.f : std::clamp( ( value - sorted[first]->Threshold ) / span, 0.f, 1.f );
        const float reference = a.StateDuration( InState );
        const float phase = reference > 0.f ? InTime / reference : 0.f;
        const size_t picks[2] = { first, second };
        const float weights[2] = { 1.f - t, t };
        for( int i = 0; i < 2; ++i )
        {
            const int clip = a.FindClip( sorted[picks[i]]->Clip );
            if( clip >= 0 )
            {
                const float duration = clips[clip].Duration;
                OutSamples[i] = { clip, ClipTime( phase * duration, duration, state.Loop ), weights[i] };
            }
        }
    };

    auto blend = []( Animator::Pose& InOutA, const Animator::Pose& InB, float InWeight ) {
        InOutA.Position = InOutA.Position + ( InB.Position - InOutA.Position ) * InWeight;
        InOutA.Scale = InOutA.Scale + ( InB.Scale - InOutA.Scale ) * InWeight;
        InOutA.Rotation = Quaternion::Slerp( InOutA.Rotation, InB.Rotation, InWeight );
    };

    // A binding's pose for a state: its bind pose, overridden by the state's clips.
    auto statePose = [&]( const Animator::Binding& InBinding, const ClipSample InSamples[2] ) {
        Animator::Pose result{ InBinding.BindPosition, InBinding.BindRotation, InBinding.BindScale };
        bool first = true;
        for( int i = 0; i < 2; ++i )
        {
            const ClipSample& sample = InSamples[i];
            if( sample.Clip < 0 || sample.Weight <= 0.f )
            {
                continue;
            }
            Animator::Pose pose{ InBinding.BindPosition, InBinding.BindRotation, InBinding.BindScale };
            const int channel = InBinding.Channels[sample.Clip];
            if( channel >= 0 )
            {
                Moonlight::AnimationClip::Sample( clips[sample.Clip].Channels[channel], sample.Time, pose.Position, pose.Rotation, pose.Scale );
            }
            if( first )
            {
                result = pose;
                first = false;
            }
            else
            {
                blend( result, pose, sample.Weight / ( InSamples[0].Weight + sample.Weight ) );
            }
        }
        return result;
    };

    ClipSample current[2];
    resolve( a.m_current, a.m_time, current );
    ClipSample previous[2];
    float fade = 1.f;
    if( a.m_previous >= 0 )
    {
        resolve( a.m_previous, a.m_previousTime, previous );
        fade = a.m_fadeDuration > 0.f ? std::clamp( a.m_fadeElapsed / a.m_fadeDuration, 0.f, 1.f ) : 1.f;
        fade = fade * fade * ( 3.f - 2.f * fade );   // smoothstep
    }
    for( size_t i = 0; i < a.m_bindings.size(); ++i )
    {
        const Animator::Binding& binding = a.m_bindings[i];
        Animator::Pose pose = statePose( binding, current );
        if( a.m_previous >= 0 && fade < 1.f )
        {
            Animator::Pose from = statePose( binding, previous );
            blend( from, pose, fade );
            pose = from;
        }
        a.m_pose[i] = pose;
    }
}


void AnimationCore::WritePose( Animator& InAnimator )
{
    for( size_t i = 0; i < InAnimator.m_bindings.size(); ++i )
    {
        const Animator::Binding& binding = InAnimator.m_bindings[i];
        if( !binding.Node )
        {
            continue;
        }
        Transform& transform = binding.Node->GetComponent<Transform>();
        const Animator::Pose& pose = InAnimator.m_pose[i];
        transform.SetPosition( pose.Position );
        transform.SetRotation( pose.Rotation );
        transform.SetScale( pose.Scale );
    }
}


void AnimationCore::Advance( float InDeltaSeconds )
{
    OPTICK_EVENT( "AnimationCore::Advance" );
    std::vector<Animator*> active;
    for( Entity& entity : GetEntities() )
    {
        Animator* animator = entity.TryGetComponent<Animator>();
        if( !animator || !animator->IsEnabled() || !entity.IsActiveInHierarchy() )
        {
            continue;
        }
        if( !animator->m_bound )
        {
            Bind( entity, *animator );
        }
        if( !animator->m_clips )
        {
            continue;
        }
        StepStateMachine( entity, *animator, InDeltaSeconds );
        if( animator->m_playing && animator->m_current >= 0 && !animator->m_bindings.empty() )
        {
            active.push_back( animator );
        }
    }

    Jobs::JobSystem::Get().ParallelFor( static_cast<uint32_t>( active.size() ), 1, [&active]( uint32_t begin, uint32_t end ) {
        for( uint32_t i = begin; i < end; ++i )
        {
            SamplePose( *active[i] );
        }
    } );
    for( Animator* animator : active )
    {
        WritePose( *animator );
    }
}
