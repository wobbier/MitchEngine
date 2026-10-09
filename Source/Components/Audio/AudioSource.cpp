#include "PCH.h"
#include "AudioSource.h"
#include "Components/Transform.h"
#include "Cores/AudioCore.h"
#include "Utils/StringUtils.h"
#include "Path.h"
#include "imgui.h"
#include "Events/HavanaEvents.h"
#include <Types/AssetDescriptor.h>

#include "Events/AudioEvents.h"

#if USING( ME_FMOD )
#include "fmod.hpp"
#include <fmod_errors.h>
#endif
#include "Resource/MetaFile.h"
#include "Core/Assert.h"
#include "Events/EditorEvents.h"
#include <algorithm>

ME_REFLECT_ENUM( AudioBus, { { "Master", AudioBus::Master }, { "Music", AudioBus::Music }, { "SFX", AudioBus::SFX }, { "UI", AudioBus::UI }, { "Voice", AudioBus::Voice } } )
ME_REFLECT_ENUM( AudioRolloff, { { "Inverse", AudioRolloff::Inverse }, { "Linear", AudioRolloff::Linear }, { "LinearSquare", AudioRolloff::LinearSquare } } )

ME_REFLECT_BEGIN( AudioSource )
    ME_FIELD( FilePath ).Asset( "Audio" ).Tooltip( "The clip (wav / mp3)" );
    ME_FIELD( Bus ).Tooltip( "Mixer bus; bus volumes are in Project Settings > Audio" );
    ME_FIELD( Volume ).Range( 0.f, 1.f );
    ME_FIELD( Pitch ).Range( 0.1f, 3.f ).Tooltip( "Playback rate: 2 = an octave up and twice as fast" );
    ME_FIELD( Mute );
    ME_FIELD( PlayOnAwake ).Tooltip( "Starts when the game starts, or when the source is spawned during play" );
    ME_FIELD( Loop );
    ME_FIELD( MaxOneShots ).Range( 1.f, 64.f ).Tooltip( "Overlapping PlayOneShot voices; the oldest stops beyond this" );
    ME_FIELD( SpatialBlend ).Category( "3D Sound" ).Range( 0.f, 1.f ).Tooltip( "0 = 2D (no panning or distance fade), 1 = positioned at this entity" );
    ME_FIELD( MinDistance ).Category( "3D Sound" ).Range( 0.f, 1000.f ).Tooltip( "Metres at full volume" );
    ME_FIELD( MaxDistance ).Category( "3D Sound" ).Range( 0.f, 10000.f ).Tooltip( "Linear rolloffs reach silence here; Inverse stops fading here" );
    ME_FIELD( Rolloff ).Category( "3D Sound" );
    ME_FIELD( DopplerLevel ).Category( "3D Sound" ).Range( 0.f, 5.f ).Tooltip( "Pitch shift from relative motion (0 = none)" );
    ME_FIELD( Priority ).Category( "3D Sound" ).Range( 0.f, 256.f ).Tooltip( "0 = most important; the least important voices go virtual first" );
ME_REFLECT_END()


AudioSource::AudioSource( const std::string& InFilePath )
    : Component( "AudioSource" )
    , FilePath( InFilePath )
{
}


AudioSource::AudioSource()
    : Component( "AudioSource" )
{
}


void AudioSource::OnDeserialize( const json& inJson )
{
    const std::string previous = FilePath.GetLocalPathString();
    Reflection::FromJson( StaticType(), this, inJson );
    if( FilePath.GetLocalPathString() != previous )
    {
        // A different clip: AudioCore loads it on its next update.
        StopAll();
        SoundInstance = nullptr;
        IsInitialized = false;
    }
}


AudioPlayParams AudioSource::MakePlayParams() const
{
    AudioPlayParams params;
    params.Bus = Bus;
    params.Volume = Mute ? 0.f : Volume;
    params.Pitch = Pitch;
    params.Loop = Loop;
    params.SpatialBlend = SpatialBlend;
    params.Position = m_hasPosition ? m_position : GetWorldPosition();
    params.Velocity = m_velocity;
    params.MinDistance = MinDistance;
    params.MaxDistance = MaxDistance;
    params.Rolloff = Rolloff;
    params.DopplerLevel = DopplerLevel;
    params.Priority = Priority;
    return params;
}


Vector3 AudioSource::GetWorldPosition() const
{
    if( Parent )
    {
        if( Transform* transform = Parent->TryGetComponent<Transform>() )
        {
            return transform->GetWorldPosition();
        }
    }
    return m_position;
}


void AudioSource::Play()
{
    Play( Loop, false );
}


