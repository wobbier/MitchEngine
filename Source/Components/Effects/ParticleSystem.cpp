#include "PCH.h"
#include "ParticleSystem.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <cmath>

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

ME_REFLECT_ENUM( ParticleShape, { { "Point", ParticleShape::Point }, { "Sphere", ParticleShape::Sphere }, { "Hemisphere", ParticleShape::Hemisphere }, { "Cone", ParticleShape::Cone }, { "Box", ParticleShape::Box } } )
ME_REFLECT_ENUM( ParticleSpace, { { "World", ParticleSpace::World }, { "Local", ParticleSpace::Local } } )
ME_REFLECT_ENUM( Moonlight::ParticleBlend, { { "Additive", Moonlight::ParticleBlend::Additive }, { "Alpha", Moonlight::ParticleBlend::Alpha } } )
ME_REFLECT_ENUM( Moonlight::ParticleAlignment, { { "Billboard", Moonlight::ParticleAlignment::Billboard }, { "Stretched", Moonlight::ParticleAlignment::Stretched }, { "Horizontal", Moonlight::ParticleAlignment::Horizontal } } )

ME_REFLECT_BEGIN( ParticleSystem )
    ME_FIELD( Looping ).Category( "Emission" );
    ME_FIELD( Duration ).Category( "Emission" ).Range( 0.05f, 120.f ).Tooltip( "Seconds a non-looping system emits for" );
    ME_FIELD( EmissionRate ).Category( "Emission" ).Range( 0.f, 10000.f ).Tooltip( "Particles per second" );
    ME_FIELD( BurstCount ).Category( "Emission" ).Range( 0.f, 10000.f ).Tooltip( "Particles emitted at once when the system starts (and each loop)" );
    ME_FIELD( MaxParticles ).Category( "Emission" ).Range( 1.f, 100000.f );
    ME_FIELD( SimulationSpace ).Category( "Emission" );
    ME_FIELD( Shape ).Category( "Shape" );
    ME_FIELD( ShapeRadius ).Category( "Shape" ).Range( 0.f, 100.f );
    ME_FIELD( ConeAngle ).Category( "Shape" ).Range( 0.f, 90.f );
    ME_FIELD( BoxSize ).Category( "Shape" );
    ME_FIELD( EmitFromShell ).Category( "Shape" ).Tooltip( "Spawn on the surface instead of inside the volume" );
    ME_FIELD( Lifetime ).Category( "Particles" ).Tooltip( "Seconds (min, max)" );
    ME_FIELD( Speed ).Category( "Particles" ).Tooltip( "m/s (min, max)" );
    ME_FIELD( Size ).Category( "Particles" ).Tooltip( "Metres (min, max)" );
    ME_FIELD( Rotation ).Category( "Particles" ).Tooltip( "Degrees (min, max)" );
    ME_FIELD( AngularVelocity ).Category( "Particles" ).Tooltip( "Degrees per second (min, max)" );
    ME_FIELD( StartColor ).Category( "Particles" ).Color();
    ME_FIELD( StartColor2 ).Category( "Particles" ).Color().Tooltip( "Each particle picks a colour between the two" );
    ME_FIELD( StartAlpha ).Category( "Particles" ).Range( 0.f, 1.f );
    ME_FIELD( Intensity ).Category( "Particles" ).Range( 0.f, 50.f ).Tooltip( "HDR brightness (bloom picks up values above 1)" );
    ME_FIELD( EndSize ).Category( "Over Lifetime" ).Range( 0.f, 10.f ).Tooltip( "Size multiplier at the end of life" );
    ME_FIELD( EndColor ).Category( "Over Lifetime" ).Color();
    ME_FIELD( EndAlpha ).Category( "Over Lifetime" ).Range( 0.f, 1.f );
    ME_FIELD( FadeIn ).Category( "Over Lifetime" ).Range( 0.f, 1.f ).Tooltip( "Fraction of life spent fading in" );
    ME_FIELD( Gravity ).Category( "Over Lifetime" ).Range( -10.f, 10.f ).Tooltip( "Multiples of 9.81 m/s^2; negative rises" );
    ME_FIELD( Drag ).Category( "Over Lifetime" ).Range( 0.f, 20.f );
    ME_FIELD( NoiseStrength ).Category( "Over Lifetime" ).Range( 0.f, 50.f );
    ME_FIELD( NoiseFrequency ).Category( "Over Lifetime" ).Range( 0.01f, 20.f );
    ME_FIELD( Texture ).Category( "Rendering" ).Asset( "Texture" );
    ME_FIELD( Blend ).Category( "Rendering" );
    ME_FIELD( Alignment ).Category( "Rendering" );
    ME_FIELD( StretchFactor ).Category( "Rendering" ).Range( 0.f, 5.f ).Tooltip( "Stretched: extra length per m/s" );
    ME_FIELD( FlipbookColumns ).Category( "Rendering" ).Range( 1.f, 64.f );
    ME_FIELD( FlipbookRows ).Category( "Rendering" ).Range( 1.f, 64.f );
    ME_FIELD( FlipbookFps ).Category( "Rendering" ).Range( 0.f, 120.f ).Tooltip( "0 plays the flipbook once over each particle's life" );
    ME_FIELD( SoftParticleDistance ).Category( "Rendering" ).Range( 0.f, 10.f ).Tooltip( "Fade where particles meet geometry (0 = hard edges)" );
