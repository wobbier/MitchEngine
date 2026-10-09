#include "PCH.h"
#include "AudioCore.h"
#include "Components/Audio/AudioListener.h"
#include "Components/Audio/AudioSource.h"
#include "Components/Camera.h"
#include "Components/Transform.h"
#include "Core/Assert.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Events/AudioEvents.h"
#include "Resource/ResourceCache.h"
#include "Resources/SoundResource.h"
#include "optick.h"
#include <algorithm>

#if USING( ME_FMOD )
#include "fmod.hpp"
#include "fmod_errors.h"
#endif

static_assert( ProjectSettings::kAudioBusCount == static_cast<int>( AudioBus::Count ), "Project Settings keeps one volume per AudioBus" );

namespace
{
    AudioCore* s_instance = nullptr;

#if USING( ME_FMOD )
    FMOD_VECTOR ToFmod( const Vector3& InVector )
    {
        return FMOD_VECTOR{ InVector.x, InVector.y, InVector.z };
    }


    bool Check( FMOD_RESULT InResult, const char* InWhat )
    {
        if( InResult != FMOD_OK )
        {
            YIKES( std::string( "FMOD: " ) + InWhat + ": " + FMOD_ErrorString( InResult ) );
            return false;
        }
        return true;
    }


    // Faster than sound: a teleport (scene load, camera snap), not motion; no doppler blip.
    Vector3 MotionVelocity( const Vector3& InFrom, const Vector3& InTo, float InDeltaSeconds )
    {
        if( InDeltaSeconds <= 0.f )
        {
            return Vector3();
        }
        const Vector3 velocity = ( InTo - InFrom ) / InDeltaSeconds;
        return velocity.Length() > 340.f ? Vector3() : velocity;
    }


    FMOD_MODE ModeFor( const AudioPlayParams& InParams )
    {
        FMOD_MODE mode = InParams.Loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;
        if( InParams.SpatialBlend <= 0.f )
        {
            return mode | FMOD_2D;
        }
        mode |= FMOD_3D | FMOD_3D_WORLDRELATIVE;
        switch( InParams.Rolloff )
        {
        case AudioRolloff::Linear:
            return mode | FMOD_3D_LINEARROLLOFF;
        case AudioRolloff::LinearSquare:
            return mode | FMOD_3D_LINEARSQUAREROLLOFF;
        case AudioRolloff::Inverse:
        default:
            return mode | FMOD_3D_INVERSEROLLOFF;
        }
    }
#endif
}


const char* AudioBusName( AudioBus InBus )
{
    switch( InBus )
    {
    case AudioBus::Master:
        return "Master";
    case AudioBus::Music:
        return "Music";
    case AudioBus::SFX:
        return "SFX";
    case AudioBus::UI:
        return "UI";
    case AudioBus::Voice:
        return "Voice";
    case AudioBus::Count:
    default:
        return "";
    }
}


bool AudioVoice::IsValid() const
{
#if USING( ME_FMOD )
    bool playing = false;
    return m_channel && m_channel->isPlaying( &playing ) == FMOD_OK && playing;
#else
    return false;
#endif
}


bool AudioVoice::IsPlaying() const
{
    return IsValid();
}


bool AudioVoice::IsPaused() const
{
#if USING( ME_FMOD )
    bool paused = false;
    return m_channel && m_channel->getPaused( &paused ) == FMOD_OK && paused;
#else
    return false;
#endif
}


void AudioVoice::Stop()
{
#if USING( ME_FMOD )
    if( m_channel )
    {
        m_channel->stop();
    }
#endif
}


void AudioVoice::SetPaused( bool InPaused )
{
#if USING( ME_FMOD )
    if( m_channel )
    {
        m_channel->setPaused( InPaused );
    }
#endif
}


void AudioVoice::SetVolume( float InVolume )
{
#if USING( ME_FMOD )
    if( m_channel )
    {
        m_channel->setVolume( InVolume );
    }
#endif
}


void AudioVoice::SetPitch( float InPitch )
{
#if USING( ME_FMOD )
    if( m_channel )
    {
        m_channel->setPitch( std::max( InPitch, 0.01f ) );
    }
#endif
}


