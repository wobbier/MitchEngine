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

    std::function<void( SharedPtr<AudioSource> sound )> Callback;
};