ME_REFLECT_END()


ParticleSystem::ParticleSystem()
    : Component( "ParticleSystem" )
{
}


void ParticleSystem::Init()
{
    Restart();
}


void ParticleSystem::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    Restart();
}


void ParticleSystem::Restart()
{
    m_particles.clear();
    m_time = 0.f;
    m_emitAccumulator = 0.f;
    m_burstDone = false;
}


float ParticleSystem::Random01()
{
    // xorshift32
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>( m_rng >> 8 ) * ( 1.f / 16777216.f );
}


float ParticleSystem::RandomRange( const Vector2& InRange )
{
    return InRange.x + ( InRange.y - InRange.x ) * Random01();
}


void ParticleSystem::ResolveTexture()
{
    const std::string& path = Texture.GetLocalPathString();
    if( path == m_loadedTexture )
    {
        return;
    }
    m_loadedTexture = path;
    m_texture = ( !path.empty() && Texture.Exists ) ? ResourceCache::GetInstance().Get<Moonlight::Texture>( Texture ) : nullptr;
}


void ParticleSystem::Emit( const glm::mat4& InWorld )
{
    if( static_cast<int>( m_particles.size() ) >= MaxParticles )
    {
        return;
    }
    constexpr float kPi = 3.14159265f;
    Particle particle;
    glm::vec3 position( 0.f );
    glm::vec3 direction( 0.f, 1.f, 0.f );

    auto randomDirection = [this, kPi]() {
        const float z = Random01() * 2.f - 1.f;
        const float phi = Random01() * 2.f * kPi;
        const float r = std::sqrt( std::max( 0.f, 1.f - z * z ) );
        return glm::vec3( r * std::cos( phi ), z, r * std::sin( phi ) );
    };

    switch( Shape )
    {
    case ParticleShape::Point:
        direction = randomDirection();
        break;
    case ParticleShape::Sphere:
    case ParticleShape::Hemisphere:
    {
        direction = randomDirection();
        if( Shape == ParticleShape::Hemisphere )
        {
            direction.y = std::abs( direction.y );
        }
        const float radius = EmitFromShell ? ShapeRadius : ShapeRadius * std::cbrt( Random01() );
        position = direction * radius;
        break;
    }
    case ParticleShape::Cone:
    {
        const float phi = Random01() * 2.f * kPi;
        const float spread = std::sqrt( Random01() );
        const float disk = EmitFromShell ? ShapeRadius : ShapeRadius * spread;
        position = glm::vec3( std::cos( phi ) * disk, 0.f, std::sin( phi ) * disk );
        // Directions fan out with the distance from the axis (like a real nozzle).
        const float angle = glm::radians( ConeAngle ) * ( EmitFromShell ? 1.f : spread );
        direction = glm::normalize( glm::vec3( std::cos( phi ) * std::sin( angle ), std::cos( angle ), std::sin( phi ) * std::sin( angle ) ) );
        break;
    }
    case ParticleShape::Box:
        position = glm::vec3( ( Random01() - 0.5f ) * BoxSize.x, ( Random01() - 0.5f ) * BoxSize.y, ( Random01() - 0.5f ) * BoxSize.z );
        break;
    }

    const glm::vec3 velocity = direction * RandomRange( Speed );
    if( SimulationSpace == ParticleSpace::World )
    {
        particle.Position = glm::vec3( InWorld * glm::vec4( position, 1.f ) );
        particle.Velocity = glm::vec3( InWorld * glm::vec4( velocity, 0.f ) );
    }
    else
    {
        particle.Position = position;
        particle.Velocity = velocity;
    }
    particle.Lifetime = std::max( RandomRange( Lifetime ), 0.01f );
    particle.Size = RandomRange( Size );
    particle.Rotation = glm::radians( RandomRange( Rotation ) );
    particle.AngularVelocity = glm::radians( RandomRange( AngularVelocity ) );
    const float t = Random01();
    particle.Color = glm::mix( StartColor.InternalVector, StartColor2.InternalVector, t );
    particle.Seed = Random01() * 100.f;
    m_particles.push_back( particle );
}