void AudioVoice::SetPosition( const Vector3& InPosition, const Vector3& InVelocity )
{
#if USING( ME_FMOD )
    if( m_channel )
    {
        const FMOD_VECTOR position = ToFmod( InPosition );
        const FMOD_VECTOR velocity = ToFmod( InVelocity );
        m_channel->set3DAttributes( &position, &velocity );
    }
#endif
}


unsigned int AudioVoice::GetPositionMs() const
{
    unsigned int position = 0;
#if USING( ME_FMOD )
    if( m_channel )
    {
        m_channel->getPosition( &position, FMOD_TIMEUNIT_MS );
    }
#endif
    return position;
}


void AudioVoice::SetPositionMs( unsigned int InPosition )
{
#if USING( ME_FMOD )
    if( m_channel )
    {
        m_channel->setPosition( InPosition, FMOD_TIMEUNIT_MS );
    }
#endif
}


float AudioVoice::GetAudibility() const
{
    float audibility = 0.f;
#if USING( ME_FMOD )
    if( m_channel && m_channel->getAudibility( &audibility ) != FMOD_OK )
    {
        audibility = 0.f;
    }
#endif
    return audibility;
}


AudioCore::AudioCore( AudioOutput InOutput )
    : Base( ComponentFilter().Requires<AudioSource>() )
    , m_output( InOutput )
{
    SetIsSerializable( false );
    m_busVolumes.fill( 1.f );
    m_busMuted.fill( false );

#if USING( ME_FMOD )
    // FMOD's logging build (debug configurations) writes warnings and errors here, not to stdout.
    FMOD::Debug_Initialize( FMOD_DEBUG_LEVEL_WARNING, FMOD_DEBUG_MODE_FILE, nullptr, "fmod_log.txt" );
    if( !Check( FMOD::System_Create( &m_system ), "System_Create" ) )
    {
        m_system = nullptr;
    }
    if( m_system )
    {
        if( m_output == AudioOutput::Silent )
        {
            m_system->setOutput( FMOD_OUTPUTTYPE_NOSOUND );
        }
        else if( m_output == AudioOutput::Manual )
        {
            m_system->setOutput( FMOD_OUTPUTTYPE_NOSOUND_NRT );
        }
        m_system->setStreamBufferSize( 64 * 1024, FMOD_TIMEUNIT_RAWBYTES );
        FMOD_RESULT result = m_system->init( 512, FMOD_INIT_NORMAL, nullptr );
        if( result != FMOD_OK && m_output == AudioOutput::Device )
        {
            // No usable device (headless machine, no sound server): keep the engine running silently.
            BRUH( std::string( "FMOD: no audio device (" ) + FMOD_ErrorString( result ) + "); audio is silent" );
            m_system->release();
            m_system = nullptr;
            m_output = AudioOutput::Silent;
            if( Check( FMOD::System_Create( &m_system ), "System_Create" ) )
            {
                m_system->setOutput( FMOD_OUTPUTTYPE_NOSOUND );
                result = m_system->init( 512, FMOD_INIT_NORMAL, nullptr );
            }
        }
        if( m_system && !Check( result, "System::init" ) )
        {
            m_system->release();
            m_system = nullptr;
        }
    }
    if( m_system )
    {
        CLog::Log( CLog::LogType::Info, std::string( "Audio: FMOD output " ) + ( m_output == AudioOutput::Device ? "device" : m_output == AudioOutput::Silent ? "silent" : "manual" ) );
        // Metres: doppler and rolloff use world units directly.
        m_system->set3DSettings( 1.f, 1.f, 1.f );
        m_system->getMasterChannelGroup( &m_buses[static_cast<size_t>( AudioBus::Master )] );
        for( size_t bus = 1; bus < m_buses.size(); ++bus )
        {
            FMOD::ChannelGroup* group = nullptr;
            if( Check( m_system->createChannelGroup( AudioBusName( static_cast<AudioBus>( bus ) ), &group ), "createChannelGroup" ) )
            {
                m_buses[bus] = group;
            }
        }
    }
#endif

    const ProjectSettings& settings = ProjectSettings::Get();
    for( size_t bus = 0; bus < m_busVolumes.size(); ++bus )
    {
        SetBusVolume( static_cast<AudioBus>( bus ), settings.BusVolumes[bus] );
    }

    EventManager::GetInstance().RegisterReceiver( this, { PlayAudioEvent::GetEventId(), StopAudioEvent::GetEventId() } );
    s_instance = this;
}


