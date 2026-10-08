#include "Events/EventReceiver.h"


EventReceiver::~EventReceiver()
{
    if( EventManager::IsAlive() )
    {
        EventManager::GetInstance().DeRegisterReciever( this );
    }
}
