#include "PCH.h"
#include "SequenceCore.h"
#include "Components/Animation/Sequence.h"

SequenceCore::SequenceCore() : Base( ComponentFilter().Requires<Sequence>() )
{
    SetIsSerializable( false );
    // Script OnStart may Play/Pause/Seek; initialize the sequence before those calls.
    RunsBefore( "ScriptCore" );
}


void SequenceCore::OnStart()
{
    StopAllPreviews();
    m_running = true;
    for( Entity& entity : GetEntities() )
    {
        Sequence& sequence = entity.GetComponent<Sequence>();
        sequence.RefreshClip();
        sequence.m_initialized = false;
        sequence.m_playing = false;
        sequence.m_time = 0.;
        sequence.m_markerTime = 0.;
    }
}


void SequenceCore::OnStop()
{
    StopAllPreviews();
    m_running = false;
    for( Entity& entity : GetEntities() ) entity.GetComponent<Sequence>().Pause();
}


void SequenceCore::Update( const UpdateContext& InContext )
{
    if( m_running ) Advance( InContext.GetDeltaTime() );
    else UpdatePreviews( InContext.GetUnscaledDeltaTime() );
}


void SequenceCore::Advance( float InDeltaSeconds )
{
    for( Entity& entity : GetEntities() )
    {
        Sequence& sequence = entity.GetComponent<Sequence>();
        sequence.RefreshClip();
        if( !sequence.IsEnabled() || !entity.IsActiveInHierarchy() ) continue;
        if( !sequence.m_initialized )
        {
            sequence.m_initialized = true;
            if( sequence.PlayOnStart ) sequence.Play();
        }
        if( sequence.IsPlaying() ) sequence.Advance( InDeltaSeconds );
    }
}


void SequenceCore::UpdatePreviews( float InDeltaSeconds )
{
    for( Entity& entity : GetEntities() )
    {
        Sequence& sequence = entity.GetComponent<Sequence>();
        sequence.RefreshClip();
        if( !sequence.IsPreviewing() ) continue;
        if( !sequence.IsEnabled() || !entity.IsActiveInHierarchy() || ++sequence.m_previewIdle > 30 ) sequence.StopPreview();
        else sequence.Advance( InDeltaSeconds );
    }
}


void SequenceCore::StopAllPreviews()
{
    GetWorld().ForEachEntity( []( Entity& entity ) {
        if( Sequence* sequence = entity.TryGetComponent<Sequence>() ) sequence->StopPreview();
    } );
}


void SequenceCore::RestorePreviewValues()
{
    GetWorld().ForEachEntity( []( Entity& entity ) {
        if( Sequence* sequence = entity.TryGetComponent<Sequence>() ) sequence->RestorePreviewValues();
    } );
}