AudioCore::~AudioCore()
{
    Shutdown();
    if( s_instance == this )
    {
        s_instance = nullptr;
    }
}


AudioCore* AudioCore::Get()
{
    return s_instance;
}


bool AudioCore::IsAvailable() const
{
#if USING( ME_FMOD )
    return m_system != nullptr;
#else
    return false;
#endif
}


FMOD::System* AudioCore::GetSystem() const
{
#if USING( ME_FMOD )
    return m_system;
#else
    return nullptr;
#endif
}


FMOD::ChannelGroup* AudioCore::GetBusGroup( AudioBus InBus ) const
{
#if USING( ME_FMOD )
    const size_t index = static_cast<size_t>( InBus );
    return index < m_buses.size() ? m_buses[index] : nullptr;
#else
    return nullptr;
#endif
}


void AudioCore::Shutdown()
{
#if USING( ME_FMOD )
    if( !m_system )
    {
        return;
    }
    StopAll();
    for( auto& cached : m_cachedSounds )
    {
        cached.second->ClearData();
    }
    m_cachedSounds.clear();
    m_oneShots.clear();
    // Free this system's sounds while it is alive and drop them from the cache: holders keep an
    // empty Sound, and the next core loads its own.
    std::vector<std::string> sounds;
    for( auto& entry : ResourceCache::GetInstance().GetResouceStack() )
    {
        Sound* sound = dynamic_cast<Sound*>( entry.second.get() );
        if( sound && sound->System == m_system )
        {
            sound->Release();
            sounds.push_back( entry.first );
        }
    }
    for( const std::string& path : sounds )
    {
        ResourceCache::GetInstance().Evict( path );
    }
    for( size_t bus = 1; bus < m_buses.size(); ++bus )
    {
        if( m_buses[bus] )
        {
            m_buses[bus]->release();
        }
    }
    m_buses.fill( nullptr );
    m_system->release();
    m_system = nullptr;
#endif
}


void AudioCore::SetBusVolume( AudioBus InBus, float InVolume )
{
    const size_t index = static_cast<size_t>( InBus );
    if( index >= m_busVolumes.size() )
    {
        return;
    }
    m_busVolumes[index] = std::clamp( InVolume, 0.f, 1.f );
#if USING( ME_FMOD )
    if( FMOD::ChannelGroup* group = GetBusGroup( InBus ) )
    {
        group->setVolume( m_busVolumes[index] );
    }
#endif
}


float AudioCore::GetBusVolume( AudioBus InBus ) const
{
    const size_t index = static_cast<size_t>( InBus );
    return index < m_busVolumes.size() ? m_busVolumes[index] : 0.f;
}


void AudioCore::SetBusMuted( AudioBus InBus, bool InMuted )
{
    const size_t index = static_cast<size_t>( InBus );
    if( index >= m_busMuted.size() )
    {
        return;
    }
    m_busMuted[index] = InMuted;
#if USING( ME_FMOD )
    if( FMOD::ChannelGroup* group = GetBusGroup( InBus ) )
    {
        group->setMute( InMuted );
    }
#endif
}


bool AudioCore::IsBusMuted( AudioBus InBus ) const
{
    const size_t index = static_cast<size_t>( InBus );
    return index < m_busMuted.size() && m_busMuted[index];
}


void AudioCore::StopAll()
{
#if USING( ME_FMOD )
    if( FMOD::ChannelGroup* master = GetBusGroup( AudioBus::Master ) )
    {
        master->stop();
    }
#endif
    m_oneShots.clear();
}


SharedPtr<Sound> AudioCore::LoadSound( const Path& InClip )
{
#if USING( ME_FMOD )
    if( !m_system || InClip.GetLocalPathString().empty() )
    {
        return nullptr;
    }
    return ResourceCache::GetInstance().Get<Sound>( InClip, m_system, SoundFlags::Default );
#else
    return nullptr;
#endif
}


