#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"
#include "Path.h"
#include "Pointers.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <glm/glm.hpp>
#include <vector>

class Transform;
namespace Moonlight { class Texture; }

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

// CPU particle emitter. Particles spawn from a shape at a rate (plus an optional burst), take
// randomized lifetime / speed / size / rotation / colour, fall with gravity, slow with drag, wander
// with noise, and fade and scale over their life. Rendered as instanced camera-facing (or
// velocity-stretched / flat) quads with optional flipbook animation and soft edges against the
// scene. Simulates in the editor too, so effects preview while you edit them.
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

    // Runtime
    void Simulate( float InDeltaSeconds, const glm::mat4& InWorld );
    void FillBatch( Moonlight::ParticleBatch& OutBatch, const glm::mat4& InWorld ) const;
    void Restart();
    // Loads the texture when it changed (main thread).
    void ResolveTexture();
    size_t GetParticleCount() const { return m_particles.size(); }
    bool IsEmitting() const { return Looping || m_time < Duration; }

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

    std::vector<Particle> m_particles;
    float m_time = 0.f;
    float m_emitAccumulator = 0.f;
    bool m_burstDone = false;
    uint32_t m_rng = 0x9e3779b9u;
    SharedPtr<Moonlight::Texture> m_texture;
    std::string m_loadedTexture;

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( ParticleSystem, "Effects" )
