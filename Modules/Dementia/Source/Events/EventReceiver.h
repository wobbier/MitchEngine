#pragma once
#include "Event.h"

#include <type_traits>

// Inherit and Listen<T>() for each event type you handle. Deregisters itself on destruction.
//   Listen<SceneLoadedEvent>( [this]( const SceneLoadedEvent& evt ) { ... } );
// A handler returning bool consumes the event (stops propagation) by returning true; a void
// handler never consumes.
class EventReceiver
{
public:
    EventReceiver() = default;
    EventReceiver( const EventReceiver& ) = delete;
    EventReceiver& operator=( const EventReceiver& ) = delete;
    virtual ~EventReceiver();

    // Called by EventManager: runs this receiver's handlers for the event's type, in Listen order.
    // True when one of them consumed it.
    bool OnEvent( const BaseEvent& evt );

protected:
    template<class T, class F>
    void Listen( F&& InHandler );

private:
    struct Handler
    {
        TypeId Type = 0;
        std::function<bool( const BaseEvent& )> Callback;
    };
    std::vector<Handler> m_handlers;
    // Cleared on destruction, so dispatch can tell when a handler destroyed its own receiver.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>( true );
};


template<class T, class F>
void EventReceiver::Listen( F&& InHandler )
{
    static_assert( std::is_invocable_v<F&, const T&>, "Listen<T>: the handler must take const T&" );
    using Result = std::invoke_result_t<F&, const T&>;
    static_assert( std::is_void_v<Result> || std::is_same_v<Result, bool>, "Listen<T>: the handler returns bool (true consumes) or void" );

    Handler handler;
    handler.Type = T::GetEventId();
    if constexpr( std::is_void_v<Result> )
    {
        handler.Callback = [callback = std::forward<F>( InHandler )]( const BaseEvent& evt ) mutable {
            callback( static_cast<const T&>( evt ) );
            return false;
        };
    }
    else
    {
        handler.Callback = [callback = std::forward<F>( InHandler )]( const BaseEvent& evt ) mutable {
            return callback( static_cast<const T&>( evt ) );
        };
    }
    m_handlers.push_back( std::move( handler ) );
    EventManager::GetInstance().RegisterReceiver( this, { T::GetEventId() } );
}


// Lambda subscription: keeps the callback registered for as long as the object lives.
//   std::unique_ptr<EventSubscription> m_onLoad = EventSubscription::Create<SceneLoadedEvent>( [this]( const SceneLoadedEvent& evt ) { ... } );
class EventSubscription final
    : public EventReceiver
{
public:
    template<typename T, typename F>
    static std::unique_ptr<EventSubscription> Create( F&& InCallback )
    {
        std::unique_ptr<EventSubscription> subscription( new EventSubscription() );
        subscription->Listen<T>( std::forward<F>( InCallback ) );
        return subscription;
    }

private:
    EventSubscription() = default;
};