void AudioCore::InitComponent( AudioSource& InSource )
{
    if( InSource.IsInitialized || InSource.FilePath.GetLocalPathString().empty() )
    {
        return;
    }
    InSource.IsInitialized = true;
#if USING( ME_FMOD )
    InSource.SoundInstance = LoadSound( InSource.FilePath );
    if( !InSource.SoundInstance && m_system )
    {
        YIKES_FMT( "Failed to load sound: %s", InSource.FilePath.GetLocalPathString().c_str() );
    }
#endif
}


AudioVoice AudioCore::Play( const SharedPtr<Sound>& InSound, const AudioPlayParams& InParams )
{
#if USING( ME_FMOD )
    if( !m_system || !InSound || !InSound->Handle )
    {
        return AudioVoice();
    }
    FMOD::Channel* channel = nullptr;
    // Start paused so the bus, volume and position apply before the first sample plays.
    if( !Check( m_system->playSound( InSound->Handle, GetBusGroup( InParams.Bus ), true, &channel ), "playSound" ) || !channel )
    {
        return AudioVoice();
    }
    AudioVoice voice( channel );
    ApplyParams( voice, InParams );
    channel->setPriority( std::clamp( InParams.Priority, 0, 256 ) );
    channel->setPaused( InParams.StartPaused );
    return voice;
#else
    return AudioVoice();
#endif
}


void AudioCore::ApplyParams( AudioVoice& InVoice, const AudioPlayParams& InParams )
{
#if USING( ME_FMOD )
    FMOD::Channel* channel = InVoice.GetChannel();
    if( !channel )
    {
        return;
    }
    const FMOD_MODE wanted = ModeFor( InParams );
    FMOD_MODE mode = 0;
    if( channel->getMode( &mode ) != FMOD_OK )
    {
        return;   // finished
    }
    // Rolloff bits only matter (and are only compared) in 3D.
    FMOD_MODE relevant = FMOD_LOOP_OFF | FMOD_LOOP_NORMAL | FMOD_2D | FMOD_3D;
    if( wanted & FMOD_3D )
    {
        relevant |= FMOD_3D_INVERSEROLLOFF | FMOD_3D_LINEARROLLOFF | FMOD_3D_LINEARSQUAREROLLOFF;
    }
    if( ( mode & relevant ) != ( wanted & relevant ) )
    {
        channel->setMode( wanted );
        if( InParams.Loop )
        {
            channel->setLoopCount( -1 );
        }
    }
    FMOD::ChannelGroup* bus = GetBusGroup( InParams.Bus );
    FMOD::ChannelGroup* current = nullptr;
    if( bus && channel->getChannelGroup( &current ) == FMOD_OK && current != bus )
    {
        channel->setChannelGroup( bus );
    }
    channel->setVolume( std::max( InParams.Volume, 0.f ) );
    channel->setPitch( std::max( InParams.Pitch, 0.01f ) );
    if( InParams.SpatialBlend > 0.f )
    {
        channel->set3DLevel( std::clamp( InParams.SpatialBlend, 0.f, 1.f ) );
        channel->set3DMinMaxDistance( std::max( InParams.MinDistance, 0.001f ), std::max( InParams.MaxDistance, InParams.MinDistance + 0.001f ) );
        channel->set3DDopplerLevel( std::clamp( InParams.DopplerLevel, 0.f, 5.f ) );
        InVoice.SetPosition( InParams.Position, InParams.Velocity );
    }
#endif
}


AudioVoice AudioCore::PlayOneShot( const Path& InClip, const AudioPlayParams& InParams, int InMaxVoices )
{
    SharedPtr<Sound> sound = LoadSound( InClip );
    if( !sound )
    {
        return AudioVoice();
    }
    std::vector<AudioVoice>& voices = m_oneShots[sound.get()];
    voices.erase( std::remove_if( voices.begin(), voices.end(), []( const AudioVoice& InVoice ) { return !InVoice.IsValid(); } ), voices.end() );
    while( !voices.empty() && static_cast<int>( voices.size() ) >= std::max( InMaxVoices, 1 ) )
    {
        voices.front().Stop();
        voices.erase( voices.begin() );
    }
    AudioPlayParams params = InParams;
    params.Loop = false;
    AudioVoice voice = Play( sound, params );
    if( voice.IsValid() )
    {
        voices.push_back( voice );
    }
    return voice;
}


