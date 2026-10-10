#include "PCH.h"
#include "AnimationCore.h"
#include "Components/Animation/Animator.h"
#include "Components/Graphics/Model.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Engine/Engine.h"
#include "Graphics/ModelResource.h"
#include "Jobs/JobSystem.h"
#include "Resource/ResourceCache.h"
#include "Scene/AnimationClip.h"
#include "optick.h"
#include <algorithm>
#include <array>
#include <climits>
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

    // The signed angle (radians, -pi..pi) of a rotation's twist about a unit axis (swing-twist).
    float TwistAngle( const Quaternion& InRotation, const Vector3& InAxis )
    {
        const float projection = InRotation.x * InAxis.x + InRotation.y * InAxis.y + InRotation.z * InAxis.z;
        float angle = 2.f * std::atan2( projection, InRotation.w );
        if( angle > glm::pi<float>() )
        {
            angle -= glm::two_pi<float>();
        }
        else if( angle < -glm::pi<float>() )
        {
            angle += glm::two_pi<float>();
        }
        return angle;
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
    // Previews end: the play bind pose is the scene as authored.
    StopAllPreviews();
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
    else
    {
        UpdatePreviews( inUpdateContext.GetUnscaledDeltaTime() );
    }
}


void AnimationCore::WriteBindPose( Animator& InAnimator )
{
    for( const Animator::Binding& binding : InAnimator.m_bindings )
    {
        if( Transform* transform = binding.Node ? binding.Node->TryGetComponent<Transform>() : nullptr )
        {
            transform->SetPosition( binding.BindPosition );
            transform->SetRotation( binding.BindRotation );
            transform->SetScale( binding.BindScale );
        }
    }
}


void AnimationCore::EndPreview( Animator& InAnimator )
{
    InAnimator.m_previewRequested = false;
    if( InAnimator.m_previewBound )
    {
        WriteBindPose( InAnimator );
        InAnimator.Unbind();
        InAnimator.m_previewBound = false;
    }
}


void AnimationCore::RestorePreviewPoses()
{
    for( Entity& entity : GetEntities() )
    {
        Animator& animator = entity.GetComponent<Animator>();
        if( animator.m_previewBound )
        {
            WriteBindPose( animator );
        }
    }
}


void AnimationCore::StopAllPreviews()
{
    for( Entity& entity : GetEntities() )
    {
        EndPreview( entity.GetComponent<Animator>() );
    }
}


void AnimationCore::UpdatePreviews( float InDeltaSeconds )
{
    OPTICK_EVENT( "AnimationCore::UpdatePreviews" );
    std::vector<Animator*> previewing;
    for( Entity& entity : GetEntities() )
    {
        Animator& a = entity.GetComponent<Animator>();
        // A preview lives while its inspector is shown (selecting something else ends it).
        if( a.m_previewRequested && ++a.m_previewIdleUpdates > kPreviewIdleUpdates )
        {
            a.m_previewRequested = false;
        }
        if( !a.m_previewRequested || !a.IsEnabled() || !entity.IsActiveInHierarchy() )
        {
            EndPreview( a );
            continue;
        }
        if( !a.m_bound )
        {
            Bind( entity, a );
            a.m_previewBound = true;
        }
        if( !a.m_clips || a.m_states.empty() || a.m_bindings.empty() )
        {
            continue;
        }
        int state = a.m_previewState.empty() ? -1 : a.FindState( a.m_previewState );
        if( state < 0 )
        {
            state = a.DefaultState.empty() ? 0 : std::max( a.FindState( a.DefaultState ), 0 );
            a.m_previewState = a.m_states[state].Name;
        }
        // Just this state, on its layer: no transitions, events or cross-fades.
        const int layerIndex = a.StateLayer( state );
        for( size_t i = 0; i < a.m_layers.size(); ++i )
        {
            Animator::LayerPlayback& layer = a.m_layers[i];
            layer.Playing = static_cast<int>( i ) == layerIndex;
            layer.Previous = -1;
        }
        const float duration = a.StateDuration( state );
        if( a.PreviewPlaying )
        {
            a.PreviewTime += InDeltaSeconds * a.Speed * a.m_states[state].Speed;
        }
        if( duration > 0.f )
        {
            a.PreviewTime = a.m_states[state].Loop ? a.PreviewTime - std::floor( a.PreviewTime / duration ) * duration : std::clamp( a.PreviewTime, 0.f, duration );
        }
        Animator::LayerPlayback& layer = a.m_layers[layerIndex];
        layer.Current = state;
        layer.Time = a.PreviewTime;
        layer.TimeBefore = a.PreviewTime;
        previewing.push_back( &a );
    }
    for( Animator* animator : previewing )
    {
        SamplePose( *animator );
        WritePose( *animator );   // root motion stays in place: nothing moves the entity
    }
}


