#pragma once
#include "Math/Vector3.h"
#include <cstdint>

namespace FMOD { class Channel; }

// Mixer buses. Each is a channel group under the master group; their volumes live in Project Settings.
// Music, SFX and Voice pause with the game; UI keeps playing.
enum class AudioBus : uint8_t
{
    Master = 0,
    Music,
    SFX,
    UI,
    Voice,

    Count
};

const char* AudioBusName( AudioBus InBus );

// How a 3D sound fades with distance from the listener.
enum class AudioRolloff : uint8_t
{
    Inverse = 0,    // realistic: full volume inside MinDistance, then halving with each doubling of distance (no further fade past MaxDistance)
    Linear,         // full volume at MinDistance down to silence at MaxDistance
    LinearSquare,   // like Linear, falling off faster near MaxDistance
};

// Where the mixer sends its output.
enum class AudioOutput : uint8_t
{
    Device = 0,     // the default audio device; falls back to Silent when none can be opened
    Silent,         // mixes in real time without a device (automated runs, --no-audio)
    Manual,         // mixes only when the core updates (deterministic; unit tests)
};

// Settings a voice plays with: an AudioSource's fields, or a one-shot's parameters.
struct AudioPlayParams
{
    AudioBus Bus = AudioBus::SFX;
    float Volume = 1.f;
    float Pitch = 1.f;
    bool Loop = false;
    bool StartPaused = false;
    // 0 = 2D (no panning or distance fade), 1 = fully positioned in 3D.
    float SpatialBlend = 0.f;
    Vector3 Position;
    Vector3 Velocity;
    float MinDistance = 1.f;
    float MaxDistance = 50.f;
    AudioRolloff Rolloff = AudioRolloff::Inverse;
    float DopplerLevel = 1.f;
    int Priority = 128;     // 0 (most important) to 256; the least important voices go virtual first
    // Muffling by obstacles between the source and the listener, 0 clear to 1 fully occluded
    // (lowpass and attenuation of the 3D part).
    float Occlusion = 0.f;
    // Send to the reverb zones, scaled by SpatialBlend: 2D sounds (music, UI) stay dry.
    float ReverbMix = 1.f;
};

// Reverb zone characters (FMOD's presets).
enum class ReverbPreset : uint8_t
{
    Generic = 0,
    PaddedCell,
    Room,
    Bathroom,
    LivingRoom,
    StoneRoom,
    Auditorium,
    ConcertHall,
    Cave,
    Arena,
    Hangar,
    CarpetedHallway,
    Hallway,
    StoneCorridor,
    Alley,
    Forest,
    City,
    Mountains,
    Quarry,
    Plain,
    ParkingLot,
    SewerPipe,
    Underwater,
};

// One playing instance of a sound. Cheap to copy and safe to keep after the sound ends: every call on
// a finished voice does nothing (FMOD channel handles are generation-checked).
class AudioVoice
{
public:
    AudioVoice() = default;
    explicit AudioVoice( FMOD::Channel* InChannel )
        : m_channel( InChannel )
    {
    }

    // Playing or paused (not finished or stopped).
    bool IsValid() const;
    bool IsPlaying() const;
    bool IsPaused() const;
    void Stop();
    void SetPaused( bool InPaused );
    void SetVolume( float InVolume );
    void SetPitch( float InPitch );
    void SetPosition( const Vector3& InPosition, const Vector3& InVelocity = Vector3() );
    unsigned int GetPositionMs() const;
    void SetPositionMs( unsigned int InPosition );
    // The voice's final level after its volume, bus and distance attenuation (0..1).
    float GetAudibility() const;

    FMOD::Channel* GetChannel() const
    {
        return m_channel;
    }

private:
    FMOD::Channel* m_channel = nullptr;
};
