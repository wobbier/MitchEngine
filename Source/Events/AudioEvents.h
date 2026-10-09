#pragma once
#include "Audio/AudioTypes.h"
#include "Events/EventManager.h"
#include "Events/Event.h"
#include "Math/Vector3.h"

class AudioSource;

class PlayAudioEvent
    : public Event<PlayAudioEvent>
{
public:
    PlayAudioEvent() = default;
    PlayAudioEvent( const std::string& InSource, bool inImmediate = true )
        : SourceName( InSource )
        , Immediate( inImmediate )
    {
    }
    std::string SourceName;
    float Volume = 1.f;
    float StartPercent = 0.f;
    bool Immediate = true;
    AudioBus Bus = AudioBus::SFX;
    // Plays in 3D at Position instead of in 2D.
    bool Spatial = false;
    Vector3 Position;

    // With a callback, the sound plays on one cached AudioSource per path (replaying restarts it)
    // that the callback receives; without one, every event is an overlapping one-shot.
    std::function<void( SharedPtr<AudioSource> sound )> Callback;
};

class StopAudioEvent
    : public Event<StopAudioEvent>
{
public:
    StopAudioEvent() = default;
    StopAudioEvent( const std::string& InSource )
        : SourceName( InSource )
    {
    }
    std::string SourceName;
};