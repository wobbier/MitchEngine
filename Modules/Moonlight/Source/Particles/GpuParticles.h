#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include "Pointers.h"

// GPU particle simulation (ParticleSystem::Simulation = GPU): a compute shader
// (Assets/Shaders/Particles/ParticleSim.comp) steps every particle of a system in place and writes
// the instances Particle.vert draws, so nothing goes back to the CPU. The CPU still runs the
// emitter (clock, bursts, rate) and sends a spawn range each frame (GpuParticleStep).
// Alpha-blended systems are also sorted back to front per camera on the GPU (GpuParticleSorter:
// ParticleSortLocal/Global/Gather.comp), still without a readback.
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

    // Alpha sort (GpuParticleSorter): entries one group sorts in groupshared memory (SORT_BLOCK in
    // ParticleSort.sh). A sort covers at least this many entries.
    constexpr uint32_t kGpuParticleSortBlock = 512;

    // One camera's sorted copy of an Alpha GPU system: the sort entries (one vec4 each: key, slot),
    // the instances in back-to-front order, and the indirect draw arguments the gather pass writes
    // (when the backend draws indirect). Owned by the system's GpuParticleBuffers; main thread only.
    class GpuParticleSortTarget
    {
    public:
        GpuParticleSortTarget( const void* InCamera, uint32_t InCapacity );
        ~GpuParticleSortTarget();
        GpuParticleSortTarget( const GpuParticleSortTarget& ) = delete;
        GpuParticleSortTarget& operator=( const GpuParticleSortTarget& ) = delete;

        // The sort entries are created on the first sort (a camera that only reuses another's order
        // doesn't need them).
        bool EnsureEntries();
        bool HasEntries() const { return bgfx::isValid( m_entries ); }
        bgfx::DynamicVertexBufferHandle GetEntries() const { return m_entries; }
        bgfx::DynamicVertexBufferHandle GetInstances() const { return m_instances; }
        bgfx::IndirectBufferHandle GetDrawArgs() const { return m_drawArgs; }
        uint32_t GetEntryCapacity() const { return m_entryCapacity; }
        bool IsValid() const { return bgfx::isValid( m_instances ); }

        const void* Camera = nullptr;
        uint64_t LastUsedFrame = 0;
        // This camera's latest order (its entries): the frame, the entries that map to slots and the
        // ring end they were taken at. Another camera can gather from it (ParticleSortCameras::MainCamera).
        bool HasOrder = false;
        uint64_t OrderFrame = 0;
        uint32_t OrderCount = 0;
        uint32_t OrderRingEnd = 0;
        // What to draw this frame: instances [0, DrawCount), or, with DrawIndirect, the count the
        // gather pass wrote into GetDrawArgs() (the live ones only).
        uint32_t DrawCount = 0;
        bool DrawIndirect = false;
        bool Ready = false;

    private:
        uint32_t m_capacity = 0;
        uint32_t m_entryCapacity = 0;
        bgfx::DynamicVertexBufferHandle m_entries = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle m_instances = BGFX_INVALID_HANDLE;
        bgfx::IndirectBufferHandle m_drawArgs = BGFX_INVALID_HANDLE;
    };

    // A system's two compute buffers of Capacity 64-byte records (four vec4s each): the particle
    // state and the instances Particle.vert reads (ParticleInstance's layout). Created, used and
    // destroyed on the main thread. Their contents are undefined until a dispatch has reset them.
    // Alpha systems also keep a sorted copy per camera that draws them (GpuParticleSortTarget).
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

        // The sorted copy for InCamera (created on first use; null if its buffers can't be created).
        // Copies no camera has drawn for a while are dropped.
        GpuParticleSortTarget* AcquireSortTarget( const void* InCamera, uint64_t InFrame );
        GpuParticleSortTarget* FindSortTarget( const void* InCamera );
        size_t GetSortTargetCount() const { return m_sortTargets.size(); }

        // Renderer side: the next dispatch must reset every slot (new buffers, or a reset step was
        // skipped while the program loaded), and whether one has run, so the instances can be drawn.
        bool NeedsReset = true;
        bool Initialized = false;
        // The camera (not a render texture camera) that sorted this system last.
        const void* MainOrderCamera = nullptr;

    private:
        uint32_t m_capacity = 0;
        bgfx::DynamicVertexBufferHandle m_state = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle m_instances = BGFX_INVALID_HANDLE;
        std::vector<UniquePtr<GpuParticleSortTarget>> m_sortTargets;
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

    // The alpha sort's dispatch schedule (ParticleSort.sh). InEntries is a power of two, at least
    // kGpuParticleSortBlock. LocalSort builds the entries and sorts each block; Flip and Disperse are
    // one global step of that size; LocalMerge finishes a merge in groupshared memory.
    struct GpuSortPass
    {
        enum class Kind : uint8_t { LocalSort, Flip, Disperse, LocalMerge };
        Kind Type = Kind::LocalSort;
        uint32_t Size = 0;
    };
    // Entries a sort of InCount slots covers: the next power of two, at least kGpuParticleSortBlock.
    uint32_t GpuSortEntryCount( uint32_t InCount );
    void BuildGpuSortPasses( uint32_t InEntries, std::vector<GpuSortPass>& OutPasses );

    // What one camera does for one Alpha GPU system this frame (GpuParticleSorter::Plan).
    struct GpuSortPlan
    {
        bool Sort = false;          // sort this camera's own order
        bool Reuse = false;         // gather with another camera's order (Source)
        uint32_t AppendCount = 0;   // Reuse: births since that order, drawn after it
    };
    struct GpuSortInputs
    {
        bool ReuseAllowed = false;  // ParticleSortCameras::MainCamera and a render texture camera
        bool HasSource = false;     // the main order exists (another camera's, with its entries)
        uint64_t Frame = 0;
        uint64_t SourceFrame = 0;
        uint32_t SourceCount = 0;
        uint32_t SourceRingEnd = 0;
        uint32_t Capacity = 1;
        uint32_t RingEnd = 0;       // this frame's
        uint32_t SpawnStart = 0;    // this frame's step
        uint32_t SpawnCount = 0;
        bool Reset = false;
    };
    // Reuse needs the main order from this frame or the last, and this frame's births to follow it
    // exactly in the ring; anything else (a restart, an overflowing burst, frames missed) sorts.
    GpuSortPlan PlanGpuSort( const GpuSortInputs& InInputs );

    // The alpha sort's programs and uniform (owned by BGFXRenderer while compute is supported).
    // Per camera that draws an Alpha GPU system, RenderParticles calls Sort in a view ordered
    // after the frame compute view and before that camera's particle view.
    class GpuParticleSorter
    {
    public:
        GpuParticleSorter();
        ~GpuParticleSorter();
        GpuParticleSorter( const GpuParticleSorter& ) = delete;
        GpuParticleSorter& operator=( const GpuParticleSorter& ) = delete;

        bool IsReady() const;

        struct Request
        {
            const void* Camera = nullptr;
            bool ExtraCamera = false;       // a render texture camera
            bool ReuseMainOrder = false;    // ParticleSortCameras::MainCamera
            glm::vec3 ViewDepth = glm::vec3( 0.f, 0.f, 1.f );  // the view matrix's depth row
            uint64_t Frame = 0;
            uint32_t Count = 0;             // slots that can be live (the ring segment ending at RingEnd)
            uint32_t RingEnd = 0;
            uint32_t SpawnStart = 0;
            uint32_t SpawnCount = 0;
            bool Reset = false;
        };
        // Dispatches the sort (or the gather from the main camera's order) into InView and returns
        // the camera's target to draw from, or null when nothing could run (draw unsorted).
        GpuParticleSortTarget* Sort( bgfx::ViewId InView, GpuParticleBuffers& InBuffers, const Request& InRequest );

        static constexpr const char* kLocalShader = "Assets/Shaders/Particles/ParticleSortLocal.comp";
        static constexpr const char* kGlobalShader = "Assets/Shaders/Particles/ParticleSortGlobal.comp";
        static constexpr const char* kGatherShader = "Assets/Shaders/Particles/ParticleSortGather.comp";
        static constexpr uint16_t kParamCount = 3;

    private:
        void Gather( bgfx::ViewId InView, GpuParticleBuffers& InBuffers, GpuParticleSortTarget& InTarget, const GpuParticleSortTarget& InOrder,
            uint32_t InOrderCount, uint32_t InAppendStart, uint32_t InAppendCount, bool InWriteArgs );

        SharedPtr<ProgramRef> m_local;
        SharedPtr<ProgramRef> m_global;
        SharedPtr<ProgramRef> m_gather;
        bgfx::UniformHandle u_particleSort = BGFX_INVALID_HANDLE;
        bool m_drawIndirect = false;
        bool m_loggedFirstSort = false;
        std::vector<GpuSortPass> m_passes;
    };
}