void AudioSource::Play( bool ShouldLoop, bool StartPaused )
{
    AudioCore* core = AudioCore::Get();
    if( core && !IsInitialized )
    {
        core->InitComponent( *this );
    }
    if( !core || !SoundInstance )
    {
        // Not loaded yet (no audio core, or no clip): AudioCore starts it once it is.
        m_pendingPlay = !FilePath.GetLocalPathString().empty();
        m_voiceLoops = ShouldLoop;
        return;
    }
    m_pendingPlay = false;
    m_voice.Stop();
    AudioPlayParams params = MakePlayParams();
    params.Loop = ShouldLoop;
    params.StartPaused = StartPaused;
    m_voiceLoops = ShouldLoop;
    m_voice = core->Play( SoundInstance, params );
#if USING( ME_FMOD )
    ChannelHandle = m_voice.GetChannel();
#endif
}


AudioVoice AudioSource::PlayOneShot( float InVolumeScale )
{
    return PlayOneShot( FilePath, InVolumeScale );
}


AudioVoice AudioSource::PlayOneShot( const Path& InClip, float InVolumeScale )
{
    AudioCore* core = AudioCore::Get();
    if( !core )
    {
        return AudioVoice();
    }
    SharedPtr<Sound> sound = InClip.GetLocalPathString() == FilePath.GetLocalPathString() && SoundInstance ? SoundInstance : core->LoadSound( InClip );
    if( !sound )
    {
        return AudioVoice();
    }
    // Voice limit: drop finished voices, then steal the oldest.
    m_oneShots.erase( std::remove_if( m_oneShots.begin(), m_oneShots.end(), []( const OneShot& InShot ) { return !InShot.Voice.IsValid(); } ), m_oneShots.end() );
    while( !m_oneShots.empty() && static_cast<int>( m_oneShots.size() ) >= std::max( MaxOneShots, 1 ) )
    {
        m_oneShots.front().Voice.Stop();
        m_oneShots.erase( m_oneShots.begin() );
    }
    AudioPlayParams params = MakePlayParams();
    params.Loop = false;
    params.Volume *= InVolumeScale;
    AudioVoice voice = core->Play( sound, params );
    if( voice.IsValid() )
    {
        m_oneShots.push_back( { voice, InVolumeScale } );
    }
    return voice;
}


void AudioSource::UpdateVoices( const Vector3& InPosition, const Vector3& InVelocity )
{
    m_position = InPosition;
    m_velocity = InVelocity;
    m_hasPosition = true;
    AudioCore* core = AudioCore::Get();
    if( !core )
    {
        return;
    }
    AudioPlayParams params = MakePlayParams();
    if( m_voice.IsValid() )
    {
        params.Loop = m_voiceLoops;
        core->ApplyParams( m_voice, params );
    }
    else
    {
        m_voice = AudioVoice();
#if USING( ME_FMOD )
        ChannelHandle = nullptr;
#endif
    }
    params.Loop = false;
    const float volume = params.Volume;
    for( size_t i = 0; i < m_oneShots.size(); )
    {
        if( !m_oneShots[i].Voice.IsValid() )
        {
            m_oneShots.erase( m_oneShots.begin() + i );
            continue;
        }
        params.Volume = volume * m_oneShots[i].VolumeScale;
        core->ApplyParams( m_oneShots[i].Voice, params );
        ++i;
    }
}


void AudioSource::Pause()
{
    m_voice.SetPaused( true );
}


void AudioSource::Resume()
{
    m_voice.SetPaused( false );
}


void AudioSource::Stop( bool immediate )
{
    m_pendingPlay = false;
    m_voice.Stop();
    m_voice = AudioVoice();
#if USING( ME_FMOD )
    ChannelHandle = nullptr;
#endif
}


void AudioSource::StopAll()
{
    Stop();
    for( OneShot& shot : m_oneShots )
    {
        shot.Voice.Stop();
    }
    m_oneShots.clear();
}


bool AudioSource::IsLoaded() const
{
#if USING( ME_FMOD )
    if( SoundInstance )
    {
        return SoundInstance->IsReady();
    }
#endif
    return false;
}


bool AudioSource::IsPlaying() const
{
    return m_voice.IsPlaying();
}


bool AudioSource::IsPaused() const
{
    return m_voice.IsPaused();
}


unsigned int AudioSource::GetLength() const
{
#if USING( ME_FMOD )
    unsigned int audioLength = 0;
    if( SoundInstance && SoundInstance->Handle && SoundInstance->Handle->getLength( &audioLength, FMOD_TIMEUNIT_MS ) != FMOD_OK )
    {
        BRUH( "Failed to get audio length" );
    }
    return audioLength;
#else
    return 0;
#endif
}