void AudioCore::OnStart()
{
    m_started = true;
    for( Entity& entity : GetEntities() )
    {
        AudioSource& source = entity.GetComponent<AudioSource>();
        if( source.PlayOnAwake )
        {
            source.m_pendingPlay = true;
            source.m_voiceLoops = source.Loop;
        }
    }
}


void AudioCore::OnStop()
{
    m_started = false;
    for( auto& cached : m_cachedSounds )
    {
        cached.second->Stop();
        cached.second->ClearData();
    }
    m_cachedSounds.clear();
    // Leaving play: everything the game started stops (and nothing stays paused).
    StopAll();
    for( Entity& entity : GetEntities() )
    {
        entity.GetComponent<AudioSource>().m_pendingPlay = false;
    }
    if( m_paused )
    {
        Tick( 0.f, false );
    }
}


void AudioCore::OnEntityAdded( Entity& InEntity )
{
    AudioSource& source = InEntity.GetComponent<AudioSource>();
    InitComponent( source );
    if( m_started && source.PlayOnAwake )
    {
        // Spawned during play.
        source.m_pendingPlay = true;
        source.m_voiceLoops = source.Loop;
    }
}


void AudioCore::OnEntityRemoved( Entity& InEntity )
{
    if( AudioSource* source = InEntity.TryGetComponent<AudioSource>() )
    {
        source->StopAll();
        source->SoundInstance = nullptr;
        source->IsInitialized = false;
    }
}


Transform* AudioCore::FindListener()
{
    Transform* listener = nullptr;
    if( m_started )
    {
        GetWorld().Each<Transform, AudioListener>( [&listener]( Entity&, Transform& InTransform, AudioListener& ) {
            if( !listener )
            {
                listener = &InTransform;
            }
        } );
    }
    Camera* camera = m_started || !Camera::EditorCamera ? Camera::CurrentCamera : Camera::EditorCamera;
    if( !listener && camera && camera->Parent )
    {
        listener = camera->Parent->TryGetComponent<Transform>();
    }
    return listener;
}


void AudioCore::UpdateListener( float InDeltaSeconds )
{
#if USING( ME_FMOD )
    Transform* listener = FindListener();
    if( !listener || !m_system )
    {
        return;
    }
    const Vector3 position = listener->GetWorldPosition();
    const Vector3 velocity = m_hasListenerPosition ? MotionVelocity( m_listenerPosition, position, InDeltaSeconds ) : Vector3();
    m_listenerPosition = position;
    m_hasListenerPosition = true;
    // FMOD's default space matches the engine's: left-handed, +Y up, +Z forward.
    Vector3 forward = listener->Front();
    Vector3 up = listener->Up();
    forward = forward.Length() > 0.f ? forward.Normalized() : Vector3( 0.f, 0.f, 1.f );
    up = up.Length() > 0.f ? up.Normalized() : Vector3( 0.f, 1.f, 0.f );
    const FMOD_VECTOR fmodPosition = ToFmod( position );
    const FMOD_VECTOR fmodVelocity = ToFmod( velocity );
    const FMOD_VECTOR fmodForward = ToFmod( forward );
    const FMOD_VECTOR fmodUp = ToFmod( up );
    m_system->set3DListenerAttributes( 0, &fmodPosition, &fmodVelocity, &fmodForward, &fmodUp );
#endif
}


void AudioCore::Update( const UpdateContext& InContext )
{
    Tick( InContext.GetUnscaledDeltaTime(), InContext.IsPaused() );
}


