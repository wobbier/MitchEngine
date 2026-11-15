#include "PCH.h"
#include "ParticleSystem.h"
#include "Graphics/Texture.h"
#include "Materials/ShaderGraphAsset.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <cmath>

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

ME_REFLECT_ENUM( ParticleShape, { { "Point", ParticleShape::Point }, { "Sphere", ParticleShape::Sphere }, { "Hemisphere", ParticleShape::Hemisphere }, { "Cone", ParticleShape::Cone }, { "Box", ParticleShape::Box } } )
ME_REFLECT_ENUM( ParticleSpace, { { "World", ParticleSpace::World }, { "Local", ParticleSpace::Local } } )
ME_REFLECT_ENUM( ParticleSimulation, { { "CPU", ParticleSimulation::CPU }, { "GPU", ParticleSimulation::GPU } } )
ME_REFLECT_ENUM( Moonlight::ParticleBlend, { { "Additive", Moonlight::ParticleBlend::Additive }, { "Alpha", Moonlight::ParticleBlend::Alpha } } )
ME_REFLECT_ENUM( Moonlight::ParticleSortCameras, { { "Every", Moonlight::ParticleSortCameras::Every }, { "MainCamera", Moonlight::ParticleSortCameras::MainCamera } } )
ME_REFLECT_ENUM( Moonlight::ParticleAlignment, { { "Billboard", Moonlight::ParticleAlignment::Billboard }, { "Stretched", Moonlight::ParticleAlignment::Stretched }, { "Horizontal", Moonlight::ParticleAlignment::Horizontal } } )

