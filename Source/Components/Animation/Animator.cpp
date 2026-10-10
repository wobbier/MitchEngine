#include "PCH.h"
#include "Animator.h"
#include "Graphics/ModelResource.h"
#include "Scene/AnimationClip.h"
#include "Cores/AnimationCore.h"
#include "Engine/Engine.h"
#include <algorithm>
#include <cmath>

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

ME_REFLECT_ENUM( AnimatorLayerBlending, { { "Override", AnimatorLayerBlending::Override }, { "Additive", AnimatorLayerBlending::Additive } } )
ME_REFLECT_ENUM( AnimatorParameterType, { { "Float", AnimatorParameterType::Float }, { "Bool", AnimatorParameterType::Bool }, { "Trigger", AnimatorParameterType::Trigger } } )
ME_REFLECT_ENUM( AnimatorCondition, { { "Always", AnimatorCondition::Always }, { "Greater", AnimatorCondition::Greater }, { "Less", AnimatorCondition::Less },
    { "True", AnimatorCondition::True }, { "False", AnimatorCondition::False }, { "Trigger", AnimatorCondition::Trigger } } )

ME_REFLECT_BEGIN( AnimatorParameter )
    ME_FIELD( Name );
    ME_FIELD_NAMED( Type, "ParameterType" );
    ME_FIELD( Value );
ME_REFLECT_END()

ME_REFLECT_BEGIN( AnimatorBlendClip )
    ME_FIELD( Clip );
    ME_FIELD( Threshold );
    ME_FIELD( ThresholdY ).Tooltip( "2D blends: the position on the second parameter" );
ME_REFLECT_END()

ME_REFLECT_BEGIN( AnimatorState )
    ME_FIELD( Name );
    ME_FIELD( Clip );
    ME_FIELD( Speed ).Range( -10.f, 10.f );
    ME_FIELD( Loop );
    ME_FIELD( BlendParameter ).Tooltip( "Float parameter that picks between BlendClips by their thresholds" );
    ME_FIELD( BlendParameterY ).Tooltip( "Second float parameter: a 2D blend over (Threshold, ThresholdY)" );
    ME_FIELD( BlendClips );
    ME_FIELD( Layer ).Tooltip( "The layer this state plays in; empty = the base layer" );
ME_REFLECT_END()

ME_REFLECT_BEGIN( AnimatorLayer )
    ME_FIELD( Name );
    ME_FIELD( DefaultState );
    ME_FIELD( Weight ).Range( 0.f, 1.f );
    ME_FIELD( Mask ).Tooltip( "Bones (with everything below them) this layer drives; empty = all" );
    ME_FIELD( Blending ).Tooltip( "Override replaces the pose below; Additive adds the layer's motion (relative to its state's first frame) on top" );
ME_REFLECT_END()

ME_REFLECT_BEGIN( AnimatorTransition )
    ME_FIELD( From ).Tooltip( "Empty = any state" );
    ME_FIELD( To );
    ME_FIELD( Parameter );
    ME_FIELD( Condition );
    ME_FIELD( Threshold );
    ME_FIELD( Duration ).Range( 0.f, 10.f ).Tooltip( "Cross-fade seconds" );
    ME_FIELD( ExitTime ).Range( -1.f, 10.f ).Tooltip( "Normalized time the source state must reach first; -1 = any time" );
ME_REFLECT_END()

ME_REFLECT_BEGIN( AnimatorEventMarker )
    ME_FIELD( State );
    ME_FIELD( Time ).Range( 0.f, 1.f );
    ME_FIELD( Name );
ME_REFLECT_END()