void ParticleSystem::Simulate( float InDeltaSeconds, const glm::mat4& InWorld )
{
    const float dt = std::clamp( InDeltaSeconds, 0.f, 0.1f );
    m_time += dt;

    // Ageing and motion.
    const glm::vec3 gravity( 0.f, -9.81f * Gravity, 0.f );
    const float dragFactor = std::exp( -Drag * dt );
    for( size_t i = 0; i < m_particles.size(); )
    {
        Particle& particle = m_particles[i];
        particle.Age += dt;
        if( particle.Age >= particle.Lifetime )
        {
            particle = m_particles.back();
            m_particles.pop_back();
            continue;
        }
        glm::vec3 acceleration = gravity;
        if( NoiseStrength > 0.f )
        {
            // Cheap divergence-ish turbulence: offset sines per axis, phased per particle.
            const glm::vec3 p = particle.Position * NoiseFrequency;
            const float time = m_time + particle.Seed;
            acceleration += NoiseStrength * glm::vec3( std::sin( p.y * 1.7f + time * 1.3f ) + std::cos( p.z * 0.9f + time ),
                std::sin( p.z * 1.3f + time * 0.7f ) * 0.5f,
                std::sin( p.x * 1.1f + time * 1.7f ) + std::cos( p.y * 0.8f - time ) );
        }
        particle.Velocity = ( particle.Velocity + acceleration * dt ) * dragFactor;
        particle.Position += particle.Velocity * dt;
        particle.Rotation += particle.AngularVelocity * dt;
        ++i;
    }

    // Emission.
    if( Looping && m_time >= Duration )
    {
        m_time = std::fmod( m_time, std::max( Duration, 0.05f ) );
        m_burstDone = false;
    }
    if( IsEmitting() )
    {
        if( !m_burstDone )
        {
            for( int i = 0; i < BurstCount; ++i )
            {
                Emit( InWorld );
            }
            m_burstDone = true;
        }
        m_emitAccumulator += EmissionRate * dt;
        const int count = static_cast<int>( m_emitAccumulator );
        m_emitAccumulator -= static_cast<float>( count );
        for( int i = 0; i < count; ++i )
        {
            Emit( InWorld );
        }
    }
}


void ParticleSystem::FillBatch( Moonlight::ParticleBatch& OutBatch, const glm::mat4& InWorld ) const
{
    OutBatch.Texture = m_texture ? m_texture->TexHandle : bgfx::TextureHandle( BGFX_INVALID_HANDLE );
    OutBatch.Blend = Blend;
    OutBatch.Alignment = Alignment;
    OutBatch.StretchFactor = StretchFactor;
    OutBatch.Softness = SoftParticleDistance;
    OutBatch.FlipbookColumns = static_cast<uint16_t>( std::clamp( FlipbookColumns, 1, 64 ) );
    OutBatch.FlipbookRows = static_cast<uint16_t>( std::clamp( FlipbookRows, 1, 64 ) );
    OutBatch.Bounds = AABB();
    OutBatch.Instances.clear();
    OutBatch.Instances.reserve( m_particles.size() );

    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    const float frames = static_cast<float>( OutBatch.FlipbookColumns * OutBatch.FlipbookRows );
    const bool local = SimulationSpace == ParticleSpace::Local;
    for( const Particle& particle : m_particles )
    {
        const float life = particle.Age / particle.Lifetime;
        const glm::vec3 position = local ? glm::vec3( InWorld * glm::vec4( particle.Position, 1.f ) ) : particle.Position;
        const glm::vec3 velocity = local ? glm::vec3( InWorld * glm::vec4( particle.Velocity, 0.f ) ) : particle.Velocity;
        const float size = particle.Size * glm::mix( 1.f, EndSize, life );
        const glm::vec3 srgb = glm::mix( particle.Color, EndColor.InternalVector, life );
        const float fadeIn = FadeIn > 0.f ? std::min( life / FadeIn, 1.f ) : 1.f;
        const float alpha = glm::mix( StartAlpha, EndAlpha, life ) * fadeIn;
        const float frame = FlipbookFps > 0.f ? std::floor( particle.Age * FlipbookFps ) : std::floor( life * frames );

        Moonlight::ParticleInstance instance;
        instance.PositionSize = glm::vec4( position, size );
        instance.Color = glm::vec4( toLinear( srgb.x ) * Intensity, toLinear( srgb.y ) * Intensity, toLinear( srgb.z ) * Intensity, alpha );
        instance.Params = glm::vec4( particle.Rotation, frame, 0.f, 0.f );
        instance.Velocity = glm::vec4( velocity, 0.f );
        OutBatch.Instances.push_back( instance );

        const Vector3 extent( size, size, size );
        OutBatch.Bounds.Encapsulate( Vector3( position.x, position.y, position.z ) - extent );
        OutBatch.Bounds.Encapsulate( Vector3( position.x, position.y, position.z ) + extent );
    }
}


#if USING( ME_EDITOR )
void ParticleSystem::OnEditorInspect()
{
    ImGui::Text( "Particles: %zu / %d", m_particles.size(), MaxParticles );
    ImGui::SameLine();
    if( ImGui::SmallButton( "Restart" ) )
    {
        Restart();
    }
}
#endif
