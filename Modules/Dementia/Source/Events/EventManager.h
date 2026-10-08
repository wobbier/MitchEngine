#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Singleton.h"
#include "ClassTypeId.h"
#include "Pointers.h"

class EventReceiver;

class BaseEvent
{
public:
    virtual ~BaseEvent() = default;

    TypeId GetEventId() const
    {
        return id;
    }
protected:
    BaseEvent( TypeId type )
        : id( type )
    {

    }
private:
    TypeId id;
};

// Event dispatch.
//  - Fire(): immediate, on the calling thread (main thread only).
//  - Queue(): from any thread; delivered on the main thread by FirePendingEvents() at frame start.
// Receivers may register/deregister (or be destroyed) during dispatch; a receiver returning true
// from OnEvent consumes the event and stops propagation.
class EventManager
{
private:
    EventManager();
public:
    ~EventManager();

    void RegisterReceiver( EventReceiver* receiver, std::vector<TypeId> events );
    void DeRegisterReciever( EventReceiver* receiver );

    void FireEvent( TypeId eventId, const BaseEvent& event );
    void QueueEvent( std::unique_ptr<BaseEvent> event );
    void FirePendingEvents();

    // False once the manager has been destroyed (static teardown): receivers skip deregistration.
    static bool IsAlive();

protected:
    void Compact( std::vector<EventReceiver*>& receivers );

    std::unordered_map<TypeId, std::vector<EventReceiver*>> m_eventReceivers;
    int m_dispatchDepth = 0;
    bool m_needsCompaction = false;

    std::mutex m_queueMutex;
    std::vector<std::unique_ptr<BaseEvent>> m_queuedEvents;

    std::thread::id m_mainThread;

    ME_SINGLETON_DEFINITION( EventManager )
};
