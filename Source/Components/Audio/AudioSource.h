#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Audio/AudioTypes.h"
#include "Path.h"
#include "Resource/MetaFile.h"
#include "Resource/MetaRegistry.h"
#include "Resources/SoundResource.h"
#include <vector>

#if USING( ME_FMOD )
#include "fmod.hpp"
namespace FMOD {
    class System;
}
#endif

// Plays a sound clip on a mixer bus, in 2D or positioned in 3D at its entity (SpatialBlend), with
// distance rolloff and doppler against the AudioListener. Play() drives the source's own voice;
// PlayOneShot() layers overlapping voices that follow the source. AudioCore loads the clip, starts
// PlayOnAwake sources when the world starts and keeps the voices in sync with these fields.
class AudioSource
    : public Component<AudioSource>
{
    ME_REFLECTABLE( AudioSource )
    friend class AudioCore;
public:
    AudioSource();
    AudioSource( const std::string& InFilePath );

    Path FilePath;
    AudioBus Bus = AudioBus::SFX;
    float Volume = 1.f;
    float Pitch = 1.f;          // playback rate: 2 = an octave up and twice as fast
    bool Mute = false;
    bool PlayOnAwake = false;
    bool Loop = false;
    float SpatialBlend = 0.f;   // 0 = 2D, 1 = 3D
    float MinDistance = 1.f;
    float MaxDistance = 50.f;
    AudioRolloff Rolloff = AudioRolloff::Inverse;
    float DopplerLevel = 1.f;
    int Priority = 128;
    int MaxOneShots = 8;        // overlapping PlayOneShot voices; the oldest stops beyond this
    bool Preload = false;

    // Starts the source's voice (restarting it if playing) with the Loop setting.
    void Play();
    void Play( bool ShouldLoop, bool StartPaused = false );
    // An overlapping voice of this source's clip (or another), at the source with its settings.
    AudioVoice PlayOneShot( float InVolumeScale = 1.f );
    AudioVoice PlayOneShot( const Path& InClip, float InVolumeScale = 1.f );
    void Pause();
    void Resume();
    // Stops the source's voice (one-shots keep playing; StopAll stops them too).
    void Stop( bool immediate = true );
    void StopAll();

    bool IsLoaded() const;
    bool IsPlaying() const;
    bool IsPaused() const;

    unsigned int GetLength() const;
    unsigned int GetPositionMs() const;
    void SetPositionMs( unsigned int position );
    void SetPositionPercent( float positionPercent );
    float GetVolume();
    void SetVolume( float inVolumePercent );
    void SetPlaybackSpeed( float inSpeed );
    float GetPlaybackSpeed();
    // The main voice's level after bus and distance attenuation (0 when not playing).
    float GetAudibility() const;
    AudioVoice GetVoice() const
    {
        return m_voice;
    }
    // The settings this source's voices play with.
    AudioPlayParams MakePlayParams() const;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() override;
#endif
    virtual void Init() override;

    void ClearData();

    bool IsInitialized = false;

    SharedPtr<Sound> SoundInstance = nullptr;
#if USING( ME_FMOD )
    FMOD::Channel* ChannelHandle = nullptr;
#endif

private:
    void OnDeserialize( const json& inJson ) override;

    struct OneShot
    {
        AudioVoice Voice;
        float VolumeScale = 1.f;
    };

    // Called by AudioCore every frame with the entity's world position and velocity.
    void UpdateVoices( const Vector3& InPosition, const Vector3& InVelocity );
    Vector3 GetWorldPosition() const;

    AudioVoice m_voice;
    bool m_voiceLoops = false;
    std::vector<OneShot> m_oneShots;
    bool m_pendingPlay = false;
    Vector3 m_position;
    Vector3 m_velocity;
    bool m_hasPosition = false;
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
