#include <doctest/doctest.h>
#include "Components/Effects/ParticleSystem.h"
#include "Graphics/ShaderCompiler.h"
#include "Particles/GpuParticles.h"
#include "Path.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <string>
#include <utility>
#include <vector>

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

namespace
{
    // GPU mode without a renderer: StepGpu only runs the emitter and fills the step, which is what
    // these tests check (nothing is dispatched).
    void MakeGpu( ParticleSystem& InSystem )
    {
        InSystem.Simulation = ParticleSimulation::GPU;
        InSystem.Blend = Moonlight::ParticleBlend::Additive;
        InSystem.UpdateSimulationMode( true );
        REQUIRE( InSystem.UsesGpuSimulation() );
    }

    // Steps a GPU-mode system; returns the particles it spawned and checks every spawn range
    // continues the ring where the last one ended.
    uint64_t RunGpu( ParticleSystem& InSystem, Moonlight::ParticleBatch& InBatch, float InSeconds, float InStep = 0.02f, const glm::mat4& InWorld = glm::mat4( 1.f ) )
    {
        uint64_t spawned = 0;
        for( float t = 0.f; t < InSeconds - 1e-4f; t += InStep )
        {
            InSystem.StepGpu( InStep, InWorld, InBatch );
            CHECK( InBatch.GpuStepPending );
            spawned += InBatch.GpuStep.SpawnCount;
        }
        return spawned;
    }
}

TEST_CASE( "Particles: GPU mode runs the CPU's emitter schedule" )
{
    Moonlight::ParticleBatch batch;

    // A start burst: one range from slot 0, and the first step resets the buffers.
    ParticleSystem system;
    MakeQuiet( system );
    MakeGpu( system );
    system.BurstCount = 12;
    system.StepGpu( 0.01f, glm::mat4( 1.f ), batch );
    CHECK( batch.GpuStep.Reset );
    CHECK( batch.GpuStep.SpawnStart == 0 );
    CHECK( batch.GpuStep.SpawnCount == 12 );
    CHECK( batch.GpuInstanceCount == 12 );
    CHECK( batch.GpuLiveCount == 12 );
    CHECK( batch.Instances.empty() );
    CHECK( system.GetParticleCount() == 12 );
    system.StepGpu( 0.01f, glm::mat4( 1.f ), batch );
    CHECK_FALSE( batch.GpuStep.Reset );
    CHECK( batch.GpuStep.SpawnCount == 0 );
    CHECK( system.GetParticleCount() == 12 );

    // A rate spawns what the CPU path spawns.
    ParticleSystem cpu;
    MakeQuiet( cpu );
    cpu.EmissionRate = 100.f;
    Run( cpu, 1.f );
    MakeQuiet( system );
    system.EmissionRate = 100.f;
    const uint64_t spawned = RunGpu( system, batch, 1.f );
    CHECK( spawned == cpu.GetParticleCount() );
    CHECK( system.GetParticleCount() == cpu.GetParticleCount() );

    // Looping re-arms the start burst like the CPU path.
    MakeQuiet( cpu );
    cpu.Looping = true;
    cpu.Duration = 0.5f;
    cpu.BurstCount = 10;
    Run( cpu, 1.3f );
    MakeQuiet( system );
    system.Looping = true;
    system.Duration = 0.5f;
    system.BurstCount = 10;
    CHECK( RunGpu( system, batch, 1.3f ) == cpu.GetParticleCount() );
    CHECK( cpu.GetParticleCount() == 30 );

    // Stop ends emission; a script burst still spawns.
    MakeQuiet( system );
    system.EmissionRate = 100.f;
    RunGpu( system, batch, 0.2f );
    system.Stop();
    CHECK( RunGpu( system, batch, 0.2f ) == 0 );
    system.Burst( 7 );
    system.StepGpu( 0.02f, glm::mat4( 1.f ), batch );
    CHECK( batch.GpuStep.SpawnCount == 7 );
}

