#include <doctest/doctest.h>
#include "Components/Effects/ParticleSystem.h"
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    // A quiet system: no burst, no rate, no forces, long-lived particles.
    void MakeQuiet( ParticleSystem& InSystem )
    {
        InSystem.BurstCount = 0;
        InSystem.EmissionRate = 0.f;
        InSystem.Lifetime = Vector2( 10.f, 10.f );
        InSystem.Gravity = 0.f;
        InSystem.Drag = 0.f;
        InSystem.NoiseStrength = 0.f;
        InSystem.Restart();
    }

    void Run( ParticleSystem& InSystem, float InSeconds, float InStep = 0.02f, const glm::mat4& InWorld = glm::mat4( 1.f ) )
    {
        for( float t = 0.f; t < InSeconds - 1e-4f; t += InStep )
        {
            InSystem.Simulate( InStep, InWorld );
        }
    }
}

TEST_CASE( "Particles: bursts, rates and the particle cap" )
{
    ParticleSystem system;
    MakeQuiet( system );
    system.BurstCount = 12;
    system.Simulate( 0.01f, glm::mat4( 1.f ) );
    CHECK( system.GetParticleCount() == 12 );

    MakeQuiet( system );
    system.EmissionRate = 100.f;
    Run( system, 1.f );
    CHECK( system.GetParticleCount() >= 98 );
    CHECK( system.GetParticleCount() <= 101 );

    MakeQuiet( system );
    system.EmissionRate = 1000.f;
    system.MaxParticles = 50;
    Run( system, 1.f );
    CHECK( system.GetParticleCount() == 50 );
}

TEST_CASE( "Particles: lifetimes expire and non-looping systems stop emitting" )
{
    ParticleSystem system;
    MakeQuiet( system );
    system.BurstCount = 5;
    system.Lifetime = Vector2( 0.5f, 0.5f );
    Run( system, 0.4f );
    CHECK( system.GetParticleCount() == 5 );
    Run( system, 0.2f );
    CHECK( system.GetParticleCount() == 0 );

    MakeQuiet( system );
    system.Looping = false;
    system.Duration = 0.5f;
    system.EmissionRate = 100.f;
    Run( system, 1.5f );
    CHECK( system.GetParticleCount() >= 48 );
    CHECK( system.GetParticleCount() <= 52 );
    CHECK_FALSE( system.IsEmitting() );
}

TEST_CASE( "Particles: gravity, world space and local space" )
{
    ParticleSystem system;
    MakeQuiet( system );
    system.Shape = ParticleShape::Point;
    system.Speed = Vector2( 0.f, 0.f );
    system.Size = Vector2( 1.f, 1.f );
    system.EndSize = 1.f;
    system.Gravity = 1.f;
    system.BurstCount = 1;
    Run( system, 1.f, 0.001f );

    Moonlight::ParticleBatch batch;
    system.FillBatch( batch, glm::mat4( 1.f ) );
    REQUIRE( batch.Instances.size() == 1 );
    CHECK( batch.Instances[0].PositionSize.y == doctest::Approx( -4.905f ).epsilon( 0.01 ) );
    CHECK( batch.Bounds.IsValid() );

    // World space: particles keep their birth position when the emitter moves.
    const glm::mat4 moved = glm::translate( glm::mat4( 1.f ), glm::vec3( 10.f, 0.f, 0.f ) );
    MakeQuiet( system );
    system.Shape = ParticleShape::Point;
    system.Speed = Vector2( 0.f, 0.f );
    system.BurstCount = 1;
    system.Simulate( 0.01f, glm::mat4( 1.f ) );
    system.FillBatch( batch, moved );
    CHECK( batch.Instances[0].PositionSize.x == doctest::Approx( 0.f ) );

    // Local space: they ride along with it.
    MakeQuiet( system );
    system.SimulationSpace = ParticleSpace::Local;
    system.Shape = ParticleShape::Point;
    system.Speed = Vector2( 0.f, 0.f );
    system.BurstCount = 1;
    system.Simulate( 0.01f, glm::mat4( 1.f ) );
    system.FillBatch( batch, moved );
    CHECK( batch.Instances[0].PositionSize.x == doctest::Approx( 10.f ) );
}

TEST_CASE( "Particles: colour fades over life and the flipbook advances" )
{
    ParticleSystem system;
    MakeQuiet( system );
    system.Shape = ParticleShape::Point;
    system.Speed = Vector2( 0.f, 0.f );
    system.Lifetime = Vector2( 1.f, 1.f );
    system.StartAlpha = 1.f;
    system.EndAlpha = 0.f;
    system.FadeIn = 0.f;
    system.FlipbookColumns = 4;
    system.FlipbookRows = 2;
    system.BurstCount = 1;
    Run( system, 0.52f, 0.01f );

    Moonlight::ParticleBatch batch;
    system.FillBatch( batch, glm::mat4( 1.f ) );
    REQUIRE( batch.Instances.size() == 1 );
    CHECK( batch.Instances[0].Color.w == doctest::Approx( 0.5f ).epsilon( 0.05 ) );
    CHECK( batch.Instances[0].Params.y == doctest::Approx( 4.f ) );   // just past half of 8 frames
    CHECK( batch.FlipbookColumns == 4 );
}
