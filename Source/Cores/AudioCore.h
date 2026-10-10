#pragma once
#include "ECS/Core.h"
#include "Audio/AudioTypes.h"
#include "Events/EventReceiver.h"
#include "Path.h"
#include "Pointers.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

class AudioSource;
class AudioReverbZone;
class Sound;
class Transform;
namespace FMOD
{
    class System;
    class ChannelGroup;
}

// The engine's audio system (engine-owned, on FMOD). Each frame it:
//   1. places the listener: the first active AudioListener, else the game camera (the editor camera
//      in edit mode), with a velocity for doppler;
//   2. loads AudioSources' clips, starts PlayOnAwake sources once the world starts, and keeps every
//      source's voices on its bus, volume, pitch and 3D position;
//   3. keeps AudioReverbZones' FMOD reverbs at their entities, and occludes 3D sources whose line to
//      the listener is blocked (OcclusionQuery, a few times a second per source, smoothed);
//   4. pauses the game buses while the engine is paused, and updates FMOD.
// Overlapping one-shots (PlayOneShot, PlayAudioEvent) each get their own voice.
class AudioCore final
    : public Core<AudioCore>
    , public EventReceiver
{
public:
    explicit AudioCore( AudioOutput InOutput = AudioOutput::Device );
    ~AudioCore() override;

    // The engine's audio core (the most recently created one), or null.
    static AudioCore* Get();

    void Update( const UpdateContext& InContext ) final;
    // One audio frame: listener, sources, pause state, FMOD update. Update calls this.
    void Tick( float InDeltaSeconds, bool InPaused = false );

    void OnEntityAdded( Entity& InEntity ) final;
    void OnEntityRemoved( Entity& InEntity ) final;
    bool OnEvent( const BaseEvent& InEvent ) final;

    // Fire-and-forget: every call gets a new voice, up to InMaxVoices of the same clip at once (the
    // oldest is stopped beyond that).
    AudioVoice PlayOneShot( const Path& InClip, const AudioPlayParams& InParams = AudioPlayParams(), int InMaxVoices = 16 );
    // Starts a voice of an already loaded sound.
    AudioVoice Play( const SharedPtr<Sound>& InSound, const AudioPlayParams& InParams );
    SharedPtr<Sound> LoadSound( const Path& InClip );
    // Loads an AudioSource's clip now (sources added to the world load on their own).
    void InitComponent( AudioSource& InSource );

    void SetBusVolume( AudioBus InBus, float InVolume );
    float GetBusVolume( AudioBus InBus ) const;
    void SetBusMuted( AudioBus InBus, bool InMuted );
    bool IsBusMuted( AudioBus InBus ) const;
    bool IsPaused() const
    {
        return m_paused;
    }
    // Stops every voice on every bus.
    void StopAll();

    Vector3 GetListenerPosition() const
    {
        return m_listenerPosition;
    }
    // True when no audio device is in use (Silent / Manual output, or the device failed to open).
    bool IsSilent() const
    {
        return m_output != AudioOutput::Device;
    }
    bool IsAvailable() const;
    FMOD::System* GetSystem() const;

    // Applies params to a playing voice (mode, bus, volume, pitch, 3D settings and position).
    void ApplyParams( AudioVoice& InVoice, const AudioPlayParams& InParams );

    // How many obstacles block the line from the listener to a source (the engine counts colliders
    // with physics raycasts; without a query nothing is occluded).
    using OcclusionQuery = std::function<int( const Vector3& InListener, const Vector3& InSource, Entity* InListenerEntity, Entity& InSourceEntity )>;
    void SetOcclusionQuery( OcclusionQuery InQuery );
    // Seconds between a source's occlusion checks.
    static constexpr float kOcclusionInterval = 0.1f;

    // Stops everything and releases FMOD (engine shutdown); the core is inert afterwards.
    void Shutdown();

private:
    void OnStart() final;
    void OnStop() final;

    void UpdateListener( float InDeltaSeconds );
    Transform* FindListener();
    void UpdateReverbZone( Entity& InEntity, AudioReverbZone& InZone );
    void ReleaseReverbZone( AudioReverbZone& InZone );
    void UpdateOcclusion( Entity& InEntity, AudioSource& InSource, const Vector3& InPosition, float InDeltaSeconds );
    FMOD::ChannelGroup* GetBusGroup( AudioBus InBus ) const;

    AudioOutput m_output = AudioOutput::Device;
    bool m_started = false;
    bool m_paused = false;
    Vector3 m_listenerPosition;
    bool m_hasListenerPosition = false;
    EntityHandle m_listenerEntity;
    OcclusionQuery m_occlusionQuery;
    std::array<float, static_cast<size_t>( AudioBus::Count )> m_busVolumes;
    std::array<bool, static_cast<size_t>( AudioBus::Count )> m_busMuted;

    // Legacy PlayAudioEvent callers that want the playing source back (one per path).
    std::map<std::string, SharedPtr<AudioSource>> m_cachedSounds;
    // Live one-shot voices per clip, for the per-clip voice limit.
    std::map<const Sound*, std::vector<AudioVoice>> m_oneShots;

#if USING( ME_FMOD )
    FMOD::System* m_system = nullptr;
    std::array<FMOD::ChannelGroup*, static_cast<size_t>( AudioBus::Count )> m_buses{};
#endif
};

ME_REGISTER_CORE( AudioCore );