void AudioCore::Tick( float InDeltaSeconds, bool InPaused )
{
    OPTICK_CATEGORY( "AudioCore Update", Optick::Category::Audio );
#if USING( ME_FMOD )
    if( !m_system )
    {
        return;
    }
    if( InPaused != m_paused )
    {
        m_paused = InPaused;
        for( AudioBus bus : { AudioBus::Music, AudioBus::SFX, AudioBus::Voice } )
        {
            if( FMOD::ChannelGroup* group = GetBusGroup( bus ) )
            {
                group->setPaused( m_paused );
            }
        }
    }

    UpdateListener( InDeltaSeconds );

    for( Entity& entity : GetEntities() )
    {
        AudioSource& source = entity.GetComponent<AudioSource>();
        if( source.IsInitialized && source.SoundInstance && source.SoundInstance->GetPath().GetLocalPathString() != source.FilePath.GetLocalPathString() )
        {
            // The clip was changed in place (inspector, script): reload it.
            source.StopAll();
            source.SoundInstance = nullptr;
            source.IsInitialized = false;
        }
        if( !source.IsInitialized )
        {
            InitComponent( source );
        }
        Vector3 position = source.m_position;
        if( Transform* transform = entity.TryGetComponent<Transform>() )
        {
            position = transform->GetWorldPosition();
        }
        const Vector3 velocity = source.m_hasPosition ? MotionVelocity( source.m_position, position, InDeltaSeconds ) : Vector3();
        if( source.m_pendingPlay && source.SoundInstance && entity.IsActiveInHierarchy() && source.IsEnabled() )
        {
            source.m_position = position;
            source.m_hasPosition = true;
            source.Play( source.m_voiceLoops, false );
        }
        source.UpdateVoices( position, velocity );
    }

    m_system->update();
#endif
}


bool AudioCore::OnEvent( const BaseEvent& InEvent )
{
    if( InEvent.GetEventId() == PlayAudioEvent::GetEventId() )
    {
        const PlayAudioEvent& evt = static_cast<const PlayAudioEvent&>( InEvent );
        const Path soundPath( evt.SourceName );
        const std::string sound = soundPath.GetLocalPathString();
        if( sound.empty() )
        {
            return true;
        }

        if( !evt.Callback )
        {
            // Fire-and-forget: overlapping plays each get a voice.
            AudioPlayParams params;
            params.Bus = evt.Bus;
            params.Volume = evt.Volume;
            if( evt.Spatial )
            {
                params.SpatialBlend = 1.f;
                params.Position = evt.Position;
            }
            AudioVoice voice = PlayOneShot( soundPath, params );
            if( evt.StartPercent > 0.f )
            {
                SharedPtr<Sound> loaded = LoadSound( soundPath );
                unsigned int length = 0;
#if USING( ME_FMOD )
                if( loaded && loaded->Handle )
                {
                    loaded->Handle->getLength( &length, FMOD_TIMEUNIT_MS );
                }
#endif
                voice.SetPositionMs( static_cast<unsigned int>( length * evt.StartPercent ) );
            }
            return true;
        }

        // Callers that want the source back get one cached source per path (it restarts on replay).
        auto cached = m_cachedSounds.find( sound );
        if( cached == m_cachedSounds.end() )
        {
            cached = m_cachedSounds.emplace( sound, MakeShared<AudioSource>( sound ) ).first;
            cached->second->Bus = evt.Bus;
            InitComponent( *cached->second );
        }
        SharedPtr<AudioSource> source = cached->second;
        source->Play( false );
        source->SetVolume( evt.Volume );
        source->SetPositionMs( static_cast<unsigned int>( source->GetLength() * evt.StartPercent ) );
        evt.Callback( source );
        return true;
    }

    if( InEvent.GetEventId() == StopAudioEvent::GetEventId() )
    {
        const StopAudioEvent& evt = static_cast<const StopAudioEvent&>( InEvent );
        const std::string sound = Path( evt.SourceName ).GetLocalPathString();
        auto cached = m_cachedSounds.find( sound );
        if( cached != m_cachedSounds.end() )
        {
            cached->second->Stop();
            m_cachedSounds.erase( cached );
        }
        if( SharedPtr<Sound> loaded = sound.empty() ? nullptr : LoadSound( Path( sound ) ) )
        {
            auto voices = m_oneShots.find( loaded.get() );
            if( voices != m_oneShots.end() )
            {
                for( AudioVoice& voice : voices->second )
                {
                    voice.Stop();
                }
                m_oneShots.erase( voices );
            }
        }
    }

    return false;
}