void AnimationCore::Bind( Entity& InEntity, Animator& InAnimator )
{
    // A model still loading in the background has neither clips nor bone entities yet: try again
    // next update.
    if( Model* model = InEntity.TryGetComponent<Model>(); model && model->ModelHandle && !model->IsReady() && !model->ModelHandle->HasLoadFailed() )
    {
        return;
    }
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
    Animator& a = InAnimator;

    // Layers: the base layer drives every node; the others the nodes under their mask's bones.
    a.m_layers.assign( a.Layers.size() + 1, Animator::LayerPlayback() );
    for( size_t layerIndex = 0; layerIndex < a.m_layers.size(); ++layerIndex )
    {
        Animator::LayerPlayback& layer = a.m_layers[layerIndex];
        layer.BoneMask.assign( a.m_bindings.size(), 1 );
        if( layerIndex == 0 )
        {
            continue;
        }
        const AnimatorLayer& settings = a.Layers[layerIndex - 1];
        layer.Weight = std::clamp( settings.Weight, 0.f, 1.f );
        layer.Additive = settings.Blending == AnimatorLayerBlending::Additive;
        if( settings.Mask.empty() )
        {
            continue;
        }
        for( size_t i = 0; i < a.m_bindings.size(); ++i )
        {
            bool inMask = false;
            for( Transform* node = a.m_bindings[i].Node ? &a.m_bindings[i].Node->GetComponent<Transform>() : nullptr; node && !inMask; node = node->GetParentTransform() )
            {
                inMask = std::find( settings.Mask.begin(), settings.Mask.end(), node->GetName() ) != settings.Mask.end();
                if( node == &root )
                {
                    break;
                }
            }
            layer.BoneMask[i] = inMask ? 1 : 0;
        }
    }
    for( const auto& [name, weight] : a.m_pendingWeights )
    {
        a.SetLayerWeight( name, weight );
    }
    a.m_pendingWeights.clear();

    // Root motion: RootBone, or the animated node closest to the Animator.
    a.m_rootBinding = -1;
    int bestDepth = INT_MAX;
    for( size_t i = 0; i < a.m_bindings.size(); ++i )
    {
        if( !a.m_bindings[i].Node )
        {
            continue;
        }
        Transform& node = a.m_bindings[i].Node->GetComponent<Transform>();
        if( !a.RootBone.empty() )
        {
            if( node.GetName() == a.RootBone )
            {
                a.m_rootBinding = static_cast<int>( i );
                break;
            }
            continue;
        }
        int depth = 0;
        for( Transform* parent = node.GetParentTransform(); parent && parent != &root; parent = parent->GetParentTransform() )
        {
            ++depth;
        }
        if( depth < bestDepth )
        {
            bestDepth = depth;
            a.m_rootBinding = static_cast<int>( i );
        }
    }
    if( a.m_rootBinding >= 0 )
    {
        Transform* parent = a.m_bindings[a.m_rootBinding].Node->GetComponent<Transform>().GetParentTransform();
        if( parent )
        {
            const glm::mat4 toLocal = glm::inverse( parent->GetLocalToWorldMatrix().GetInternalMatrix() );
            const glm::vec3 up = glm::vec3( toLocal * glm::vec4( 0.f, 1.f, 0.f, 0.f ) );
            a.m_rootUp = glm::length( up ) > 0.f ? Vector3( glm::normalize( up ) ) : Vector3( 0.f, 1.f, 0.f );
        }
    }

    if( a.PlayOnStart && !a.m_states.empty() )
    {
        if( a.m_pendingPlays.empty() )
        {
            int state = a.DefaultState.empty() ? -1 : a.FindState( a.DefaultState );
            for( size_t i = 0; state < 0 && i < a.m_states.size(); ++i )
            {
                state = a.StateLayer( static_cast<int>( i ) ) == 0 ? static_cast<int>( i ) : -1;
            }
            a.BeginState( std::max( state, 0 ), 0.f );
        }
        for( const AnimatorLayer& layer : a.Layers )
        {
            const int state = layer.DefaultState.empty() ? -1 : a.FindState( layer.DefaultState );
            if( state >= 0 )
            {
                a.BeginState( state, 0.f );
            }
        }
    }
}


