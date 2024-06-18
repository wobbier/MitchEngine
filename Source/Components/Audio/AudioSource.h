#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Path.h"
#include "Resource/MetaFile.h"
#include "Resource/MetaRegistry.h"
#include "Resources/SoundResource.h"

#if USING( ME_FMOD )
#include "fmod.hpp"
namespace FMOD {
    class System;
}
#endif

class AudioSource
    : public Component<AudioSource>
{
public:
    AudioSource();
    AudioSource( const std::string& InFilePath );

    void Play( const bool ShouldLoop = false );
    void Pause();
    void Resume();
    void Stop( bool immediate = true );
    bool Preload = false;

    bool IsLoaded() const;
    bool IsPlaying() const;
    bool IsPaused() const;

    unsigned int GetLength();
    unsigned int GetPositionMs();
    void SetPositionMs( unsigned int position );
    void SetPositionPercent( float positionPercent );
    void SetPlaybackSpeed( float inSpeed );
    float GetPlaybackSpeed();

    void SetVolume( float inVolumePercent );

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() override;
#endif
    virtual void Init() override;

    void ClearData();

    bool IsInitialized = false;

    bool PlayOnAwake = false;
    bool Loop = false;

#if USING( ME_FMOD )
    SharedPtr<Sound> SoundInstance = nullptr;
    FMOD::Channel* ChannelHandle = nullptr;
#endif

    Path FilePath;

private:
    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;

    float PlaybackSpeed = 1.f;
    float Frequency = 1.f;

#if USING( ME_FMOD )
    FMOD::System* m_owner = nullptr;
#endif
};

ME_REGISTER_COMPONENT_FOLDER( AudioSource, "Audio" )

struct AudioResourceMetadata
    : public MetaBase
{
    enum class OutputTextureType : uint8_t
    {
        Default = 0,
        NormalMap,
        Sprite,
        Count
    };

    AudioResourceMetadata( const Path& filePath )
        : MetaBase( filePath )
    {
    }

    virtual std::string GetExtension2() const override;

    void OnSerialize( json& inJson ) override;
    void OnDeserialize( const json& inJson ) override;

#if USING( ME_EDITOR )
    void Export() override;

    virtual void OnEditorInspect() final;
#endif

};

struct AudioResourceMetadataMp3
    : public AudioResourceMetadata
{
    AudioResourceMetadataMp3( const Path& filePath ) : AudioResourceMetadata( filePath ) {}
    virtual std::string GetExtension2() const override;
};


ME_REGISTER_METADATA( "wav", AudioResourceMetadata );
ME_REGISTER_METADATA( "mp3", AudioResourceMetadataMp3 );
