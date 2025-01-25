#include "PCH.h"
#include "AudioSource.h"
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

AudioSource::AudioSource( const std::string& InFilePath )
    : Component( "AudioSource" )
    , FilePath( InFilePath )
#if USING( ME_FMOD )
    , SoundInstance( nullptr )
#endif
{
    if( Preload )
    {

    }
}

AudioSource::AudioSource()
    : Component( "AudioSource" )
#if USING( ME_FMOD )
    , SoundInstance( nullptr )
#endif
{

}

void AudioSource::Play( bool ShouldLoop )
{
#if USING( ME_FMOD )
    if( SoundInstance )
    {
        if( IsPlaying() )
        {
            Stop( true );
        }
        SoundInstance->Handle->getSystemObject( &m_owner );
        m_owner->playSound( SoundInstance->Handle, nullptr, false, &ChannelHandle );
        if( ChannelHandle && ChannelHandle->getFrequency( &Frequency ) != FMOD_OK )
        {
        }
    }
#endif
}

void AudioSource::Pause()
{
#if USING( ME_FMOD )
    if( ChannelHandle )
    {
        ChannelHandle->setPaused( true );
    }
#endif
}

void AudioSource::Resume()
{
#if USING( ME_FMOD )
    if( ChannelHandle )
    {
        ChannelHandle->setPaused( false );
    }
#endif
}

void AudioSource::Stop( bool immediate )
{
#if USING( ME_FMOD )
    if( ChannelHandle )
    {
        FMOD_RESULT result = ChannelHandle->stop();
        if( result == FMOD_OK )
        {
            ChannelHandle = nullptr;
        }
        else
        {
            std::cerr << "Error stopping channel: " << FMOD_ErrorString( result ) << std::endl;
        }
    }
#endif
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
#if USING( ME_FMOD )
    bool isPlaying;
    return ( ChannelHandle && ChannelHandle->isPlaying( &isPlaying ) == FMOD_OK && isPlaying );
#else
    return false;
#endif
}

bool AudioSource::IsPaused() const
{
#if USING( ME_FMOD )
    bool isPaused;
    return ( ChannelHandle && ChannelHandle->getPaused( &isPaused ) == FMOD_OK && isPaused );
#else
    return false;
#endif
}

unsigned int AudioSource::GetLength()
{
#if USING( ME_FMOD )
    unsigned int isPlaying = 0;
    if( SoundInstance && SoundInstance->Handle && SoundInstance->Handle->getLength( &isPlaying, FMOD_TIMEUNIT_MS ) != FMOD_OK )
    {
    }
    return isPlaying;
#else
    return 0.f;
#endif
}

unsigned int AudioSource::GetPositionMs()
{
#if USING( ME_FMOD )
    unsigned int isPlaying = 0;
    if( ChannelHandle && ChannelHandle->getPosition( &isPlaying, FMOD_TIMEUNIT_MS ) != FMOD_OK )
    {
    }
    return isPlaying;
#else
    return 0.f;
#endif
}


#if USING( ME_FMOD )
void checkFmodError( FMOD_RESULT result, const char* message = "" )
{
    if( result != FMOD_OK )
    {
        std::cerr << "FMOD error (" << result << "): " << FMOD_ErrorString( result );
        if( message && message[0] != '\0' )
        {
            std::cerr << " - " << message;
        }
        std::cerr << std::endl;
    }
}
#endif


float AudioSource::GetVolume()
{
    float volume = 1.f;
#if USING( ME_FMOD )
    if( !ChannelHandle )
        return volume;
    
    FMOD_RESULT result = ChannelHandle->getVolume( &volume );
    if( result != FMOD_OK )
    {
        checkFmodError( result );
    }
#endif
    return volume;
}

void AudioSource::SetPositionMs( unsigned int position )
{
#if USING( ME_FMOD )
    if( ChannelHandle && ChannelHandle->setPosition( position, FMOD_TIMEUNIT_MS ) != FMOD_OK )
    {
    }
#endif
}

void AudioSource::SetPositionPercent( float positionPercent )
{
#if USING( ME_FMOD )
    if( ChannelHandle && ChannelHandle->setPosition( GetLength() * positionPercent, FMOD_TIMEUNIT_MS ) != FMOD_OK )
    {
    }
#endif
}

void AudioSource::SetPlaybackSpeed( float inSpeed )
{
    PlaybackSpeed = inSpeed;
#if USING( ME_FMOD )
    if( ChannelHandle && ChannelHandle->setFrequency( Frequency * PlaybackSpeed ) != FMOD_OK )
    {
    }
#endif
}

float AudioSource::GetPlaybackSpeed()
{
    return PlaybackSpeed;
}

void AudioSource::SetVolume( float inVolumePercent )
{
#if USING( ME_FMOD )
    if( !ChannelHandle )
        return;
    FMOD_RESULT result = ChannelHandle->setVolume( inVolumePercent );
    checkFmodError( result );
#endif
}

void AudioSource::OnSerialize( json& outJson )
{
    outJson["FilePath"] = FilePath.GetLocalPath();
    outJson["PlayOnAwake"] = PlayOnAwake;
    outJson["Loop"] = Loop;
}

void AudioSource::OnDeserialize( const json& inJson )
{
    if( inJson.contains( "FilePath" ) )
    {
        FilePath = Path( inJson["FilePath"] );
    }
    if( inJson.contains( "PlayOnAwake" ) )
    {
        PlayOnAwake = inJson["PlayOnAwake"];
    }
    if( inJson.contains( "Loop" ) )
    {
        Loop = inJson["Loop"];
    }
}

#if USING( ME_EDITOR )
void AudioSource::OnEditorInspect()
{
#if !USING( ME_FMOD )
    ImGui::Text( "FMOD NOT ENABLED! See Help > About." );
    return;
#endif
    ImVec2 selectorSize( -1.f, 19.f );

    HavanaUtils::Label( "Asset" );
    auto soundPath = FilePath.GetLocalPath();
    if( !soundPath.empty() )
    {
        selectorSize = ImVec2( ImGui::GetContentRegionAvail().x - 19.f, 19.f );
    }
    if( ImGui::Button( soundPath.empty() ? "Select Asset" : soundPath.data(), selectorSize ) )
    {
        RequestAssetSelectionEvent evt( [this]( Path selectedAsset ) {
            ClearData();
            FilePath = selectedAsset;
            }, AssetType::Audio );
        evt.Fire();
    }
    if( ImGui::BeginDragDropTarget() )
    {
        if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( AssetDescriptor::kDragAndDropPayload ) )
        {
            IM_ASSERT( payload->DataSize == sizeof( AssetDescriptor ) );
            AssetDescriptor& payload_n = *(AssetDescriptor*)payload->Data;

            if( payload_n.Type == AssetType::Audio )
            {
                ClearData();
                FilePath = payload_n.FullPath;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if( !soundPath.empty() )
    {
        ImGui::SameLine();
        if( ImGui::Button( "X" ) )
        {
            ClearData();
        }
    }

    if( ImGui::Checkbox( "Play On Awake", &PlayOnAwake ) )
    {
    }

    if( ImGui::Checkbox( "Loop", &Loop ) )
    {
    }

    if( IsPlaying() )
    {
        if( ImGui::Button( "Stop" ) )
        {
            Stop();
        }
    }
    else if( ImGui::Button( "Play" ) )
    {
        Play( false );
    }
}
#endif

void AudioSource::Init()
{
}

void AudioSource::ClearData()
{
#if USING( ME_FMOD )
    SoundInstance = nullptr;
    m_owner = nullptr;
#endif

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