void AnimationCore::StepStateMachine( Entity& InEntity, Animator& InAnimator, float InDeltaSeconds )
{
    Animator& a = InAnimator;
    for( const auto& [name, fade] : a.m_pendingPlays )
    {
        int state = a.FindState( name );
        if( state < 0 && a.FindClip( name ) >= 0 )
        {
            // A clip without a state of its own: play it as one (on the base layer).
            AnimatorState implicit;
            implicit.Name = name;
            implicit.Clip = name;
            a.m_states.push_back( implicit );
            state = static_cast<int>( a.m_states.size() ) - 1;
        }
        if( state >= 0 )
        {
            a.BeginState( state, fade );
        }
    }
    a.m_pendingPlays.clear();

    for( size_t layerIndex = 0; layerIndex < a.m_layers.size(); ++layerIndex )
    {
        Animator::LayerPlayback& layer = a.m_layers[layerIndex];
        layer.TimeBefore = layer.Time;
        layer.PreviousTimeBefore = layer.PreviousTime;
        if( !layer.Playing || layer.Current < 0 )
        {
            continue;
        }

        // Transitions into this layer's states (one at a time: not while cross-fading).
        if( layer.Previous < 0 )
        {
            const std::string& current = a.m_states[layer.Current].Name;
            const float currentDuration = a.StateDuration( layer.Current );
            const float normalized = currentDuration > 0.f ? layer.Time / currentDuration : 0.f;
            for( const AnimatorTransition& transition : a.Transitions )
            {
                if( ( !transition.From.empty() && transition.From != current ) || transition.To == current )
                {
                    continue;
                }
                const int target = a.FindState( transition.To );
                if( target < 0 || a.StateLayer( target ) != static_cast<int>( layerIndex ) || ( transition.ExitTime >= 0.f && normalized < transition.ExitTime ) )
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
        const AnimatorState& state = a.m_states[layer.Current];
        const float duration = a.StateDuration( layer.Current );
        const float before = layer.Time;
        layer.TimeBefore = before;
        layer.Time += InDeltaSeconds * a.Speed * state.Speed;
        if( !state.Loop && duration > 0.f )
        {
            layer.Time = std::clamp( layer.Time, 0.f, duration );
        }
        if( duration > 0.f && layer.Time > before )
        {
            const float from = before / duration;
            const float to = layer.Time / duration;
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

        if( layer.Previous >= 0 )
        {
            const AnimatorState& previous = a.m_states[layer.Previous];
            layer.PreviousTimeBefore = layer.PreviousTime;
            layer.PreviousTime += InDeltaSeconds * a.Speed * previous.Speed;
            if( !previous.Loop )
            {
                layer.PreviousTime = std::min( layer.PreviousTime, a.StateDuration( layer.Previous ) );
            }
            layer.FadeElapsed += InDeltaSeconds;
            if( layer.FadeElapsed >= layer.FadeDuration )
            {
                layer.Previous = -1;
            }
        }
    }
}


namespace
{
    // One weighted clip of a state at a point in time (and where it was a frame ago, for root motion).
    struct ClipSample
    {
        int Clip = -1;
        float Time = 0.f;           // unwrapped
        float TimeBefore = 0.f;     // unwrapped
        float Weight = 0.f;
    };

    constexpr int kMaxSamples = 8;

    struct StateSamples
    {
        std::array<ClipSample, kMaxSamples> Items;
        int Count = 0;
        bool Loop = true;
    };

    // Gradient band interpolation (2D freeform blends): each sample's weight falls off towards
    // every other sample, so weights stay local and sum to one after normalizing.
    void GradientBandWeights( const std::vector<std::array<float, 2>>& InPoints, float InX, float InY, std::vector<float>& OutWeights )
    {
        const size_t count = InPoints.size();
        OutWeights.assign( count, 0.f );
        float total = 0.f;
        for( size_t i = 0; i < count; ++i )
        {
            float weight = 1.f;
            const float px = InX - InPoints[i][0];
            const float py = InY - InPoints[i][1];
            for( size_t j = 0; j < count; ++j )
            {
                if( i == j )
                {
                    continue;
                }
                const float vx = InPoints[j][0] - InPoints[i][0];
                const float vy = InPoints[j][1] - InPoints[i][1];
                const float lengthSquared = vx * vx + vy * vy;
                if( lengthSquared <= 1e-8f )
                {
                    continue;
                }
                weight = std::min( weight, std::clamp( 1.f - ( px * vx + py * vy ) / lengthSquared, 0.f, 1.f ) );
            }
            OutWeights[i] = weight;
            total += weight;
        }
        if( total <= 1e-6f )
        {
            // Outside every band: the nearest sample.
            size_t nearest = 0;
            float best = FLT_MAX;
            for( size_t i = 0; i < count; ++i )
            {
                const float dx = InX - InPoints[i][0];
                const float dy = InY - InPoints[i][1];
                if( dx * dx + dy * dy < best )
                {
                    best = dx * dx + dy * dy;
                    nearest = i;
                }
            }
            OutWeights[nearest] = 1.f;
            return;
        }
        for( float& weight : OutWeights )
        {
            weight /= total;
        }
    }
}


void AnimationCore::SamplePose( Animator& InAnimator )
{
    Animator& a = InAnimator;
    const std::vector<Moonlight::AnimationClip>& clips = *a.m_clips;

    // The weighted clips a state plays at InTime (blends stay in phase, normalized to the first
    // clip's length).
    auto resolve = [&a, &clips]( int InState, float InTime, float InTimeBefore ) {
        StateSamples out;
        const AnimatorState& state = a.m_states[InState];
        out.Loop = state.Loop;
        if( state.BlendClips.empty() )
        {
            const int clip = a.FindClip( state.Clip );
            if( clip >= 0 )
            {
                out.Items[0] = { clip, InTime, InTimeBefore, 1.f };
                out.Count = 1;
            }
            return out;
        }

        std::vector<float> weights( state.BlendClips.size(), 0.f );
        if( state.BlendParameterY.empty() )
        {
            // 1D: the two clips around the parameter.
            std::vector<size_t> order( state.BlendClips.size() );
            for( size_t i = 0; i < order.size(); ++i )
            {
                order[i] = i;
            }
            std::sort( order.begin(), order.end(), [&state]( size_t x, size_t y ) { return state.BlendClips[x].Threshold < state.BlendClips[y].Threshold; } );
            const float value = a.GetFloat( state.BlendParameter );
            size_t upper = 0;
            while( upper < order.size() && state.BlendClips[order[upper]].Threshold < value )
            {
                ++upper;
            }
            const size_t first = upper == 0 ? 0 : std::min( upper - 1, order.size() - 1 );
            const size_t second = std::min( upper, order.size() - 1 );
            const float span = state.BlendClips[order[second]].Threshold - state.BlendClips[order[first]].Threshold;
            const float t = first == second || span <= 0.f ? 0.f : std::clamp( ( value - state.BlendClips[order[first]].Threshold ) / span, 0.f, 1.f );
            weights[order[first]] += 1.f - t;
            weights[order[second]] += t;
        }
        else
        {
            std::vector<std::array<float, 2>> points;
            for( const AnimatorBlendClip& entry : state.BlendClips )
            {
                points.push_back( { entry.Threshold, entry.ThresholdY } );
            }
            GradientBandWeights( points, a.GetFloat( state.BlendParameter ), a.GetFloat( state.BlendParameterY ), weights );
        }

        // The strongest few, renormalized.
        std::vector<size_t> ranked;
        for( size_t i = 0; i < weights.size(); ++i )
        {
            if( weights[i] > 1e-4f && a.FindClip( state.BlendClips[i].Clip ) >= 0 )
            {
                ranked.push_back( i );
            }
        }
        std::sort( ranked.begin(), ranked.end(), [&weights]( size_t x, size_t y ) { return weights[x] > weights[y]; } );
        if( ranked.size() > static_cast<size_t>( kMaxSamples ) )
        {
            ranked.resize( kMaxSamples );
        }
        float total = 0.f;
        for( size_t index : ranked )
        {
            total += weights[index];
        }
        const float reference = a.StateDuration( InState );
        for( size_t index : ranked )
        {
            const int clip = a.FindClip( state.BlendClips[index].Clip );
            const float scale = reference > 0.f ? clips[clip].Duration / reference : 1.f;
            out.Items[out.Count++] = { clip, InTime * scale, InTimeBefore * scale, total > 0.f ? weights[index] / total : 0.f };
        }
        return out;
    };

    auto blend = []( Animator::Pose& InOutA, const Animator::Pose& InB, float InWeight ) {
        InOutA.Position = InOutA.Position + ( InB.Position - InOutA.Position ) * InWeight;
        InOutA.Scale = InOutA.Scale + ( InB.Scale - InOutA.Scale ) * InWeight;
        InOutA.Rotation = Quaternion::Slerp( InOutA.Rotation, InB.Rotation, InWeight );
    };

    // A binding's pose for a state: its bind pose, overridden by the state's clips.
    auto statePose = [&]( const Animator::Binding& InBinding, const StateSamples& InSamples ) {
        Animator::Pose result{ InBinding.BindPosition, InBinding.BindRotation, InBinding.BindScale };
        float accumulated = 0.f;
        for( int i = 0; i < InSamples.Count; ++i )
        {
            const ClipSample& sample = InSamples.Items[i];
            if( sample.Weight <= 0.f )
            {
                continue;
            }
            Animator::Pose pose{ InBinding.BindPosition, InBinding.BindRotation, InBinding.BindScale };
            const int channel = InBinding.Channels[sample.Clip];
            if( channel >= 0 )
            {
                Moonlight::AnimationClip::Sample( clips[sample.Clip].Channels[channel], ClipTime( sample.Time, clips[sample.Clip].Duration, InSamples.Loop ), pose.Position, pose.Rotation, pose.Scale );
            }
            accumulated += sample.Weight;
            if( accumulated <= sample.Weight )
            {
                result = pose;
            }
            else
            {
                blend( result, pose, sample.Weight / accumulated );
            }
        }
        return result;
    };

    // A layer's pose for a binding: its current state, cross-faded from the previous one. Additive
    // layers also sample each state's first frame, the reference their motion is measured from.
    struct LayerSamples
    {
        StateSamples Current;
        StateSamples Previous;
        StateSamples CurrentReference;
        StateSamples PreviousReference;
        float Fade = 1.f;
        bool Active = false;
    };
    std::vector<LayerSamples> layers( a.m_layers.size() );
    for( size_t i = 0; i < a.m_layers.size(); ++i )
    {
        const Animator::LayerPlayback& playback = a.m_layers[i];
        if( !playback.Playing || playback.Current < 0 )
        {
            continue;
        }
        LayerSamples& samples = layers[i];
        samples.Active = true;
        samples.Current = resolve( playback.Current, playback.Time, playback.TimeBefore );
        if( playback.Additive )
        {
            samples.CurrentReference = resolve( playback.Current, 0.f, 0.f );
        }
        if( playback.Previous >= 0 )
        {
            samples.Previous = resolve( playback.Previous, playback.PreviousTime, playback.PreviousTimeBefore );
            if( playback.Additive )
            {
                samples.PreviousReference = resolve( playback.Previous, 0.f, 0.f );
            }
            float fade = playback.FadeDuration > 0.f ? std::clamp( playback.FadeElapsed / playback.FadeDuration, 0.f, 1.f ) : 1.f;
            samples.Fade = fade * fade * ( 3.f - 2.f * fade );   // smoothstep
        }
    }
    auto layerPose = [&]( const Animator::Binding& InBinding, const Animator::LayerPlayback& InPlayback, const LayerSamples& InSamples ) {
        Animator::Pose pose = statePose( InBinding, InSamples.Current );
        if( InPlayback.Previous >= 0 && InSamples.Fade < 1.f )
        {
            Animator::Pose from = statePose( InBinding, InSamples.Previous );
            blend( from, pose, InSamples.Fade );
            pose = from;
        }
        return pose;
    };

    // An additive layer's motion for a binding: its pose relative to its state's first frame
    // (position offset, local rotation, scale ratio), cross-faded like a pose.
    auto ratio = []( float InValue, float InReference ) { return std::abs( InReference ) > 1e-6f ? InValue / InReference : 1.f; };
    auto additiveDelta = [&]( const Animator::Binding& InBinding, const Animator::LayerPlayback& InPlayback, const LayerSamples& InSamples ) {
        auto difference = [&]( const StateSamples& InPose, const StateSamples& InReference ) {
            const Animator::Pose pose = statePose( InBinding, InPose );
            const Animator::Pose reference = statePose( InBinding, InReference );
            return Animator::Pose{ pose.Position - reference.Position, reference.Rotation.Inverse() * pose.Rotation,
                Vector3( ratio( pose.Scale.x, reference.Scale.x ), ratio( pose.Scale.y, reference.Scale.y ), ratio( pose.Scale.z, reference.Scale.z ) ) };
        };
        Animator::Pose delta = difference( InSamples.Current, InSamples.CurrentReference );
        if( InPlayback.Previous >= 0 && InSamples.Fade < 1.f )
        {
            Animator::Pose from = difference( InSamples.Previous, InSamples.PreviousReference );
            blend( from, delta, InSamples.Fade );
            delta = from;
        }
        return delta;
    };
    auto add = []( Animator::Pose& InOutPose, const Animator::Pose& InDelta, float InWeight ) {
        InOutPose.Position = InOutPose.Position + InDelta.Position * InWeight;
        InOutPose.Rotation = InOutPose.Rotation * Quaternion::Slerp( Quaternion::Identity, InDelta.Rotation, InWeight );
        InOutPose.Scale = Vector3( InOutPose.Scale.x * ( 1.f + ( InDelta.Scale.x - 1.f ) * InWeight ),
            InOutPose.Scale.y * ( 1.f + ( InDelta.Scale.y - 1.f ) * InWeight ),
            InOutPose.Scale.z * ( 1.f + ( InDelta.Scale.z - 1.f ) * InWeight ) );
    };

    for( size_t i = 0; i < a.m_bindings.size(); ++i )
    {
        const Animator::Binding& binding = a.m_bindings[i];
        Animator::Pose pose{ binding.BindPosition, binding.BindRotation, binding.BindScale };
        if( layers[0].Active )
        {
            pose = layerPose( binding, a.m_layers[0], layers[0] );
        }
        for( size_t layer = 1; layer < a.m_layers.size(); ++layer )
        {
            const Animator::LayerPlayback& playback = a.m_layers[layer];
            if( !layers[layer].Active || playback.Weight <= 0.f || !playback.BoneMask[i] )
            {
                continue;
            }
            if( playback.Additive )
            {
                add( pose, additiveDelta( binding, playback, layers[layer] ), playback.Weight );
            }
            else
            {
                blend( pose, layerPose( binding, playback, layers[layer] ), playback.Weight );
            }
        }
        a.m_pose[i] = pose;
    }

    // Root motion (base layer): the root's travel since last frame, horizontal part only, and its
    // turn about up.
    a.m_rootMotion = Vector3();
    a.m_rootTurn = 0.f;
    if( a.m_rootBinding < 0 || !layers[0].Active )
    {
        return;
    }
    const Animator::Binding& root = a.m_bindings[a.m_rootBinding];
    auto travel = [&]( const StateSamples& InSamples ) {
        Vector3 total;
        for( int i = 0; i < InSamples.Count; ++i )
        {
            const ClipSample& sample = InSamples.Items[i];
            const int channel = root.Channels[sample.Clip];
            if( channel < 0 || sample.Weight <= 0.f )
            {
                continue;
            }
            const Moonlight::AnimationChannel& keys = clips[sample.Clip].Channels[channel];
            const float duration = clips[sample.Clip].Duration;
            auto positionAt = [&]( float InTime ) {
                Vector3 position = root.BindPosition;
                Quaternion rotation = root.BindRotation;
                Vector3 scale = root.BindScale;
                Moonlight::AnimationClip::Sample( keys, InTime, position, rotation, scale );
                return position;
            };
            Vector3 delta;
            if( InSamples.Loop && duration > 0.f )
            {
                const float lapsNow = std::floor( sample.Time / duration );
                const float lapsBefore = std::floor( sample.TimeBefore / duration );
                const float now = sample.Time - lapsNow * duration;
                const float before = sample.TimeBefore - lapsBefore * duration;
                if( lapsNow == lapsBefore )
                {
                    delta = positionAt( now ) - positionAt( before );
                }
                else
                {
                    // Wrapped: to the end, any whole laps, then from the start.
                    const Vector3 lap = positionAt( duration ) - positionAt( 0.f );
                    delta = ( positionAt( duration ) - positionAt( before ) ) + lap * std::max( lapsNow - lapsBefore - 1.f, 0.f ) + ( positionAt( now ) - positionAt( 0.f ) );
                }
            }
            else
            {
                delta = positionAt( std::clamp( sample.Time, 0.f, duration ) ) - positionAt( std::clamp( sample.TimeBefore, 0.f, duration ) );
            }
            total = total + delta * sample.Weight;
        }
        return total;
    };
    Vector3 delta = travel( layers[0].Current );
    if( a.m_layers[0].Previous >= 0 && layers[0].Fade < 1.f )
    {
        const Vector3 previous = travel( layers[0].Previous );
        delta = previous + ( delta - previous ) * layers[0].Fade;
    }
    const Vector3 up = a.m_rootUp;
    a.m_rootMotion = delta - up * delta.Dot( up );

    // The turn: each clip's change in the root's heading about up since last frame (in its parent's
    // space), weighted like the travel, laps included.
    auto turn = [&]( const StateSamples& InSamples ) {
        float total = 0.f;
        for( int i = 0; i < InSamples.Count; ++i )
        {
            const ClipSample& sample = InSamples.Items[i];
            const int channel = root.Channels[sample.Clip];
            if( channel < 0 || sample.Weight <= 0.f )
            {
                continue;
            }
            const Moonlight::AnimationChannel& keys = clips[sample.Clip].Channels[channel];
            const float duration = clips[sample.Clip].Duration;
            auto rotationAt = [&]( float InTime ) {
                Vector3 position = root.BindPosition;
                Quaternion rotation = root.BindRotation;
                Vector3 scale = root.BindScale;
                Moonlight::AnimationClip::Sample( keys, InTime, position, rotation, scale );
                return rotation;
            };
            auto change = [&]( float InFrom, float InTo ) {
                return TwistAngle( rotationAt( InTo ) * rotationAt( InFrom ).Inverse(), up );
            };
            float angle = 0.f;
            if( InSamples.Loop && duration > 0.f )
            {
                const float lapsNow = std::floor( sample.Time / duration );
                const float lapsBefore = std::floor( sample.TimeBefore / duration );
                const float now = sample.Time - lapsNow * duration;
                const float before = sample.TimeBefore - lapsBefore * duration;
                angle = lapsNow == lapsBefore ? change( before, now )
                    : change( before, duration ) + change( 0.f, duration ) * std::max( lapsNow - lapsBefore - 1.f, 0.f ) + change( 0.f, now );
            }
            else
            {
                angle = change( std::clamp( sample.TimeBefore, 0.f, duration ), std::clamp( sample.Time, 0.f, duration ) );
            }
            total += angle * sample.Weight;
        }
        return total;
    };
    float angle = turn( layers[0].Current );
    if( a.m_layers[0].Previous >= 0 && layers[0].Fade < 1.f )
    {
        const float previous = turn( layers[0].Previous );
        angle = previous + ( angle - previous ) * layers[0].Fade;
    }
    a.m_rootTurn = angle;

    if( a.ApplyRootMotion )
    {
        // The entity moves instead: keep the root above it (vertical motion stays in the bone)...
        Animator::Pose& pose = a.m_pose[a.m_rootBinding];
        const Vector3 offset = pose.Position - root.BindPosition;
        pose.Position = pose.Position - ( offset - up * offset.Dot( up ) );
        if( a.RootRotation )
        {
            // ...and facing its bind heading, since the entity turns instead (tilt and roll stay).
            const float heading = TwistAngle( pose.Rotation * root.BindRotation.Inverse(), up );
            pose.Rotation = Quaternion::AngleAxis( -heading, up ) * pose.Rotation;
        }
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
        const bool anyPlaying = std::any_of( animator->m_layers.begin(), animator->m_layers.end(), []( const Animator::LayerPlayback& layer ) { return layer.Playing && layer.Current >= 0; } );
        if( anyPlaying && !animator->m_bindings.empty() )
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
        ApplyRootMotion( *animator );
    }
}


void AnimationCore::ApplyRootMotion( Animator& InAnimator )
{
    Animator& a = InAnimator;
    a.m_rootMotionWorld = Vector3();
    if( a.m_rootBinding < 0 || !a.m_bindings[a.m_rootBinding].Node )
    {
        return;
    }
    // The bone's horizontal travel, from its parent's space to the world.
    Transform* parent = a.m_bindings[a.m_rootBinding].Node->GetComponent<Transform>().GetParentTransform();
    glm::vec4 world( a.m_rootMotion.InternalVector, 0.f );
    if( parent )
    {
        world = parent->GetLocalToWorldMatrix().GetInternalMatrix() * world;
    }
    a.m_rootMotionWorld = Vector3( world.x, 0.f, world.z );
    // The turn about the parent's up axis, as degrees about world up (a mirrored parent flips it).
    float turn = a.m_rootTurn;
    if( parent )
    {
        const glm::mat4& toWorld = parent->GetLocalToWorldMatrix().GetInternalMatrix();
        const glm::vec3 upWorld = glm::vec3( toWorld * glm::vec4( a.m_rootUp.InternalVector, 0.f ) );
        if( glm::determinant( glm::mat3( toWorld ) ) * upWorld.y < 0.f )
        {
            turn = -turn;
        }
    }
    a.m_rootTurnDegrees = glm::degrees( turn );
    Entity* owner = a.Parent.Get();
    if( !a.ApplyRootMotion || !owner )
    {
        return;
    }
    if( a.RootRotation && std::abs( turn ) > 0.f )
    {
        Transform& transform = owner->GetComponent<Transform>();
        transform.SetWorldRotation( Quaternion::AngleAxis( turn, Vector3( 0.f, 1.f, 0.f ) ) * transform.GetWorldRotation() );
    }
    if( a.m_rootMotionWorld.LengthSquared() <= 0.f )
    {
        return;
    }
    if( CharacterController* character = owner->TryGetComponent<CharacterController>() )
    {
        character->Move( a.m_rootMotionWorld );
    }
    else
    {
        Transform& transform = owner->GetComponent<Transform>();
        transform.SetWorldPosition( transform.GetWorldPosition() + a.m_rootMotionWorld );
    }
}
