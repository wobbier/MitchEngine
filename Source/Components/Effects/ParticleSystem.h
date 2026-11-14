#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"
#include "Path.h"
#include "Pointers.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <glm/glm.hpp>
#include <deque>
#include <vector>

class Transform;
namespace Moonlight { class Texture; class ShaderGraphAsset; }

enum class ParticleShape : uint8_t
{
    Point = 0,
    Sphere,
    Hemisphere,
    Cone,       // along the entity's up axis
    Box,
};

enum class ParticleSpace : uint8_t
{
    World = 0,  // particles stay where they were born when the emitter moves
    Local,      // particles move with the emitter
};

enum class ParticleSimulation : uint8_t
{
    CPU = 0,    // the reference: simulated on job threads, uploaded each frame
    GPU,        // a compute shader steps the particles where they are drawn (Additive only; falls back to CPU)
};

// Particle emitter. Particles spawn from a shape at a rate (plus an optional burst), take
// randomized lifetime / speed / size / rotation / colour, fall with gravity, slow with drag, wander
// with noise, and fade and scale over their life. Rendered as instanced camera-facing (or
// velocity-stretched / flat) quads with optional flipbook animation and soft edges against the
// scene. Simulates in the editor too, so effects preview while you edit them. Simulation picks
// where the particles are stepped: on the CPU (the reference), or on the GPU by a compute shader
// (Moonlight/Particles/GpuParticles.h), where the CPU still runs the emitter and only sends what
// to spawn. GPU systems fall back to the CPU when they blend Alpha (they need a sort) or compute
// isn't available.
class ParticleSystem
    : public Component<ParticleSystem>
{
    ME_REFLECTABLE( ParticleSystem )
public:
    ParticleSystem();

    virtual void Init() final;

    // Emission
    bool Looping = true;
    float Duration = 5.f;
    float EmissionRate = 30.f;
    int BurstCount = 0;
    int MaxParticles = 1000;
    ParticleSpace SimulationSpace = ParticleSpace::World;
    ParticleSimulation Simulation = ParticleSimulation::CPU;

    // Shape
    ParticleShape Shape = ParticleShape::Cone;
    float ShapeRadius = 0.25f;
    float ConeAngle = 20.f;
    Vector3 BoxSize = Vector3( 1.f, 1.f, 1.f );
    bool EmitFromShell = false;

    // Particles (x = min, y = max)
    Vector2 Lifetime = Vector2( 1.f, 2.f );
    Vector2 Speed = Vector2( 1.5f, 3.f );
    Vector2 Size = Vector2( 0.15f, 0.35f );
    Vector2 Rotation = Vector2( 0.f, 360.f );
    Vector2 AngularVelocity = Vector2( -45.f, 45.f );
    Vector3 StartColor = Vector3( 1.f, 0.6f, 0.2f );
    Vector3 StartColor2 = Vector3( 1.f, 0.85f, 0.4f );
    float StartAlpha = 1.f;
    float Intensity = 2.f;

    // Over lifetime
    float EndSize = 0.5f;           // multiplier at the end of life
    Vector3 EndColor = Vector3( 0.6f, 0.15f, 0.05f );
    float EndAlpha = 0.f;
    float FadeIn = 0.1f;            // fraction of life spent fading in
    float Gravity = -0.2f;          // multiples of 9.81 m/s^2 (negative rises)
    float Drag = 0.5f;
    float NoiseStrength = 0.6f;
    float NoiseFrequency = 1.f;

    // Rendering
    Path Texture;
    Moonlight::ParticleBlend Blend = Moonlight::ParticleBlend::Additive;
    Moonlight::ParticleAlignment Alignment = Moonlight::ParticleAlignment::Billboard;
    float StretchFactor = 0.1f;
    int FlipbookColumns = 1;
    int FlipbookRows = 1;
    float FlipbookFps = 0.f;        // 0 = play the flipbook once over each particle's life
    float SoftParticleDistance = 0.4f;
    bool Lit = false;               // shade with the scene's lights (smoke, dust); glows stay unlit
    // A Particle shader graph colouring the quads (empty: the texture and colour over life as is).
    Path Graph;
    // Values for the graph's exposed properties (the graph's defaults where unset).
    Moonlight::ShaderGraphParams GraphParams;

    // Runtime
    void Simulate( float InDeltaSeconds, const glm::mat4& InWorld );
    void FillBatch( Moonlight::ParticleBatch& OutBatch, const glm::mat4& InWorld ) const;
    void Restart();
    // Script control (CS-5 c): Play restarts emission, Stop ends it (live particles finish their
    // lives), Burst emits InCount at once on the next simulation step.
    void Play();
    void Stop();
    void Burst( int InCount );
    // Loads the texture and graph when they changed (main thread).
    void ResolveTexture();
    // CPU: the live particles. GPU: an estimate (nothing is read back): the particles spawned
    // within the longest lifetime, at most MaxParticles.
    size_t GetParticleCount() const { return m_gpuActive ? m_gpuLiveEstimate : m_particles.size(); }
    bool IsEmitting() const { return !m_stopped && ( Looping || m_time < Duration ); }

    // GPU simulation (main thread, before stepping): decides the path for this frame. The GPU path
    // runs when Simulation is GPU, Blend is Additive and InComputeActive (the renderer's
    // IsComputeActive()). Switching paths or changing MaxParticles restarts the system.
    void UpdateSimulationMode( bool InComputeActive );
    bool UsesGpuSimulation() const { return m_gpuActive; }
    // Creates (or resizes) the compute buffers of a GPU system (main thread, renderer running).
    void EnsureGpuBuffers();
    // The GPU path's CPU half (any thread): advances the emitter like Simulate, and fills OutBatch
    // with this frame's spawn range, the instance count to draw, the live estimate and
    // conservative bounds. The renderer dispatches the step (no GPU work happens here).
    void StepGpu( float InDeltaSeconds, const glm::mat4& InWorld, Moonlight::ParticleBatch& OutBatch );

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    struct Particle
    {
        glm::vec3 Position;
        glm::vec3 Velocity;
        glm::vec3 Color;
        float Age = 0.f;
        float Lifetime = 1.f;
        float Size = 1.f;
        float Rotation = 0.f;
        float AngularVelocity = 0.f;
        float Seed = 0.f;
    };

    float Random01();
    float RandomRange( const Vector2& InRange );
    void Emit( const glm::mat4& InWorld );
    // The emitter half of a step, shared by both paths: clamps the step and advances the clock
    // (BeginStep), then after the particles moved wraps a looping clock, re-arms the start burst
    // and counts this step's spawns (script bursts, start burst, rate) (AdvanceEmitter).
    float BeginStep( float InDeltaSeconds );
    int AdvanceEmitter( float InDeltaSeconds );
    // The batch fields both paths share (texture, blend, alignment, flipbook, graph).
    void FillBatchSettings( Moonlight::ParticleBatch& OutBatch ) const;
    float GpuMaxLifetime() const;
    uint32_t GpuCapacity() const;
    // Conservative bounds of particles spawned with InWorld (World space), or of every particle in
    // the emitter's local space (Local space; the caller transforms it).
    AABB GpuSpawnBounds( const glm::mat4& InWorld ) const;

    std::vector<Particle> m_particles;
    float m_time = 0.f;
    float m_emitAccumulator = 0.f;
    bool m_burstDone = false;
    bool m_stopped = false;
    int m_pendingBurst = 0;
    uint32_t m_rng = 0x9e3779b9u;
    SharedPtr<Moonlight::Texture> m_texture;
    std::string m_loadedTexture;
    SharedPtr<Moonlight::ShaderGraphAsset> m_graph;
    std::string m_loadedGraph;

    // GPU path (CPU side). One entry per step that spawned, oldest first, dropped once its
    // particles must all have died: the live estimate and, in World space, the bounds.
    struct GpuSpawn
    {
        float Age = 0.f;
        uint32_t Count = 0;
        AABB Bounds;
    };
    // Buffers aren't shared by copies of the component (a copy makes its own).
    struct GpuBufferHolder
    {
        GpuBufferHolder() = default;
        GpuBufferHolder( const GpuBufferHolder& ) {}
        GpuBufferHolder& operator=( const GpuBufferHolder& ) { return *this; }
        SharedPtr<Moonlight::GpuParticleBuffers> Buffers;
    };
    bool m_gpuActive = false;
    bool m_gpuResetPending = true;
    uint32_t m_gpuCapacity = 0;
    uint32_t m_gpuCursor = 0;
    uint32_t m_gpuHighWater = 0;
    uint32_t m_gpuStepIndex = 0;
    size_t m_gpuLiveEstimate = 0;
    std::deque<GpuSpawn> m_gpuSpawns;
    GpuBufferHolder m_gpu;

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( ParticleSystem, "Effects" )
