#include "Events/EventReceiver.h"


EventReceiver::~EventReceiver()
{
    *m_alive = false;
    if( EventManager::IsAlive() )
    {
        EventManager::GetInstance().DeRegisterReciever( this );
    }
}


bool EventReceiver::OnEvent( const BaseEvent& evt )
{
    const TypeId type = evt.GetEventId();
    // A handler may destroy this receiver, or Listen to more types (those wait for the next event).
    const std::shared_ptr<bool> alive = m_alive;
    const size_t count = m_handlers.size();
    for( size_t i = 0; i < count; ++i )
    {
        if( m_handlers[i].Type != type )
        {
            continue;
        }
        // Called through a copy, so the callback outlives a receiver it destroys.
        const std::function<bool( const BaseEvent& )> callback = m_handlers[i].Callback;
        if( callback( evt ) )
        {
            return true;
        }
        if( !*alive )
        {
            return false;
        }
    }
    return false;
}
