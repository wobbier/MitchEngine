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

    class PongEvent
        : public Event<PongEvent>
    {
    public:
        float Value = 0.f;
    };

    struct Counter
        : public EventReceiver
    {
        explicit Counter( bool consume = false ) : Consume( consume )
        {
            Listen<PingEvent>( [this]( const PingEvent& evt ) {
                ++Received;
                Last = evt.Value;
                if( OnReceive )
                {
                    OnReceive();
                }
                return Consume;
            } );
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


TEST_CASE( "Events: typed handlers, void handlers and several types on one receiver" )
{
    struct Both
        : public EventReceiver
    {
        Both()
        {
            Listen<PingEvent>( [this]( const PingEvent& evt ) { Pings += evt.Value; } );  // void: never consumes
            Listen<PongEvent>( [this]( const PongEvent& evt ) { Pongs += evt.Value; return false; } );
        }
        int Pings = 0;
        float Pongs = 0.f;
    };
    Both both;
    Counter after;

    PingEvent ping;
    ping.Value = 2;
    ping.Fire();
    PongEvent pong;
    pong.Value = 0.5f;
    pong.Fire();
    pong.Fire();

    CHECK( both.Pings == 2 );
    CHECK( both.Pongs == doctest::Approx( 1.f ) );
    CHECK( after.Received == 1 );   // the void handler let the ping through
}


TEST_CASE( "Events: handlers for one type run in Listen order until one consumes" )
{
    struct Twice
        : public EventReceiver
    {
        Twice()
        {
            Listen<PingEvent>( [this]( const PingEvent& ) { Order.push_back( 1 ); return ConsumeFirst; } );
            Listen<PingEvent>( [this]( const PingEvent& ) { Order.push_back( 2 ); } );
        }
        std::vector<int> Order;
        bool ConsumeFirst = false;
    };
    Twice twice;
    Counter after;
    PingEvent().Fire();
    CHECK( twice.Order == std::vector<int>{ 1, 2 } );
    CHECK( after.Received == 1 );

    twice.ConsumeFirst = true;
    twice.Order.clear();
    PingEvent().Fire();
    CHECK( twice.Order == std::vector<int>{ 1 } );
    CHECK( after.Received == 1 );
}


TEST_CASE( "Events: a handler may destroy its own receiver or listen for more" )
{
    struct Grower
        : public EventReceiver
    {
        Grower()
        {
            Listen<PingEvent>( [this]( const PingEvent& ) {
                ++First;
                if( First == 1 )
                {
                    Listen<PingEvent>( [this]( const PingEvent& ) { ++Second; } );
                }
            } );
        }
        int First = 0;
        int Second = 0;
    };
    Grower grower;
    PingEvent().Fire();
    CHECK( grower.First == 1 );
    CHECK( grower.Second == 0 );   // added mid-dispatch: waits for the next event
    PingEvent().Fire();
    CHECK( grower.First == 2 );
    CHECK( grower.Second == 1 );

    struct SelfDestruct
        : public EventReceiver
    {
        SelfDestruct( std::unique_ptr<SelfDestruct>& InOwner, int& InLaterCalls )
        {
            Listen<PingEvent>( [&InOwner]( const PingEvent& ) { InOwner.reset(); } );
            Listen<PingEvent>( [&InLaterCalls]( const PingEvent& ) { ++InLaterCalls; } );  // must not run: its receiver is gone
        }
    };
    int laterCalls = 0;
    std::unique_ptr<SelfDestruct> self;
    self = std::make_unique<SelfDestruct>( self, laterCalls );
    Counter after;
    PingEvent().Fire();
    CHECK_FALSE( self );
    CHECK( laterCalls == 0 );
    CHECK( after.Received == 1 );
}
