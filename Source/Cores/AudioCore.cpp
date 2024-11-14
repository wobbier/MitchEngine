#include "PCH.h"
#include "AudioCore.h"
#include "Components/Audio/AudioSource.h"
#include "Events/AudioEvents.h"
#include "optick.h"
#include "Resource/ResourceCache.h"

#if USING( ME_FMOD )
#include "fmod.hpp"
#endif

AudioCore::AudioCore()
    : Base( ComponentFilter().Requires<AudioSource>() )
{
    SetIsSerializable( false );

#if USING( ME_FMOD )
    FMOD_RESULT res;
    res = FMOD::System_Create( &system );
    if( res != FMOD_OK )
    {
        YIKES( "FMOD Failed to initialize: "/* + FMOD_ErrorString(res)*/ );
    }

    res = system->init( 512, FMOD_INIT_NORMAL, 0 );
    if( res != FMOD_OK )
    {
        YIKES( "FMOD System failed to create!" );
    }
    IsInitialized = true;
#endif

    std::vector<TypeId> events = {
        PlayAudioEvent::GetEventId(),
        StopAudioEvent::GetEventId()
    };
    EventManager::GetInstance().RegisterReceiver( this, events );
}

void AudioCore::Update( float dt )
{
    OPTICK_CATEGORY( "AudioCore Update", Optick::Category::Audio );

#if USING( ME_FMOD )
    if( system )
    {
        system->update();
    }
#endif
    auto& AudioEntities = GetEntities();
    for( auto& ent : AudioEntities )
    {
        AudioSource& audioSource = ent.GetComponent<AudioSource>();

        InitComponent( audioSource );
    }
}

void AudioCore::InitComponent( AudioSource& audioSource )
{
    if( !audioSource.IsInitialized && !audioSource.FilePath.GetLocalPath().empty() )
    {
#if USING( ME_FMOD )
        // #TODO: Add support for audio flags.
        SharedPtr<Sound> soundResource = ResourceCache::GetInstance().Get<Sound>( audioSource.FilePath, system, SoundFlags::Default );
        if( !soundResource )
        {
            YIKES_FMT( "Failed to load sound: %s", audioSource.FilePath.GetLocalPathString().c_str() );
            audioSource.IsInitialized = true;
            return;
        }
        audioSource.SoundInstance = soundResource;
#endif
        audioSource.IsInitialized = true;
    }
}

void AudioCore::OnEntityAdded( Entity& NewEntity )
{
    AudioSource& comp = NewEntity.GetComponent<AudioSource>();

    InitComponent( comp );
}

void AudioCore::OnEntityRemoved( Entity& NewEntity )
{
    AudioSource& comp = NewEntity.GetComponent<AudioSource>();
    comp.Stop();
    comp.ClearData();
}

bool AudioCore::OnEvent( const BaseEvent& InEvent )
{
    if( InEvent.GetEventId() == PlayAudioEvent::GetEventId() )
    {
        const PlayAudioEvent& evt = static_cast<const PlayAudioEvent&>( InEvent );
        Path soundPath = Path( evt.SourceName );
        auto sound = soundPath.GetLocalPath();
        if( m_cachedSounds.find( sound.data() ) == m_cachedSounds.end() )
        {
            auto& source = m_cachedSounds[sound.data()] = MakeShared<AudioSource>( sound.data() );
            InitComponent( *source );
        }
        auto& cachedSound = m_cachedSounds[sound.data()];
        cachedSound->Play( false );
        cachedSound->SetVolume( evt.Volume );
        cachedSound->SetPositionMs( cachedSound->GetLength() * evt.StartPercent );

        if( evt.Callback )
        {
            evt.Callback( m_cachedSounds[sound.data()] );
        }

        return true;
    }

    if( InEvent.GetEventId() == StopAudioEvent::GetEventId() )
    {
        const StopAudioEvent& evt = static_cast<const StopAudioEvent&>( InEvent );
        Path soundPath = Path( evt.SourceName );
        auto sound = soundPath.GetLocalPath();
        if( m_cachedSounds.find( sound.data() ) != m_cachedSounds.end() )
        {
            m_cachedSounds.erase( sound.data() );
            //auto& source = m_cachedSounds[sound.data()] = MakeShared<AudioSource>( sound.data() );
            //InitComponent( *source );
        }
    }

    return false;
}

FMOD::System* AudioCore::GetSystem() const
{
    return system;
}

void AudioCore::Init()
{

}

void AudioCore::OnStart()
{
    auto AudioEntities = GetEntities();
    for( auto& ent : AudioEntities )
    {
        AudioSource& audioSource = ent.GetComponent<AudioSource>();
        if( audioSource.PlayOnAwake )
        {
            audioSource.Play( audioSource.Loop );
        }
    }
}

void AudioCore::OnStop()
{
    for( auto& audioSource : m_cachedSounds )
    {
        audioSource.second->Stop();
        audioSource.second->ClearData();
    }
    m_cachedSounds.clear();
}