ME_REFLECT_BEGIN( ParticleSystem )
    ME_FIELD( Looping ).Category( "Emission" );
    ME_FIELD( Duration ).Category( "Emission" ).Range( 0.05f, 120.f ).Tooltip( "Seconds a non-looping system emits for" ).Unit( Reflection::Unit::Seconds ).Log();
    ME_FIELD( EmissionRate ).Category( "Emission" ).Range( 0.f, 10000.f ).Tooltip( "Particles per second" ).Unit( Reflection::Unit::Hertz ).Log().SoftRange( 0.f, 500.f );
    ME_FIELD( BurstCount ).Category( "Emission" ).Range( 0.f, 10000.f ).Tooltip( "Particles emitted at once when the system starts (and each loop)" ).Log().SoftRange( 0.f, 500.f );
    ME_FIELD( MaxParticles ).Category( "Emission" ).Range( 1.f, 100000.f ).Log();
    ME_FIELD( SimulationSpace ).Category( "Emission" );
    ME_FIELD( Simulation ).Category( "Emission" ).Tooltip( "Where the particles are stepped. GPU: a compute shader, for many particles (Alpha systems are sorted back to front on the GPU too); the CPU path is used where compute isn't available. When the pool is full a GPU system replaces its oldest particle (CPU skips the new one)" );
    ME_FIELD( Shape ).Category( "Shape" );
    ME_FIELD( ShapeRadius ).Category( "Shape" ).Range( 0.f, 100.f ).Unit( Reflection::Unit::Meters ).Log().SoftRange( 0.f, 10.f );
    ME_FIELD( ConeAngle ).Category( "Shape" ).Range( 0.f, 90.f ).Unit( Reflection::Unit::Degrees );
    ME_FIELD( BoxSize ).Category( "Shape" ).Unit( Reflection::Unit::Meters );
    ME_FIELD( EmitFromShell ).Category( "Shape" ).Tooltip( "Spawn on the surface instead of inside the volume" );
    ME_FIELD( Lifetime ).Category( "Particles" ).Tooltip( "Seconds (min, max)" ).Unit( Reflection::Unit::Seconds );
    ME_FIELD( Speed ).Category( "Particles" ).Tooltip( "m/s (min, max)" ).Unit( Reflection::Unit::MetersPerSecond );
    ME_FIELD( Size ).Category( "Particles" ).Tooltip( "Metres (min, max)" ).Unit( Reflection::Unit::Meters );
    ME_FIELD( Rotation ).Category( "Particles" ).Tooltip( "Degrees (min, max)" ).Unit( Reflection::Unit::Degrees );
    ME_FIELD( AngularVelocity ).Category( "Particles" ).Tooltip( "Degrees per second (min, max)" ).Unit( Reflection::Unit::DegreesPerSecond );
    ME_FIELD( StartColor ).Category( "Particles" ).Color();
    ME_FIELD( StartColor2 ).Category( "Particles" ).Color().Tooltip( "Each particle picks a colour between the two" );
    ME_FIELD( StartAlpha ).Category( "Particles" ).Range( 0.f, 1.f ).Unit( Reflection::Unit::Percent );
    ME_FIELD( Intensity ).Category( "Particles" ).Range( 0.f, 50.f ).Tooltip( "HDR brightness (bloom picks up values above 1)" ).Unit( Reflection::Unit::Multiplier ).Log().SoftRange( 0.f, 10.f );
    ME_FIELD( EndSize ).Category( "Over Lifetime" ).Range( 0.f, 10.f ).Tooltip( "Size multiplier at the end of life" ).Unit( Reflection::Unit::Multiplier );
    ME_FIELD( EndColor ).Category( "Over Lifetime" ).Color();
    ME_FIELD( EndAlpha ).Category( "Over Lifetime" ).Range( 0.f, 1.f ).Unit( Reflection::Unit::Percent );
    ME_FIELD( FadeIn ).Category( "Over Lifetime" ).Range( 0.f, 1.f ).Tooltip( "Fraction of life spent fading in" ).Unit( Reflection::Unit::Percent );
    ME_FIELD( Gravity ).Category( "Over Lifetime" ).Range( -10.f, 10.f ).Tooltip( "Multiples of 9.81 m/s^2; negative rises" ).Unit( Reflection::Unit::Multiplier );
    ME_FIELD( Drag ).Category( "Over Lifetime" ).Range( 0.f, 20.f ).Log().SoftRange( 0.f, 5.f );
    ME_FIELD( NoiseStrength ).Category( "Over Lifetime" ).Range( 0.f, 50.f ).Log().SoftRange( 0.f, 5.f );
    ME_FIELD( NoiseFrequency ).Category( "Over Lifetime" ).Range( 0.01f, 20.f ).Unit( Reflection::Unit::PerMeter ).Log();
    ME_FIELD( Texture ).Category( "Rendering" ).Asset( "Texture" );
    ME_FIELD( Blend ).Category( "Rendering" );
    ME_FIELD( GpuSortCameras ).Category( "Rendering" ).Display( "GPU Sort Cameras" ).Tooltip( "Alpha with Simulation GPU: which cameras sort the particles back to front. Every: each camera, for its own view. MainCamera: render texture cameras (screen feeds) draw with the main camera's latest order, which saves their sort but can show order mistakes from their angle" );
    ME_FIELD( Alignment ).Category( "Rendering" );
    ME_FIELD( StretchFactor ).Category( "Rendering" ).Range( 0.f, 5.f ).Tooltip( "Stretched: extra length per m/s" );
    ME_FIELD( FlipbookColumns ).Category( "Rendering" ).Range( 1.f, 64.f );
    ME_FIELD( FlipbookRows ).Category( "Rendering" ).Range( 1.f, 64.f );
    ME_FIELD( FlipbookFps ).Category( "Rendering" ).Range( 0.f, 120.f ).Tooltip( "0 plays the flipbook once over each particle's life" );
    ME_FIELD( SoftParticleDistance ).Category( "Rendering" ).Range( 0.f, 10.f ).Tooltip( "Fade where particles meet geometry (0 = hard edges)" ).Unit( Reflection::Unit::Meters ).SoftRange( 0.f, 2.f );
    ME_FIELD( Graph ).Category( "Rendering" ).Asset( "ShaderGraph" ).Tooltip( "A Particle shader graph for the quads' colour (it can still read the Texture and the colour over life)" );
    ME_FIELD( GraphParams ).Category( "Rendering" ).Display( "Graph Properties" ).Tooltip( "This system's values for the graph's exposed properties" );
    ME_FIELD( Lit ).Category( "Rendering" ).Tooltip( "Shade with the sun, point / spot lights (and their shadows) and ambient light, like smoke; additive glows usually stay unlit" );
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
    m_stopped = false;
    // GPU path: every slot dies on the next dispatch and the ring starts over.
    m_gpuResetPending = true;
    m_gpuCursor = 0;
    m_gpuHighWater = 0;
    m_gpuLiveEstimate = 0;
    m_gpuSpawns.clear();
}


