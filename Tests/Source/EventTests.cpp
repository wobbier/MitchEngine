#include <doctest/doctest.h>

#include "Events/Event.h"
#include "Events/EventReceiver.h"
#include <memory>
#include <thread>
#include <vector>

namespace EventTest
{
    class PingEvent
        : public Event<PingEvent>
    {
    public:
        int Value = 0;
    };

    struct Counter
        : public EventReceiver
    {
        explicit Counter( bool consume = false ) : Consume( consume )
        {
            EventManager::GetInstance().RegisterReceiver( this, { PingEvent::GetEventId() } );
        }

        bool OnEvent( const BaseEvent& evt ) override
        {
            ++Received;
            Last = static_cast<const PingEvent&>( evt ).Value;
            if( OnReceive )
            {
                OnReceive();
            }
            return Consume;
        }

        int Received = 0;
        int Last = 0;
        bool Consume = false;
        std::function<void()> OnReceive;
    };
}

using namespace EventTest;

TEST_CASE( "Events: fire reaches receivers and receivers deregister on destruction" )
{
    Counter a;
    {
        Counter b;
        PingEvent evt;
        evt.Value = 3;
        evt.Fire();
        CHECK( a.Received == 1 );
        CHECK( b.Received == 1 );
        CHECK( b.Last == 3 );
    }
    // b is gone; firing must not touch it.
    PingEvent evt;
    evt.Fire();
    CHECK( a.Received == 2 );
}


TEST_CASE( "Events: consuming stops propagation" )
{
    Counter first( true );
    Counter second;
    PingEvent().Fire();
    CHECK( first.Received == 1 );
    CHECK( second.Received == 0 );
}


TEST_CASE( "Events: receivers can be destroyed or added during dispatch" )
{
    std::unique_ptr<Counter> victim;
    std::unique_ptr<Counter> late;
    Counter killer;
    victim = std::make_unique<Counter>();
    killer.OnReceive = [&]() {
        victim.reset();                         // destroy a receiver mid-dispatch
        if( !late )
        {
            late = std::make_unique<Counter>(); // register mid-dispatch
        }
    };
    PingEvent().Fire();
    CHECK( killer.Received == 1 );
    CHECK_FALSE( victim );
    REQUIRE( late );

    PingEvent().Fire();
    CHECK( killer.Received == 2 );
    CHECK( late->Received >= 1 );
}


TEST_CASE( "Events: Queue is thread safe and delivers on FirePendingEvents" )
{
    Counter counter;
    std::vector<std::thread> threads;
    for( int t = 0; t < 4; ++t )
    {
        threads.emplace_back( []() {
            for( int i = 0; i < 100; ++i )
            {
                PingEvent evt;
                evt.Value = i;
                evt.Queue();
            }
        } );
    }
    for( std::thread& thread : threads )
    {
        thread.join();
    }
    CHECK( counter.Received == 0 );
    EventManager::GetInstance().FirePendingEvents();
    CHECK( counter.Received == 400 );
}


TEST_CASE( "Events: lambda subscriptions" )
{
    int sum = 0;
    {
        auto subscription = EventSubscription::Create<PingEvent>( [&sum]( const PingEvent& evt ) {
            sum += evt.Value;
            return false;
        } );
        PingEvent evt;
        evt.Value = 5;
        evt.Fire();
        evt.Fire();
    }
    PingEvent evt;
    evt.Value = 100;
    evt.Fire();
    CHECK( sum == 10 );
}
