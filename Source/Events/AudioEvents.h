#pragma once
#include "Events/EventManager.h"
#include "Events/Event.h"

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