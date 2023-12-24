#pragma once
#include "Events/EventManager.h"
#include "Events/Event.h"

class AudioSource;

class PlayAudioEvent
    : public Event<PlayAudioEvent>
{
public:
    PlayAudioEvent() = default;
    PlayAudioEvent( const std::string& InSource )
        : SourceName( InSource )
    {
    }
    std::string SourceName;
    float Volume = 1.f;
    float StartPercent = 0.f;

    std::function<void( SharedPtr<AudioSource> sound )> Callback;
};