TEST_CASE( "Particles: GPU mode's ring wraps at the cap and the estimate follows lifetimes" )
{
    Moonlight::ParticleBatch batch;
    ParticleSystem system;
    MakeQuiet( system );
    system.MaxParticles = 50;
    MakeGpu( system );
    system.EmissionRate = 1000.f;
    uint64_t spawned = 0;
    for( int i = 0; i < 50; ++i )
    {
        system.StepGpu( 0.02f, glm::mat4( 1.f ), batch );
        spawned += batch.GpuStep.SpawnCount;
        // Each range starts where the previous one ended, modulo the capacity.
        CHECK( ( batch.GpuStep.SpawnStart + batch.GpuStep.SpawnCount ) % 50 == spawned % 50 );
        CHECK( batch.GpuStep.SpawnStart < 50 );
    }
    CHECK( spawned > 50 );
    CHECK( system.GetParticleCount() == 50 );
    CHECK( batch.GpuInstanceCount == 50 );

    // A burst bigger than the pool fills it once, ending where the ring cursor lands.
    system.EmissionRate = 0.f;
    system.Burst( 120 );
    system.StepGpu( 0.02f, glm::mat4( 1.f ), batch );
    spawned += 120;
    CHECK( batch.GpuStep.SpawnCount == 50 );
    CHECK( ( batch.GpuStep.SpawnStart + batch.GpuStep.SpawnCount ) % 50 == spawned % 50 );

    // The estimate drops to zero once the longest lifetime has passed, and the ring starts over.
    MakeQuiet( system );
    system.BurstCount = 5;
    system.Lifetime = Vector2( 0.25f, 0.5f );
    RunGpu( system, batch, 0.4f );
    CHECK( system.GetParticleCount() == 5 );
    CHECK( batch.GpuInstanceCount == 5 );
    RunGpu( system, batch, 0.2f );
    CHECK( system.GetParticleCount() == 0 );
    CHECK( batch.GpuInstanceCount == 0 );
    CHECK_FALSE( batch.Bounds.IsValid() );
    system.Burst( 3 );
    system.StepGpu( 0.02f, glm::mat4( 1.f ), batch );
    CHECK( batch.GpuStep.SpawnStart == 0 );
    CHECK( batch.GpuInstanceCount == 3 );
}

TEST_CASE( "Particles: GPU simulation falls back to the CPU and restarts on a switch" )
{
    Moonlight::ParticleBatch batch;
    ParticleSystem system;
    MakeQuiet( system );
    system.Simulation = ParticleSimulation::GPU;

    // Alpha runs on the GPU too (sorted there, ME-381); without compute it doesn't.
    system.Blend = Moonlight::ParticleBlend::Alpha;
    system.UpdateSimulationMode( true );
    CHECK( system.UsesGpuSimulation() );
    system.UpdateSimulationMode( false );
    CHECK_FALSE( system.UsesGpuSimulation() );
    // No compute: CPU.
    system.Blend = Moonlight::ParticleBlend::Additive;
    system.UpdateSimulationMode( false );
    CHECK_FALSE( system.UsesGpuSimulation() );
    system.BurstCount = 4;
    system.Restart();
    system.Simulate( 0.01f, glm::mat4( 1.f ) );
    CHECK( system.GetParticleCount() == 4 );

    // Switching to the GPU restarts (the CPU particles go, the burst comes again from a reset).
    system.UpdateSimulationMode( true );
    REQUIRE( system.UsesGpuSimulation() );
    CHECK( system.GetParticleCount() == 0 );
    system.StepGpu( 0.01f, glm::mat4( 1.f ), batch );
    CHECK( batch.GpuStep.Reset );
    CHECK( batch.GpuStep.SpawnCount == 4 );

    // A new pool size restarts too; the same size doesn't.
    system.UpdateSimulationMode( true );
    system.StepGpu( 0.01f, glm::mat4( 1.f ), batch );
    CHECK_FALSE( batch.GpuStep.Reset );
    system.MaxParticles = 77;
    system.UpdateSimulationMode( true );
    system.StepGpu( 0.01f, glm::mat4( 1.f ), batch );
    CHECK( batch.GpuStep.Reset );
    CHECK( batch.GpuStep.SpawnCount == 4 );

    // And back to the CPU.
    system.Simulation = ParticleSimulation::CPU;
    system.UpdateSimulationMode( true );
    CHECK_FALSE( system.UsesGpuSimulation() );
    CHECK( system.GetParticleCount() == 0 );
}