void ParticleSystem::Play()
{
    m_stopped = false;
    m_time = 0.f;
    m_emitAccumulator = 0.f;
    m_burstDone = false;
}


void ParticleSystem::Stop()
{
    m_stopped = true;
}


void ParticleSystem::Burst( int InCount )
{
    m_pendingBurst += std::max( InCount, 0 );
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
    const std::string& graph = Graph.GetLocalPathString();
    if( graph != m_loadedGraph )
    {
        m_loadedGraph = graph;
        m_graph = graph.empty() ? nullptr : ResourceCache::GetInstance().Get<Moonlight::ShaderGraphAsset>( Moonlight::ShaderGraphAsset::ManifestPathFor( graph ) );
    }
    GraphParams.Asset = m_graph;
    const std::string& path = Texture.GetLocalPathString();
    if( path == m_loadedTexture )
    {
        return;
    }
    m_loadedTexture = path;
    m_texture = ( !path.empty() && Texture.Exists ) ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( Texture ) : nullptr;
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


float ParticleSystem::BeginStep( float InDeltaSeconds )
{
    const float dt = std::clamp( InDeltaSeconds, 0.f, 0.1f );
    m_time += dt;
    return dt;
}


int ParticleSystem::AdvanceEmitter( float InDeltaSeconds )
{
    if( Looping && m_time >= Duration )
    {
        m_time = std::fmod( m_time, std::max( Duration, 0.05f ) );
        m_burstDone = false;
    }
    // Script bursts emit whether or not the system is emitting.
    int count = m_pendingBurst;
    m_pendingBurst = 0;
    if( IsEmitting() )
    {
        if( !m_burstDone )
        {
            count += std::max( BurstCount, 0 );
            m_burstDone = true;
        }
        m_emitAccumulator += EmissionRate * InDeltaSeconds;
        const int rate = static_cast<int>( m_emitAccumulator );
        m_emitAccumulator -= static_cast<float>( rate );
        count += std::max( rate, 0 );
    }
    return count;
}


void ParticleSystem::Simulate( float InDeltaSeconds, const glm::mat4& InWorld )
{
    const float dt = BeginStep( InDeltaSeconds );

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
    const int spawns = AdvanceEmitter( dt );
    for( int i = 0; i < spawns; ++i )
    {
        Emit( InWorld );
    }
}


void ParticleSystem::FillBatchSettings( Moonlight::ParticleBatch& OutBatch ) const
{
    OutBatch.Texture = m_texture ? m_texture->TexHandle : bgfx::TextureHandle( BGFX_INVALID_HANDLE );
    OutBatch.Blend = Blend;
    OutBatch.Alignment = Alignment;
    OutBatch.StretchFactor = StretchFactor;
    OutBatch.Softness = SoftParticleDistance;
    OutBatch.Lit = Lit;
    OutBatch.Graph = m_graph && m_graph->IsValid() && m_graph->GetTarget() == "Particle" ? m_graph : nullptr;
    if( OutBatch.Graph )
    {
        GraphParams.Fill( OutBatch.GraphBinding );
    }
    OutBatch.FlipbookColumns = static_cast<uint16_t>( std::clamp( FlipbookColumns, 1, 64 ) );
    OutBatch.FlipbookRows = static_cast<uint16_t>( std::clamp( FlipbookRows, 1, 64 ) );
}


void ParticleSystem::FillBatch( Moonlight::ParticleBatch& OutBatch, const glm::mat4& InWorld ) const
{
    FillBatchSettings( OutBatch );
    OutBatch.Gpu = nullptr;
    OutBatch.GpuStepPending = false;
    OutBatch.GpuInstanceCount = 0;
    OutBatch.GpuLiveCount = 0;
    OutBatch.GpuRingEnd = 0;
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


void ParticleSystem::UpdateSimulationMode( bool InComputeActive )
{
    const bool gpu = Simulation == ParticleSimulation::GPU && InComputeActive;
    const uint32_t capacity = GpuCapacity();
    if( gpu != m_gpuActive || ( gpu && capacity != m_gpuCapacity ) )
    {
        m_gpuActive = gpu;
        m_gpuCapacity = gpu ? capacity : 0;
        Restart();
    }
    if( !gpu )
    {
        m_gpu.Buffers.reset();
    }
}


void ParticleSystem::EnsureGpuBuffers()
{
    if( !m_gpuActive )
    {
        return;
    }
    if( !m_gpu.Buffers || m_gpu.Buffers->GetCapacity() != m_gpuCapacity )
    {
        m_gpu.Buffers = MakeShared<Moonlight::GpuParticleBuffers>( m_gpuCapacity );
    }
}


float ParticleSystem::GpuMaxLifetime() const
{
    return std::max( std::max( Lifetime.x, Lifetime.y ), 0.01f );
}


uint32_t ParticleSystem::GpuCapacity() const
{
    return static_cast<uint32_t>( std::clamp<int64_t>( MaxParticles, 1, Moonlight::kMaxGpuParticles ) );
}


AABB ParticleSystem::GpuSpawnBounds( const glm::mat4& InWorld ) const
{
    // Where a particle can start, in the emitter's space.
    const float radius = std::abs( ShapeRadius );
    glm::vec3 shapeMin( 0.f );
    glm::vec3 shapeMax( 0.f );
    switch( Shape )
    {
    case ParticleShape::Point:
        break;
    case ParticleShape::Sphere:
        shapeMin = glm::vec3( -radius );
        shapeMax = glm::vec3( radius );
        break;
    case ParticleShape::Hemisphere:
        shapeMin = glm::vec3( -radius, 0.f, -radius );
        shapeMax = glm::vec3( radius );
        break;
    case ParticleShape::Cone:
        shapeMin = glm::vec3( -radius, 0.f, -radius );
        shapeMax = glm::vec3( radius, 0.f, radius );
        break;
    case ParticleShape::Box:
        shapeMax = glm::abs( BoxSize.InternalVector ) * 0.5f;
        shapeMin = -shapeMax;
        break;
    }

    // How far one can travel in its longest life T. Per axis, the speed stays under the start
    // speed plus T times the largest acceleration on that axis (gravity, and the noise: its x and z
    // terms reach 2, y 0.5); drag only slows (a negative drag grows it by exp(-Drag * T)).
    const float maxLife = GpuMaxLifetime();
    const float growth = std::exp( std::max( -Drag, 0.f ) * maxLife );
    const float noise = std::max( NoiseStrength, 0.f );
    const glm::vec3 acceleration( 2.f * noise, 9.81f * std::abs( Gravity ) + 0.5f * noise, 2.f * noise );
    const float startSpeed = std::max( std::abs( Speed.x ), std::abs( Speed.y ) );
    const glm::mat3 basis( InWorld );
    const float scale = std::max( { glm::length( basis[0] ), glm::length( basis[1] ), glm::length( basis[2] ) } );
    const bool local = SimulationSpace == ParticleSpace::Local;
    // World space: the start speed is scaled by the emitter, gravity and noise are not. Local
    // space: everything is in the emitter's units and the whole box is transformed.
    const float speed0 = local ? startSpeed : startSpeed * scale;
    const glm::vec3 travel = ( glm::vec3( speed0 ) + acceleration * maxLife ) * maxLife * growth * 1.01f + glm::vec3( 1e-3f );
    const float worldSpeed = ( speed0 + glm::length( acceleration ) * maxLife ) * growth * ( local ? scale : 1.f );

    const glm::vec3 boxMin = local ? shapeMin - travel : shapeMin;
    const glm::vec3 boxMax = local ? shapeMax + travel : shapeMax;
    AABB bounds;
    for( int corner = 0; corner < 8; ++corner )
    {
        const glm::vec3 point( corner & 1 ? boxMax.x : boxMin.x, corner & 2 ? boxMax.y : boxMin.y, corner & 4 ? boxMax.z : boxMin.z );
        const glm::vec3 world = glm::vec3( InWorld * glm::vec4( point, 1.f ) );
        bounds.Encapsulate( Vector3( world.x, world.y, world.z ) );
    }

    // The quad around the particle: Particle.vert reaches size / sqrt(2) from the centre, a
    // stretched quad its length (size + speed * StretchFactor) behind it plus half its width.
    const float maxSize = std::max( std::abs( Size.x ), std::abs( Size.y ) ) * std::max( 1.f, std::abs( EndSize ) );
    const float extent = Alignment == Moonlight::ParticleAlignment::Stretched ? 1.5f * maxSize + worldSpeed * std::abs( StretchFactor ) : maxSize;
    const glm::vec3 grow = ( local ? glm::vec3( 0.f ) : travel ) + glm::vec3( extent );
    bounds.Min = bounds.Min - Vector3( grow.x, grow.y, grow.z );
    bounds.Max = bounds.Max + Vector3( grow.x, grow.y, grow.z );
    return bounds;
}


void ParticleSystem::StepGpu( float InDeltaSeconds, const glm::mat4& InWorld, Moonlight::ParticleBatch& OutBatch )
{
    const float dt = BeginStep( InDeltaSeconds );
    // The noise phase the particles move with this step (before a looping clock wraps).
    const float time = m_time;

    // Age the spawn history like the GPU ages its particles; a step whose particles must all have
    // died (its age reached the longest lifetime) drops out of the estimate and the bounds.
    const float maxLife = GpuMaxLifetime() * 1.0001f + 1e-4f;
    for( GpuSpawn& spawn : m_gpuSpawns )
    {
        spawn.Age += dt;
    }
    while( !m_gpuSpawns.empty() && m_gpuSpawns.front().Age >= maxLife )
    {
        m_gpuSpawns.pop_front();
    }
    if( m_gpuSpawns.empty() )
    {
        // Every particle is dead: the ring starts over and nothing needs drawing.
        m_gpuCursor = 0;
        m_gpuHighWater = 0;
    }

    // This step's births take the next slots of the ring; when it is full they replace the oldest.
    const int spawns = AdvanceEmitter( dt );
    const uint32_t capacity = std::max( m_gpuCapacity, 1u );
    uint32_t spawnStart = 0;
    uint32_t spawnCount = 0;
    if( spawns > 0 )
    {
        const uint64_t total = static_cast<uint64_t>( spawns );
        spawnCount = static_cast<uint32_t>( std::min<uint64_t>( total, capacity ) );
        spawnStart = static_cast<uint32_t>( ( m_gpuCursor + total - spawnCount ) % capacity );
        m_gpuCursor = static_cast<uint32_t>( ( m_gpuCursor + total ) % capacity );
        m_gpuHighWater = static_cast<uint32_t>( std::min<uint64_t>( static_cast<uint64_t>( m_gpuHighWater ) + total, capacity ) );
        GpuSpawn spawn;
        spawn.Count = spawnCount;
        if( SimulationSpace == ParticleSpace::World )
        {
            spawn.Bounds = GpuSpawnBounds( InWorld );
        }
        m_gpuSpawns.push_back( spawn );
    }

    uint64_t live = 0;
    AABB bounds;
    for( const GpuSpawn& spawn : m_gpuSpawns )
    {
        live += spawn.Count;
        bounds.Encapsulate( spawn.Bounds );
    }
    m_gpuLiveEstimate = static_cast<size_t>( std::min<uint64_t>( live, capacity ) );
    if( SimulationSpace == ParticleSpace::Local && !m_gpuSpawns.empty() )
    {
        // Local particles ride along with the emitter: this step's matrix places all of them.
        bounds = GpuSpawnBounds( InWorld );
    }

    FillBatchSettings( OutBatch );
    OutBatch.Instances.clear();
    OutBatch.Bounds = bounds;
    OutBatch.Gpu = m_gpu.Buffers;
    OutBatch.GpuStepPending = true;
    OutBatch.GpuInstanceCount = m_gpuHighWater;
    OutBatch.GpuLiveCount = static_cast<uint32_t>( m_gpuLiveEstimate );
    // Every live particle was born within the last GpuLiveCount slots before the cursor.
    OutBatch.GpuRingEnd = m_gpuCursor;
    OutBatch.GpuSortCameras = GpuSortCameras;

    Moonlight::GpuParticleStep& step = OutBatch.GpuStep;
    step.World = InWorld;
    step.DeltaSeconds = dt;
    step.Time = time;
    step.SpawnStart = spawnStart;
    step.SpawnCount = spawnCount;
    step.Seed = ( ++m_gpuStepIndex * 2654435761u ) >> 8;
    step.Reset = m_gpuResetPending;
    m_gpuResetPending = false;
    step.Local = SimulationSpace == ParticleSpace::Local;
    step.EmitFromShell = EmitFromShell;
    step.Shape = static_cast<uint8_t>( Shape );
    step.ShapeRadius = ShapeRadius;
    step.ConeAngle = glm::radians( ConeAngle );
    step.BoxSize = BoxSize.InternalVector;
    step.Lifetime = glm::vec2( Lifetime.x, Lifetime.y );
    step.Speed = glm::vec2( Speed.x, Speed.y );
    step.Size = glm::vec2( Size.x, Size.y );
    step.Rotation = glm::radians( glm::vec2( Rotation.x, Rotation.y ) );
    step.AngularVelocity = glm::radians( glm::vec2( AngularVelocity.x, AngularVelocity.y ) );
    step.StartColor = StartColor.InternalVector;
    step.StartColor2 = StartColor2.InternalVector;
    step.EndColor = EndColor.InternalVector;
    step.StartAlpha = StartAlpha;
    step.EndAlpha = EndAlpha;
    step.Intensity = Intensity;
    step.Gravity = Gravity;
    step.Drag = Drag;
    step.NoiseStrength = NoiseStrength;
    step.NoiseFrequency = NoiseFrequency;
    step.EndSize = EndSize;
    step.FadeIn = FadeIn;
    step.FlipbookFps = FlipbookFps;
    step.FlipbookFrames = static_cast<float>( OutBatch.FlipbookColumns * OutBatch.FlipbookRows );
}


#if USING( ME_EDITOR )
void ParticleSystem::OnEditorInspect()
{
    // Keeps the Graph Properties above in step with Graph even when nothing renders the system.
    ResolveTexture();
    HavanaUtils::Label( "Particles" );
    if( m_gpuActive )
    {
        ImGui::Text( "~%zu / %d (GPU, estimated)", GetParticleCount(), MaxParticles );
    }
    else
    {
        ImGui::Text( "%zu / %d", GetParticleCount(), MaxParticles );
    }
    ImGui::SameLine();
    if( ImGui::Button( "Restart" ) )
    {
        Restart();
    }
    if( Simulation == ParticleSimulation::GPU && !m_gpuActive )
    {
        ImGui::TextWrapped( "Simulated on the CPU: compute shaders are unavailable here (the backend, the Compute feature cap or a headless renderer)." );
    }
    else if( m_gpuActive && Blend == Moonlight::ParticleBlend::Alpha )
    {
        ImGui::TextWrapped( GpuSortCameras == Moonlight::ParticleSortCameras::MainCamera
            ? "Sorted back to front on the GPU by the main camera; render texture cameras reuse its order."
            : "Sorted back to front on the GPU for every camera that draws it." );
    }
}
#endif