ME_REFLECT_BEGIN( Animator )
    ME_FIELD( ClipSource ).Tooltip( "Optional model file whose animations to play (shared rigs); default: this entity's Model" );
    ME_FIELD( DefaultState ).Tooltip( "Empty = the first state (or clip)" );
    ME_FIELD( Speed ).Range( -10.f, 10.f );
    ME_FIELD( PlayOnStart );
    ME_FIELD( Parameters ).Category( "State Machine" );
    ME_FIELD( States ).Category( "State Machine" );
    ME_FIELD( Transitions ).Category( "State Machine" );
    ME_FIELD( Events ).Category( "State Machine" );
    ME_FIELD( Layers ).Category( "State Machine" );
    ME_FIELD( RootRotation ).Tooltip( "With root motion, the root's turning about up turns the entity too" );
    ME_FIELD( ApplyRootMotion ).Category( "Root Motion" ).Tooltip( "Move this entity by the root bone's horizontal travel" );
    ME_FIELD( RootBone ).Category( "Root Motion" ).Tooltip( "Empty = the topmost animated node" );
ME_REFLECT_END()


Animator::Animator()
    : Component( "Animator" )
{
}


void Animator::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    Unbind();
}


void Animator::Unbind()
{
    m_bound = false;
    m_clips = nullptr;
    m_clipModel.reset();
    m_bindings.clear();
    m_states.clear();
    m_layers.clear();
    m_rootBinding = -1;
}


void Animator::Play( const std::string& InState, float InFadeSeconds )
{
    // Applied by AnimationCore once the clips are bound.
    m_pendingPlays.emplace_back( InState, std::max( InFadeSeconds, 0.f ) );
}


void Animator::Stop()
{
    for( LayerPlayback& layer : m_layers )
    {
        layer.Playing = false;
    }
    m_pendingPlays.clear();
}


bool Animator::IsPlaying() const
{
    return !m_layers.empty() && m_layers[0].Playing;
}


int Animator::FindLayer( const std::string& InName ) const
{
    if( InName.empty() )
    {
        return 0;
    }
    for( size_t i = 0; i < Layers.size(); ++i )
    {
        if( Layers[i].Name == InName )
        {
            return static_cast<int>( i ) + 1;
        }
    }
    return -1;
}


int Animator::StateLayer( int InState ) const
{
    if( InState < 0 || InState >= static_cast<int>( m_states.size() ) )
    {
        return -1;
    }
    return FindLayer( m_states[InState].Layer );
}


const Animator::LayerPlayback* Animator::GetLayer( const std::string& InLayer ) const
{
    const int index = FindLayer( InLayer );
    return index >= 0 && index < static_cast<int>( m_layers.size() ) ? &m_layers[index] : nullptr;
}


void Animator::SetLayerWeight( const std::string& InLayer, float InWeight )
{
    const int index = FindLayer( InLayer );
    if( index > 0 && index < static_cast<int>( m_layers.size() ) )
    {
        m_layers[index].Weight = std::clamp( InWeight, 0.f, 1.f );
    }
    else if( index > 0 )
    {
        m_pendingWeights.emplace_back( InLayer, std::clamp( InWeight, 0.f, 1.f ) );
    }
}


float Animator::GetLayerWeight( const std::string& InLayer ) const
{
    if( const LayerPlayback* layer = GetLayer( InLayer ) )
    {
        return layer->Weight;
    }
    const int index = FindLayer( InLayer );
    return index > 0 ? Layers[index - 1].Weight : ( index == 0 ? 1.f : 0.f );
}


int Animator::FindState( const std::string& InName ) const
{
    for( size_t i = 0; i < m_states.size(); ++i )
    {
        if( m_states[i].Name == InName )
        {
            return static_cast<int>( i );
        }
    }
    return -1;
}


int Animator::FindClip( const std::string& InName ) const
{
    if( !m_clips )
    {
        return -1;
    }
    for( size_t i = 0; i < m_clips->size(); ++i )
    {
        if( ( *m_clips )[i].Name == InName )
        {
            return static_cast<int>( i );
        }
    }
    return -1;
}


AnimatorParameter* Animator::FindParameter( const std::string& InName )
{
    for( AnimatorParameter& parameter : Parameters )
    {
        if( parameter.Name == InName )
        {
            return &parameter;
        }
    }
    return nullptr;
}