TEST_CASE( "Particles: GPU bounds hold every particle of the same CPU simulation" )
{
    struct Setup
    {
        const char* Name;
        ParticleShape Shape;
        ParticleSpace Space;
        Moonlight::ParticleAlignment Alignment;
        bool Shell;
        bool Moving;
    };
    const Setup setups[] = {
        { "cone fire", ParticleShape::Cone, ParticleSpace::World, Moonlight::ParticleAlignment::Billboard, false, false },
        { "sphere shell", ParticleShape::Sphere, ParticleSpace::World, Moonlight::ParticleAlignment::Billboard, true, false },
        { "hemisphere sparks, moving", ParticleShape::Hemisphere, ParticleSpace::World, Moonlight::ParticleAlignment::Stretched, false, true },
        { "point, moving", ParticleShape::Point, ParticleSpace::World, Moonlight::ParticleAlignment::Billboard, false, true },
        { "box, local", ParticleShape::Box, ParticleSpace::Local, Moonlight::ParticleAlignment::Billboard, false, true },
        { "cone sparks, local", ParticleShape::Cone, ParticleSpace::Local, Moonlight::ParticleAlignment::Stretched, true, true },
    };
    for( const Setup& setup : setups )
    {
        const std::string name = setup.Name;
        CAPTURE( name );
        auto configure = [&setup]( ParticleSystem& InSystem ) {
            InSystem.Shape = setup.Shape;
            InSystem.SimulationSpace = setup.Space;
            InSystem.Alignment = setup.Alignment;
            InSystem.EmitFromShell = setup.Shell;
            InSystem.ShapeRadius = 0.6f;
            InSystem.ConeAngle = 35.f;
            InSystem.BoxSize = Vector3( 1.f, 0.5f, 2.f );
            InSystem.EmissionRate = 150.f;
            InSystem.BurstCount = 40;
            InSystem.Lifetime = Vector2( 0.5f, 1.5f );
            InSystem.Speed = Vector2( 2.f, 6.f );
            InSystem.Size = Vector2( 0.1f, 0.3f );
            InSystem.EndSize = 1.8f;
            InSystem.Gravity = 0.8f;
            InSystem.Drag = 0.7f;
            InSystem.NoiseStrength = 2.f;
            InSystem.NoiseFrequency = 1.3f;
            InSystem.StretchFactor = 0.08f;
            InSystem.MaxParticles = 5000;
            InSystem.Restart();
        };
        ParticleSystem cpu;
        configure( cpu );
        ParticleSystem gpu;
        configure( gpu );
        MakeGpu( gpu );

        Moonlight::ParticleBatch cpuBatch;
        Moonlight::ParticleBatch gpuBatch;
        int outside = 0;
        for( int frame = 0; frame < 240; ++frame )
        {
            const float t = frame / 60.f;
            glm::mat4 world = glm::translate( glm::mat4( 1.f ), glm::vec3( 1.f, 2.f, -3.f ) );
            if( setup.Moving )
            {
                world = glm::translate( world, glm::vec3( 4.f * std::sin( t * 2.f ), 0.5f * t, 3.f * t ) );
                world = glm::rotate( world, t * 1.5f, glm::normalize( glm::vec3( 0.3f, 1.f, 0.2f ) ) );
            }
            world = glm::rotate( world, 0.7f, glm::vec3( 1.f, 0.f, 0.f ) );
            world = glm::scale( world, glm::vec3( 1.5f, 0.75f, 2.f ) );
            // Uneven steps, including one past the 0.1 s clamp.
            const float dt = frame % 37 == 5 ? 0.15f : ( frame % 3 == 0 ? 1.f / 30.f : 1.f / 90.f );
            cpu.Simulate( dt, world );
            cpu.FillBatch( cpuBatch, world );
            gpu.StepGpu( dt, world, gpuBatch );
            CHECK( gpu.GetParticleCount() >= cpu.GetParticleCount() );
            if( cpuBatch.Instances.empty() )
            {
                continue;
            }
            REQUIRE( gpuBatch.Bounds.IsValid() );
            const AABB& bounds = gpuBatch.Bounds;
            for( const Moonlight::ParticleInstance& instance : cpuBatch.Instances )
            {
                const glm::vec3 center( instance.PositionSize );
                const float size = instance.PositionSize.w;
                glm::vec3 lo = center - glm::vec3( size );
                glm::vec3 hi = center + glm::vec3( size );
                if( setup.Alignment == Moonlight::ParticleAlignment::Stretched )
                {
                    // The stretched quad's tail.
                    const glm::vec3 velocity( instance.Velocity );
                    const float speed = glm::length( velocity );
                    if( speed > 1e-3f )
                    {
                        const glm::vec3 tail = center - velocity / speed * ( size + speed * cpu.StretchFactor );
                        lo = glm::min( lo, tail - glm::vec3( size * 0.5f ) );
                        hi = glm::max( hi, tail + glm::vec3( size * 0.5f ) );
                    }
                }
                if( !bounds.Contains( Vector3( lo.x, lo.y, lo.z ) ) || !bounds.Contains( Vector3( hi.x, hi.y, hi.z ) ) )
                {
                    ++outside;
                }
            }
        }
        CHECK( outside == 0 );
    }
}

