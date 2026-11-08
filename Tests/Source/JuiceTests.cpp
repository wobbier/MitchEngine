#include <doctest/doctest.h>
#include <Widgets/Juice.h>
#include <imgui.h>
#include <cmath>
#include <vector>

using namespace MeWidgets;

namespace
{
    // Restores the level the editor (or another test) had.
    struct LevelScope
    {
        Juice::Level Previous = Juice::GetLevel();
        bool PreviousReduce = Juice::GetReduceMotion();
        LevelScope( Juice::Level InLevel, bool InReduce = false )
        {
            Juice::SetLevel( InLevel );
            Juice::SetReduceMotion( InReduce );
            Juice::Reset();
        }
        ~LevelScope()
        {
            Juice::SetLevel( Previous );
            Juice::SetReduceMotion( PreviousReduce );
            Juice::Reset();
        }
    };

    // The spring's path from 0 to 1 over a second at 60 fps.
    std::vector<float> SpringPath( ImGuiID InId )
    {
        std::vector<float> path;
        Juice::Spring( InId, 0.f );
        for( int i = 0; i < 60; ++i )
        {
            Juice::Update( 1.f / 60.f );
            path.push_back( Juice::Spring( InId, 1.f ) );
        }
        return path;
    }
}

TEST_CASE( "Juice: Off is static — springs land at once and nothing flashes, ticks, shakes or bursts" )
{
    LevelScope scope( Juice::Level::Off );
    for( float value : SpringPath( 1 ) )
    {
        CHECK( value == 1.f );
    }
    CHECK_FALSE( Juice::IsMoving( 1 ) );
    Juice::Flash( 2 );
    Juice::Shake( 2 );
    Juice::Tick( 2, ImVec2( 10.f, 10.f ) );
    Juice::Burst( ImVec2( 10.f, 10.f ), IM_COL32_WHITE, 20 );
    CHECK( Juice::FlashAmount( 2 ) == 0.f );
    CHECK( Juice::ShakeOffset( 2 ) == 0.f );
    CHECK( Juice::TickAmount( 2 ) == 0.f );
    CHECK( Juice::LiveParticles() == 0 );
}

TEST_CASE( "Juice: Full springs overshoot and settle; Subtle settles without overshoot" )
{
    {
        LevelScope scope( Juice::Level::Full );
        const std::vector<float> path = SpringPath( 7 );
        float peak = 0.f;
        for( float value : path ) peak = std::max( peak, value );
        CHECK( path.front() > 0.f );
        CHECK( path.front() < 1.f );
        CHECK( peak > 1.01f );
        CHECK( path.back() == doctest::Approx( 1.f ).epsilon( 0.001 ) );
    }
    {
        LevelScope scope( Juice::Level::Subtle );
        const std::vector<float> path = SpringPath( 7 );
        float peak = 0.f;
        for( float value : path ) peak = std::max( peak, value );
        CHECK( peak <= 1.0001f );
        CHECK( path.back() == doctest::Approx( 1.f ).epsilon( 0.001 ) );
        // Monotonic: no wobble.
        for( size_t i = 1; i < path.size(); ++i ) CHECK( path[i] >= path[i - 1] - 1e-6f );
    }
}

TEST_CASE( "Juice: reduce motion keeps colour feedback and drops motion" )
{
    LevelScope scope( Juice::Level::Full, true );
    for( float value : SpringPath( 3 ) ) CHECK( value == 1.f );
    Juice::Flash( 4 );
    Juice::Shake( 4 );
    Juice::Burst( ImVec2( 0.f, 0.f ), IM_COL32_WHITE, 10 );
    CHECK( Juice::FlashAmount( 4 ) > 0.9f );
    CHECK( Juice::ShakeOffset( 4 ) == 0.f );
    CHECK( Juice::LiveParticles() == 0 );
}

TEST_CASE( "Juice: flashes, ticks and shakes fade; bursts expire; Subtle is half as strong" )
{
    LevelScope scope( Juice::Level::Full );
    Juice::Flash( 5 );
    Juice::Tick( 5, ImVec2( 0.f, 0.f ) );
    Juice::Shake( 5 );
    CHECK( Juice::FlashAmount( 5 ) == doctest::Approx( 1.f ) );
    CHECK( Juice::TickAmount( 5 ) == doctest::Approx( 1.f ) );
    CHECK( Juice::LiveParticles() > 0 );
    Juice::Update( 0.1f );
    const float flash = Juice::FlashAmount( 5 );
    CHECK( flash > 0.f );
    CHECK( flash < 1.f );
    CHECK( std::abs( Juice::ShakeOffset( 5 ) ) > 0.f );
    for( int i = 0; i < 60; ++i ) Juice::Update( 1.f / 60.f );
    CHECK( Juice::FlashAmount( 5 ) == 0.f );
    CHECK( Juice::TickAmount( 5 ) == 0.f );
    CHECK( Juice::ShakeOffset( 5 ) == 0.f );
    CHECK( Juice::LiveParticles() == 0 );

    Juice::SetLevel( Juice::Level::Subtle );
    Juice::Flash( 6 );
    CHECK( Juice::FlashAmount( 6 ) == doctest::Approx( 0.5f ) );
}

TEST_CASE( "Juice: sound cues reach the player only when juice is on" )
{
    LevelScope scope( Juice::Level::Full );
    std::vector<Juice::Cue> heard;
    Juice::SetSoundPlayer( [&]( Juice::Cue InCue ) { heard.push_back( InCue ); } );
    Juice::Play( Juice::Cue::Commit );
    Juice::Tick( 9, ImVec2( 0.f, 0.f ) );
    Juice::SetLevel( Juice::Level::Off );
    Juice::Play( Juice::Cue::Reset );
    Juice::SetSoundPlayer( nullptr );
    REQUIRE( heard.size() == 2 );
    CHECK( heard[0] == Juice::Cue::Commit );
    CHECK( heard[1] == Juice::Cue::Tick );
}
