#include "GpuParticles.h"
#include "Utils/BGFXUtils.h"
#include "CLog.h"
#include <algorithm>
#include <string>

namespace
{
    // Four vec4s (64 bytes): ParticleInstance and the shader's i_data0..3 (Particle.var), also used
    // for the state records. Compute buffers stay vec4-only for D3D11 (RGBA32F typed buffers).
    const bgfx::VertexLayout& ParticleRecordLayout()
    {
        static const bgfx::VertexLayout layout = []() {
            bgfx::VertexLayout result;
            result.begin()
                .add( bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float )
                .end();
            return result;
        }();
        return layout;
    }
}


Moonlight::GpuParticleBuffers::GpuParticleBuffers( uint32_t InCapacity )
    : m_capacity( std::clamp<uint32_t>( InCapacity, 1u, kMaxGpuParticles ) )
{
    const bgfx::VertexLayout& layout = ParticleRecordLayout();
    BX_ASSERT( layout.getStride() == 64, "particle records are four vec4s" );
    m_state = bgfx::createDynamicVertexBuffer( m_capacity, layout, BGFX_BUFFER_COMPUTE_READ_WRITE );
    m_instances = bgfx::createDynamicVertexBuffer( m_capacity, layout, BGFX_BUFFER_COMPUTE_READ_WRITE );
}


Moonlight::GpuParticleBuffers::~GpuParticleBuffers()
{
    if( !IsGpuAlive() )
    {
        return;
    }
    if( bgfx::isValid( m_state ) )
    {
        bgfx::destroy( m_state );
    }
    if( bgfx::isValid( m_instances ) )
    {
        bgfx::destroy( m_instances );
    }
}


Moonlight::GpuParticleSimulator::GpuParticleSimulator()
{
    m_program = AcquireComputeProgram( kShader );
    u_particleSim = bgfx::createUniform( "u_particleSim", bgfx::UniformType::Vec4, kParamCount );
    u_particleWorld = bgfx::createUniform( "u_particleWorld", bgfx::UniformType::Mat4 );
}


Moonlight::GpuParticleSimulator::~GpuParticleSimulator()
{
    if( !IsGpuAlive() )
    {
        return;
    }
    if( bgfx::isValid( u_particleSim ) )
    {
        bgfx::destroy( u_particleSim );
    }
    if( bgfx::isValid( u_particleWorld ) )
    {
        bgfx::destroy( u_particleWorld );
    }
}


bool Moonlight::GpuParticleSimulator::IsReady() const
{
    return m_program && bgfx::isValid( m_program->Handle );
}


void Moonlight::PackGpuParticleStep( const GpuParticleStep& InStep, glm::vec4 OutParams[GpuParticleSimulator::kParamCount] )
{
    // Keep in step with the #defines at the top of ParticleSim.comp.
    OutParams[0] = glm::vec4( InStep.DeltaSeconds, InStep.Time, static_cast<float>( InStep.SpawnStart ), static_cast<float>( InStep.SpawnCount ) );
    OutParams[1] = glm::vec4( 0.f, InStep.Reset ? 1.f : 0.f, static_cast<float>( InStep.Seed & 0xffffffu ), static_cast<float>( InStep.Shape ) );
    OutParams[2] = glm::vec4( InStep.ShapeRadius, InStep.ConeAngle, InStep.EmitFromShell ? 1.f : 0.f, InStep.Local ? 1.f : 0.f );
    OutParams[3] = glm::vec4( InStep.BoxSize, InStep.Gravity );
    OutParams[4] = glm::vec4( InStep.Lifetime, InStep.Speed );
    OutParams[5] = glm::vec4( InStep.Size, InStep.Rotation );
    OutParams[6] = glm::vec4( InStep.AngularVelocity, InStep.Drag, InStep.NoiseStrength );
    OutParams[7] = glm::vec4( InStep.StartColor, InStep.StartAlpha );
    OutParams[8] = glm::vec4( InStep.StartColor2, InStep.Intensity );
    OutParams[9] = glm::vec4( InStep.EndColor, InStep.EndAlpha );
    OutParams[10] = glm::vec4( InStep.EndSize, InStep.FadeIn, InStep.FlipbookFps, InStep.FlipbookFrames );
    OutParams[11] = glm::vec4( InStep.NoiseFrequency, 0.f, 0.f, 0.f );
}


bool Moonlight::GpuParticleSimulator::Dispatch( bgfx::ViewId InView, GpuParticleBuffers& InBuffers, const GpuParticleStep& InStep )
{
    if( InStep.Reset )
    {
        InBuffers.NeedsReset = true;
    }
    if( !IsReady() || !InBuffers.IsValid() )
    {
        return false;
    }
    GpuParticleStep step = InStep;
    step.Reset = InBuffers.NeedsReset;

    glm::vec4 params[kParamCount];
    PackGpuParticleStep( step, params );
    params[1].x = static_cast<float>( InBuffers.GetCapacity() );
    bgfx::setUniform( u_particleSim, params, kParamCount );
    bgfx::setUniform( u_particleWorld, &step.World[0][0] );
    bgfx::setBuffer( 0, InBuffers.GetState(), bgfx::Access::ReadWrite );
    bgfx::setBuffer( 1, InBuffers.GetInstances(), bgfx::Access::Write );
    bgfx::dispatch( InView, m_program->Handle, ComputeGroupCount( InBuffers.GetCapacity(), kGpuParticleGroupSize ), 1, 1 );

    InBuffers.NeedsReset = false;
    InBuffers.Initialized = true;
    if( !m_loggedFirstDispatch )
    {
        m_loggedFirstDispatch = true;
        CLog::Log( CLog::LogType::Info, "GPU particles: first compute dispatch (" + std::to_string( InBuffers.GetCapacity() ) + " slots)" );
    }
    return true;
}