TEST_CASE( "Particles: the GPU simulation and alpha sort shaders compile for every portable profile" )
{
    const std::filesystem::path directory = std::filesystem::path( Path( ".tmp/ParticleSimShaderTests" ).FullPath );
    std::filesystem::remove_all( directory );
    std::filesystem::create_directories( directory );
    std::string failures;
    for( const char* name : { "ParticleSim", "ParticleSortLocal", "ParticleSortGlobal", "ParticleSortGather" } )
    {
        const std::string source = Path( std::string( "Assets/Shaders/Particles/" ) + name + ".comp" ).FullPath;
        REQUIRE( std::filesystem::exists( source ) );
        for( const Moonlight::ShaderCompiler::Profile& profile : Moonlight::ShaderCompiler::GetPortableProfiles() )
        {
            Moonlight::ShaderCompiler::Request request;
            request.SourcePath = source;
            request.OutputPath = ( directory / ( std::string( name ) + ".comp." + profile.Name + ".bin" ) ).string();
            request.ShaderStage = Moonlight::ShaderCompiler::Stage::Compute;
            request.TargetProfile = profile;
            const Moonlight::ShaderCompiler::Result result = Moonlight::ShaderCompiler::Compile( request );
            if( !result.Success || !std::filesystem::exists( request.OutputPath ) )
            {
                failures += std::string( name ) + " [" + profile.Name + "]\n" + result.Log + "\n";
            }
        }
    }
    CHECK_MESSAGE( failures.empty(), failures );
    std::filesystem::remove_all( directory );
}

namespace
{
    // The alpha sort's network on the CPU, step for step as ParticleSort.sh / ParticleSortLocal.comp /
    // ParticleSortGlobal.comp run it: entries are (key, slot), a sorts after b by key, then slot.
    using SortEntry = std::pair<float, float>;

    bool SortAfter( const SortEntry& a, const SortEntry& b )
    {
        return a.first > b.first || ( a.first == b.first && a.second > b.second );
    }

    void SortStep( std::vector<SortEntry>& InEntries, uint32_t InFirst, uint32_t InCount, uint32_t InSize, bool InFlip )
    {
        // One "thread" per compare: InCount / 2 of them, over the entries [InFirst, InFirst + InCount).
        for( uint32_t thread = 0; thread < InCount / 2; ++thread )
        {
            const uint32_t halfSize = InSize / 2;
            const uint32_t base = ( thread / halfSize ) * InSize;
            const uint32_t offset = thread % halfSize;
            const uint32_t i = InFirst + base + offset;
            const uint32_t j = InFirst + ( InFlip ? base + InSize - 1 - offset : base + offset + halfSize );
            if( SortAfter( InEntries[i], InEntries[j] ) )
            {
                std::swap( InEntries[i], InEntries[j] );
            }
        }
    }

    void RunGpuSortOnCpu( std::vector<SortEntry>& InEntries )
    {
        const uint32_t entries = static_cast<uint32_t>( InEntries.size() );
        const uint32_t block = Moonlight::kGpuParticleSortBlock;
        std::vector<Moonlight::GpuSortPass> passes;
        Moonlight::BuildGpuSortPasses( entries, passes );
        for( const Moonlight::GpuSortPass& pass : passes )
        {
            switch( pass.Type )
            {
            case Moonlight::GpuSortPass::Kind::LocalSort:
                for( uint32_t first = 0; first < entries; first += block )
                {
                    for( uint32_t h = 2; h <= block; h *= 2 )
                    {
                        SortStep( InEntries, first, block, h, true );
                        for( uint32_t d = h / 2; d >= 2; d /= 2 )
                        {
                            SortStep( InEntries, first, block, d, false );
                        }
                    }
                }
                break;
            case Moonlight::GpuSortPass::Kind::LocalMerge:
                for( uint32_t first = 0; first < entries; first += block )
                {
                    for( uint32_t d = block; d >= 2; d /= 2 )
                    {
                        SortStep( InEntries, first, block, d, false );
                    }
                }
                break;
            case Moonlight::GpuSortPass::Kind::Flip:
            case Moonlight::GpuSortPass::Kind::Disperse:
                SortStep( InEntries, 0, entries, pass.Size, pass.Type == Moonlight::GpuSortPass::Kind::Flip );
                break;
            }
        }
    }
}