const AnimatorParameter* Animator::FindParameter( const std::string& InName ) const
{
    return const_cast<Animator*>( this )->FindParameter( InName );
}


void Animator::SetFloat( const std::string& InName, float InValue )
{
    if( AnimatorParameter* parameter = FindParameter( InName ) )
    {
        parameter->Value = InValue;
    }
    else
    {
        Parameters.push_back( { InName, AnimatorParameterType::Float, InValue } );
    }
}


float Animator::GetFloat( const std::string& InName ) const
{
    const AnimatorParameter* parameter = FindParameter( InName );
    return parameter ? parameter->Value : 0.f;
}


void Animator::SetBool( const std::string& InName, bool InValue )
{
    if( AnimatorParameter* parameter = FindParameter( InName ) )
    {
        parameter->Value = InValue ? 1.f : 0.f;
    }
    else
    {
        Parameters.push_back( { InName, AnimatorParameterType::Bool, InValue ? 1.f : 0.f } );
    }
}


bool Animator::GetBool( const std::string& InName ) const
{
    return GetFloat( InName ) != 0.f;
}


void Animator::SetTrigger( const std::string& InName )
{
    if( AnimatorParameter* parameter = FindParameter( InName ) )
    {
        parameter->Value = 1.f;
    }
    else
    {
        Parameters.push_back( { InName, AnimatorParameterType::Trigger, 1.f } );
    }
}


void Animator::ResetTrigger( const std::string& InName )
{
    if( AnimatorParameter* parameter = FindParameter( InName ) )
    {
        parameter->Value = 0.f;
    }
}


std::string Animator::GetCurrentState( const std::string& InLayer ) const
{
    const LayerPlayback* layer = GetLayer( InLayer );
    return layer && layer->Current >= 0 && layer->Current < static_cast<int>( m_states.size() ) ? m_states[layer->Current].Name : std::string();
}


float Animator::GetStateTime( const std::string& InLayer ) const
{
    const LayerPlayback* layer = GetLayer( InLayer );
    return layer ? layer->Time : 0.f;
}


bool Animator::IsInTransition( const std::string& InLayer ) const
{
    const LayerPlayback* layer = GetLayer( InLayer );
    return layer && layer->Previous >= 0;
}


float Animator::StateDuration( int InState ) const
{
    if( InState < 0 || InState >= static_cast<int>( m_states.size() ) || !m_clips )
    {
        return 0.f;
    }
    const AnimatorState& state = m_states[InState];
    if( !state.BlendClips.empty() )
    {
        // Blends are normalized to their first clip's length.
        const int clip = FindClip( state.BlendClips.front().Clip );
        return clip >= 0 ? ( *m_clips )[clip].Duration : 0.f;
    }
    const int clip = FindClip( state.Clip );
    return clip >= 0 ? ( *m_clips )[clip].Duration : 0.f;
}


float Animator::GetNormalizedTime( const std::string& InLayer ) const
{
    const LayerPlayback* layer = GetLayer( InLayer );
    if( !layer )
    {
        return 0.f;
    }
    const float duration = StateDuration( layer->Current );
    return duration > 0.f ? layer->Time / duration : 0.f;
}


void Animator::BeginState( int InState, float InFade )
{
    const int index = StateLayer( InState );
    if( index < 0 || index >= static_cast<int>( m_layers.size() ) )
    {
        return;
    }
    LayerPlayback& layer = m_layers[index];
    if( InFade > 0.f && layer.Current >= 0 && layer.Playing )
    {
        layer.Previous = layer.Current;
        layer.PreviousTime = layer.Time;
        layer.PreviousTimeBefore = layer.Time;
        layer.FadeElapsed = 0.f;
        layer.FadeDuration = InFade;
    }
    else
    {
        layer.Previous = -1;
    }
    layer.Current = InState;
    layer.Time = 0.f;
    layer.TimeBefore = 0.f;
    layer.Playing = true;
}