unsigned int AudioSource::GetPositionMs() const
{
    return m_voice.GetPositionMs();
}


void AudioSource::SetPositionMs( unsigned int position )
{
    m_voice.SetPositionMs( position );
}


void AudioSource::SetPositionPercent( float positionPercent )
{
    m_voice.SetPositionMs( static_cast<unsigned int>( GetLength() * positionPercent ) );
}


float AudioSource::GetVolume()
{
    return Volume;
}


void AudioSource::SetVolume( float inVolumePercent )
{
    Volume = inVolumePercent;
    m_voice.SetVolume( Mute ? 0.f : Volume );
}


void AudioSource::SetPlaybackSpeed( float inSpeed )
{
    Pitch = inSpeed;
    m_voice.SetPitch( Pitch );
}


float AudioSource::GetPlaybackSpeed()
{
    return Pitch;
}


float AudioSource::GetAudibility() const
{
    return m_voice.GetAudibility();
}


#if USING( ME_EDITOR )
void AudioSource::OnEditorInspect()
{
#if !USING( ME_FMOD )
    ImGui::Text( "FMOD NOT ENABLED! See Help > About." );
    return;
#endif
    if( FilePath.GetLocalPathString().empty() )
    {
        return;
    }
    if( IsPlaying() )
    {
        if( ImGui::Button( "Stop" ) )
        {
            StopAll();
        }
        ImGui::SameLine();
        if( IsPaused() ? ImGui::Button( "Resume" ) : ImGui::Button( "Pause" ) )
        {
            IsPaused() ? Resume() : Pause();
        }
    }
    else if( ImGui::Button( "Play" ) )
    {
        Play();
    }
    ImGui::SameLine();
    if( ImGui::Button( "One-Shot" ) )
    {
        PlayOneShot();
    }

    const unsigned int length = GetLength();
    if( IsPlaying() && length > 0 )
    {
        float position = static_cast<float>( GetPositionMs() ) / 1000.f;
        if( ImGui::SliderFloat( "##Seek", &position, 0.f, length / 1000.f, "%.2f s" ) )
        {
            SetPositionMs( static_cast<unsigned int>( position * 1000.f ) );
        }
        ImGui::ProgressBar( GetAudibility(), ImVec2( -1.f, 0.f ), "Audibility" );
    }
    else if( length > 0 )
    {
        ImGui::TextDisabled( "%.2f s", length / 1000.f );
    }
    if( !m_oneShots.empty() )
    {
        ImGui::TextDisabled( "%zu one-shot voice(s)", m_oneShots.size() );
    }
}
#endif


void AudioSource::Init()
{
}


void AudioSource::ClearData()
{
    StopAll();
    SoundInstance = nullptr;
    IsInitialized = false;
    FilePath = Path();
}


std::string AudioResourceMetadata::GetExtension2() const
{
    return "wav";
}


void AudioResourceMetadata::OnSerialize( json& inJson )
{
}


void AudioResourceMetadata::OnDeserialize( const json& inJson )
{
}


#if USING( ME_EDITOR )
void AudioResourceMetadata::Export()
{
}


void AudioResourceMetadata::OnEditorInspect()
{
    MetaBase::OnEditorInspect();
#if !USING( ME_FMOD )
    ImGui::Separator();
    ImGui::Text( "FMOD NOT ENABLED! See Help > About." );
    return;
#endif
    static SharedPtr<AudioSource> source = nullptr;
    if( source && source->FilePath.GetLocalPath() != FilePath.GetLocalPath() )
    {
        source->Stop();
        source = nullptr;
    }

    if( source && source->IsPlaying() )
    {
        if( ImGui::Button( "Stop" ) )
        {
            source->Stop();
        }
    }
    else if( ImGui::Button( "Play" ) )
    {
        if( !source || ( source && !source->IsInitialized ) )
        {
            PlayAudioEvent evt;
            evt.SourceName = FilePath.FullPath;
            evt.Callback = []( SharedPtr<AudioSource> loadedAudio ) { source = loadedAudio; };
            evt.Fire();
        }
        else
        {
            source->Play();
        }
    }

    if( !source )
    {
        return;
    }

    ImGui::SameLine();
    float progress = (float)source->GetPositionMs();

    if( ImGui::SliderFloat( "##SeekSlider", &progress, 0, (float)source->GetLength(), "%.3f" ) )
    {
        source->SetPositionMs( progress );
    }

    if( ImGui::Button( "Seek Half Way" ) )
    {
        float half = (float)source->GetLength() / 2.f;

        source->SetPositionMs( half );
    }
}
#endif


std::string AudioResourceMetadataMp3::GetExtension2() const
{
    return "mp3";
}