TEST_CASE( "Particles: the GPU alpha sort's schedule sorts like the CPU (back to front, dead and padding last)" )
{
    CHECK( Moonlight::GpuSortEntryCount( 1 ) == Moonlight::kGpuParticleSortBlock );
    CHECK( Moonlight::GpuSortEntryCount( 512 ) == 512 );
    CHECK( Moonlight::GpuSortEntryCount( 513 ) == 1024 );
    CHECK( Moonlight::GpuSortEntryCount( 100000 ) == 131072 );
    std::vector<Moonlight::GpuSortPass> passes;
    Moonlight::BuildGpuSortPasses( 131072, passes );
    CHECK( passes.size() == 45 );

    std::mt19937 rng( 381 );
    std::uniform_real_distribution<float> depth( -20.f, 20.f );
    for( uint32_t count : { 1u, 7u, 512u, 513u, 3000u, 20000u } )
    {
        const uint32_t entries = Moonlight::GpuSortEntryCount( count );
        const uint32_t capacity = count + 17;
        std::vector<SortEntry> gpu( entries );
        for( uint32_t i = 0; i < entries; ++i )
        {
            if( i >= count )
            {
                gpu[i] = { 3.0e38f, static_cast<float>( capacity + i ) };   // padding
            }
            else if( rng() % 5 == 0 )
            {
                gpu[i] = { 3.0e38f, static_cast<float>( i ) };              // a dead slot
            }
            else
            {
                // Coarse depths so ties happen; the key is minus the view-space depth.
                gpu[i] = { -std::round( depth( rng ) * 4.f ) / 4.f, static_cast<float>( i ) };
            }
        }
        std::vector<SortEntry> reference = gpu;
        RunGpuSortOnCpu( gpu );
        // The CPU path's order: view-space depth descending (back to front); ties by slot here.
        std::sort( reference.begin(), reference.end(), []( const SortEntry& a, const SortEntry& b ) { return SortAfter( b, a ); } );
        CHECK_MESSAGE( gpu == reference, "count " << count );
    }
}

TEST_CASE( "Particles: a render texture camera reuses the main camera's order only when it still fits" )
{
    Moonlight::GpuSortInputs inputs;
    inputs.ReuseAllowed = true;
    inputs.HasSource = true;
    inputs.Capacity = 1000;
    inputs.Frame = 10;
    inputs.SourceFrame = 9;
    inputs.SourceCount = 300;
    inputs.SourceRingEnd = 400;
    inputs.SpawnStart = 400;
    inputs.SpawnCount = 20;
    inputs.RingEnd = 420;
    Moonlight::GpuSortPlan plan = Moonlight::PlanGpuSort( inputs );
    CHECK( plan.Reuse );
    CHECK( plan.AppendCount == 20 );

    // The main camera already sorted this frame: nothing to append.
    Moonlight::GpuSortInputs same = inputs;
    same.SourceFrame = 10;
    same.SourceRingEnd = 420;
    plan = Moonlight::PlanGpuSort( same );
    CHECK( plan.Reuse );
    CHECK( plan.AppendCount == 0 );

    // Births wrapping the ring still follow it.
    Moonlight::GpuSortInputs wrap = inputs;
    wrap.SourceRingEnd = wrap.SpawnStart = 990;
    wrap.RingEnd = 10;
    plan = Moonlight::PlanGpuSort( wrap );
    CHECK( plan.Reuse );
    CHECK( plan.AppendCount == 20 );

    // Anything else sorts: not allowed, no order, a reset, a stale order, an overflowing burst,
    // or too many particles for one copy.
    for( int variant = 0; variant < 6; ++variant )
    {
        Moonlight::GpuSortInputs bad = inputs;
        switch( variant )
        {
        case 0: bad.ReuseAllowed = false; break;
        case 1: bad.HasSource = false; break;
        case 2: bad.Reset = true; break;
        case 3: bad.SourceFrame = 7; break;
        case 4: bad.SpawnStart = 0; bad.SpawnCount = 1000; bad.RingEnd = 900; break;
        case 5: bad.SourceCount = 990; break;
        }
        plan = Moonlight::PlanGpuSort( bad );
        CHECK_MESSAGE( plan.Sort, "variant " << variant );
        CHECK_FALSE( plan.Reuse );
    }
}
