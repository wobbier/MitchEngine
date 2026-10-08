#pragma once
#include "Event.h"

// Inherit and register with EventManager::RegisterReceiver. Deregisters itself on destruction.
class EventReceiver
{
public:
    EventReceiver() = default;
    EventReceiver( const EventReceiver& ) = delete;
    EventReceiver& operator=( const EventReceiver& ) = delete;
    virtual ~EventReceiver();

    virtual bool OnEvent( const BaseEvent& evt ) = 0;
};

// Lambda subscription: keeps the callback registered for as long as the object lives.
//   EventSubscription m_onLoad = EventSubscription::Create<SceneLoadedEvent>( [this]( const SceneLoadedEvent& e ) { ...; return false; } );
class EventSubscription final
    : public EventReceiver
{
public:
    EventSubscription() = default;
    EventSubscription( EventSubscription&& ) = delete;

    template<typename T>
    static std::unique_ptr<EventSubscription> Create( std::function<bool( const T& )> callback )
    {
        std::unique_ptr<EventSubscription> subscription( new EventSubscription() );
        subscription->m_callback = [callback = std::move( callback )]( const BaseEvent& event ) {
            return callback( static_cast<const T&>( event ) );
        };
        EventManager::GetInstance().RegisterReceiver( subscription.get(), { T::GetEventId() } );
        return subscription;
    }

    bool OnEvent( const BaseEvent& evt ) override
    {
        return m_callback ? m_callback( evt ) : false;
    }

private:
    std::function<bool( const BaseEvent& )> m_callback;
};