void Animator::UseClips( SharedPtr<std::vector<Moonlight::AnimationClip>> InClips )
{
    Unbind();
    m_ownClips = std::move( InClips );
}


std::vector<std::string> Animator::GetClipNames() const
{
    std::vector<std::string> names;
    if( m_clips )
    {
        for( const Moonlight::AnimationClip& clip : *m_clips )
        {
            names.push_back( clip.Name );
        }
    }
    return names;
}


void Animator::StartPreview( const std::string& InState )
{
    m_previewRequested = true;
    m_previewIdleUpdates = 0;
    if( InState != m_previewState || InState.empty() )
    {
        m_previewState = InState;
        PreviewTime = 0.f;
    }
}


void Animator::StopPreview()
{
    m_previewRequested = false;
}


float Animator::GetPreviewDuration() const
{
    const int state = m_previewBound ? FindState( m_previewState ) : -1;
    return state >= 0 ? StateDuration( state ) : 0.f;
}


#if USING( ME_EDITOR )
void Animator::OnEditorInspect()
{
    AnimationCore* core = GetEngine().Animation;
    if( !core || !core->IsRunning() )
    {
        // Edit mode: preview a state on the model.
        m_previewIdleUpdates = 0;
        if( !m_previewRequested )
        {
            if( ImGui::Button( "Preview", ImVec2( -1.f, 0.f ) ) )
            {
                StartPreview( DefaultState );
            }
            return;
        }
        if( !m_previewBound )
        {
            ImGui::TextDisabled( "Binding..." );
            return;
        }
        if( ImGui::BeginCombo( "Preview State", m_previewState.c_str() ) )
        {
            for( const AnimatorState& state : m_states )
            {
                if( ImGui::Selectable( state.Name.c_str(), state.Name == m_previewState ) )
                {
                    m_previewState = state.Name;
                    PreviewTime = 0.f;
                }
            }
            ImGui::EndCombo();
        }
        if( ImGui::Button( PreviewPlaying ? "Pause" : "Play" ) )
        {
            PreviewPlaying = !PreviewPlaying;
        }
        ImGui::SameLine();
        const float duration = GetPreviewDuration();
        ImGui::SetNextItemWidth( -90.f );
        if( ImGui::SliderFloat( "##PreviewTime", &PreviewTime, 0.f, std::max( duration, 0.001f ), "%.2f s" ) )
        {
            PreviewPlaying = false;
        }
        ImGui::SameLine();
        if( ImGui::Button( "Stop" ) )
        {
            StopPreview();
        }
        ImGui::TextDisabled( "Previewing: the authored pose comes back when it stops" );
        return;
    }
    if( !m_bound )
    {
        ImGui::TextDisabled( "Binds to its clips on its first update" );
        return;
    }
    ImGui::Text( "State: %s%s", GetCurrentState().c_str(), IsInTransition() ? "  (blending)" : "" );
    ImGui::Text( "Time: %.2f s  (%.2f)", GetStateTime(), GetNormalizedTime() );
    for( const AnimatorLayer& layer : Layers )
    {
        ImGui::Text( "%s: %s  (weight %.2f)%s", layer.Name.c_str(), GetCurrentState( layer.Name ).c_str(), GetLayerWeight( layer.Name ), IsInTransition( layer.Name ) ? "  (blending)" : "" );
    }
    ImGui::Text( "Animated nodes: %zu", m_bindings.size() );
    if( ApplyRootMotion )
    {
        ImGui::Text( "Root motion: %.3f, %.3f, %.3f", m_rootMotionWorld.x, m_rootMotionWorld.y, m_rootMotionWorld.z );
    }
    if( ImGui::TreeNode( "Clips" ) )
    {
        for( const std::string& name : GetClipNames() )
        {
            if( ImGui::Selectable( name.c_str() ) )
            {
                Play( name, 0.2f );
            }
        }
        ImGui::TreePop();
    }
}
#endif
