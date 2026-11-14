#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include "Pointers.h"

// GPU particle simulation (ParticleSystem::Simulation = GPU): a compute shader
// (Assets/Shaders/Particles/ParticleSim.comp) steps every particle of a system in place and writes
// the instances Particle.vert draws, so nothing goes back to the CPU. The CPU still runs the
// emitter (clock, bursts, rate) and sends a spawn range each frame (GpuParticleStep).
namespace Moonlight
{
    struct ProgramRef;

    // Threads per compute group in ParticleSim.comp (NUM_THREADS).
    constexpr uint32_t kGpuParticleGroupSize = 64;
    // Slots a GPU system can have. Counts reach the shader as floats, which are exact to 2^24.
    constexpr uint32_t kMaxGpuParticles = 1u << 20;

    // One frame's work for one system: the emitter's settings and this frame's spawns. Plain data,
    // filled on a job thread (ParticleSystem::StepGpu) and dispatched by the renderer.
    struct GpuParticleStep
    {
        glm::mat4 World = glm::mat4( 1.f );     // the emitter this frame
        float DeltaSeconds = 0.f;               // already clamped like the CPU simulation
        float Time = 0.f;                       // the emitter clock the noise phase reads
        uint32_t SpawnStart = 0;                // ring slots [start, start + count) mod capacity
        uint32_t SpawnCount = 0;                //   are (re)born this frame
        uint32_t Seed = 0;                      // per frame, 24 bits
        bool Reset = false;                     // every slot dies first (restart, mode switch)
        bool Local = false;                     // ParticleSpace::Local
        bool EmitFromShell = false;
        uint8_t Shape = 0;                      // ParticleShape
        float ShapeRadius = 0.f;
        float ConeAngle = 0.f;                  // radians
        glm::vec3 BoxSize = glm::vec3( 1.f );
        glm::vec2 Lifetime = glm::vec2( 1.f );  // (min, max) for every range
        glm::vec2 Speed = glm::vec2( 0.f );
        glm::vec2 Size = glm::vec2( 1.f );
        glm::vec2 Rotation = glm::vec2( 0.f );          // radians
        glm::vec2 AngularVelocity = glm::vec2( 0.f );   // radians per second
        glm::vec3 StartColor = glm::vec3( 1.f );        // sRGB
        glm::vec3 StartColor2 = glm::vec3( 1.f );
        glm::vec3 EndColor = glm::vec3( 1.f );
        float StartAlpha = 1.f;
        float EndAlpha = 0.f;
        float Intensity = 1.f;
        float Gravity = 0.f;                    // multiples of 9.81 m/s^2
        float Drag = 0.f;
        float NoiseStrength = 0.f;
        float NoiseFrequency = 1.f;
        float EndSize = 1.f;
        float FadeIn = 0.f;
        float FlipbookFps = 0.f;
        float FlipbookFrames = 1.f;             // columns * rows
    };

    // A system's two compute buffers of Capacity 64-byte records (four vec4s each): the particle
    // state and the instances Particle.vert reads (ParticleInstance's layout). Created, used and
    // destroyed on the main thread. Their contents are undefined until a dispatch has reset them.
    class GpuParticleBuffers
    {
    public:
        explicit GpuParticleBuffers( uint32_t InCapacity );
        ~GpuParticleBuffers();
        GpuParticleBuffers( const GpuParticleBuffers& ) = delete;
        GpuParticleBuffers& operator=( const GpuParticleBuffers& ) = delete;

        uint32_t GetCapacity() const { return m_capacity; }
        bgfx::DynamicVertexBufferHandle GetState() const { return m_state; }
        bgfx::DynamicVertexBufferHandle GetInstances() const { return m_instances; }
        bool IsValid() const { return bgfx::isValid( m_state ) && bgfx::isValid( m_instances ); }

        // Renderer side: the next dispatch must reset every slot (new buffers, or a reset step was
        // skipped while the program loaded), and whether one has run, so the instances can be drawn.
        bool NeedsReset = true;
        bool Initialized = false;

    private:
        uint32_t m_capacity = 0;
        bgfx::DynamicVertexBufferHandle m_state = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle m_instances = BGFX_INVALID_HANDLE;
    };

    // The compute program and its uniforms (owned by BGFXRenderer while compute is supported).
    class GpuParticleSimulator
    {
    public:
        GpuParticleSimulator();
        ~GpuParticleSimulator();
        GpuParticleSimulator( const GpuParticleSimulator& ) = delete;
        GpuParticleSimulator& operator=( const GpuParticleSimulator& ) = delete;

        // The program has loaded (it is invalid while loading, after a failed compile or a failed
        // hot reload).
        bool IsReady() const;

        // Steps InBuffers by InStep in InView (main thread, inside the frame). Returns false and
        // dispatches nothing while the program isn't ready; a reset that was asked for is then kept
        // for the next dispatch.
        bool Dispatch( bgfx::ViewId InView, GpuParticleBuffers& InBuffers, const GpuParticleStep& InStep );

        static constexpr const char* kShader = "Assets/Shaders/Particles/ParticleSim.comp";
        static constexpr uint16_t kParamCount = 12;

    private:
        SharedPtr<ProgramRef> m_program;
        bgfx::UniformHandle u_particleSim = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_particleWorld = BGFX_INVALID_HANDLE;
        bool m_loggedFirstDispatch = false;
    };

    // Packs InStep into the shader's u_particleSim[kParamCount] (see ParticleSim.comp).
    void PackGpuParticleStep( const GpuParticleStep& InStep, glm::vec4 OutParams[GpuParticleSimulator::kParamCount] );
}
