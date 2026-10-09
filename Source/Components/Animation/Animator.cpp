#include "PCH.h"
#include "Animator.h"
#include "Graphics/ModelResource.h"
#include "Scene/AnimationClip.h"
#include <algorithm>
#include <cmath>

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

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
ME_REFLECT_END()

ME_REFLECT_BEGIN( AnimatorState )
    ME_FIELD( Name );
    ME_FIELD( Clip );
    ME_FIELD( Speed ).Range( -10.f, 10.f );
    ME_FIELD( Loop );
    ME_FIELD( BlendParameter ).Tooltip( "Float parameter that picks between BlendClips by their thresholds" );
    ME_FIELD( BlendClips );
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
    m_current = -1;
    m_previous = -1;
    m_playing = false;
}


void Animator::Play( const std::string& InState, float InFadeSeconds )
{
    // Applied by AnimationCore once the clips are bound.
    m_pendingPlay = InState;
    m_pendingFade = std::max( InFadeSeconds, 0.f );
    m_hasPendingPlay = true;
}


void Animator::Stop()
{
    m_playing = false;
    m_hasPendingPlay = false;
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


std::string Animator::GetCurrentState() const
{
    return m_current >= 0 && m_current < static_cast<int>( m_states.size() ) ? m_states[m_current].Name : std::string();
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


float Animator::GetNormalizedTime() const
{
    const float duration = StateDuration( m_current );
    return duration > 0.f ? m_time / duration : 0.f;
}


void Animator::BeginState( int InState, float InFade )
{
    if( InFade > 0.f && m_current >= 0 && m_playing )
    {
        m_previous = m_current;
        m_previousTime = m_time;
        m_fadeElapsed = 0.f;
        m_fadeDuration = InFade;
    }
    else
    {
        m_previous = -1;
    }
    m_current = InState;
    m_time = 0.f;
    m_playing = true;
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


#if USING( ME_EDITOR )
void Animator::OnEditorInspect()
{
    if( !m_bound )
    {
        ImGui::TextDisabled( "Binds to its clips when the game runs" );
        return;
    }
    ImGui::Text( "State: %s%s", GetCurrentState().c_str(), IsInTransition() ? "  (blending)" : "" );
    ImGui::Text( "Time: %.2f s  (%.2f)", m_time, GetNormalizedTime() );
    ImGui::Text( "Animated nodes: %zu", m_bindings.size() );
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
