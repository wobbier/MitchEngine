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

    // One vec4 per alpha sort entry (key, slot, 0, 0).
    const bgfx::VertexLayout& SortEntryLayout()
    {
        static const bgfx::VertexLayout layout = []() {
            bgfx::VertexLayout result;
            result.begin().add( bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float ).end();
            return result;
        }();
        return layout;
    }

    // Sort targets no camera drew for this many frames are dropped (screen-feed cameras in stepped
    // looks render every few frames, so keep it generous).
    constexpr uint64_t kSortTargetIdleFrames = 600;
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


Moonlight::GpuParticleSortTarget* Moonlight::GpuParticleBuffers::FindSortTarget( const void* InCamera )
{
    for( const UniquePtr<GpuParticleSortTarget>& target : m_sortTargets )
    {
        if( target->Camera == InCamera )
        {
            return target.get();
        }
    }
    return nullptr;
}


Moonlight::GpuParticleSortTarget* Moonlight::GpuParticleBuffers::AcquireSortTarget( const void* InCamera, uint64_t InFrame )
{
    // Drop the copies of cameras that stopped drawing this system (or went away).
    m_sortTargets.erase( std::remove_if( m_sortTargets.begin(), m_sortTargets.end(), [&]( const UniquePtr<GpuParticleSortTarget>& target ) {
        return target->Camera != InCamera && InFrame > target->LastUsedFrame + kSortTargetIdleFrames;
    } ), m_sortTargets.end() );
    if( MainOrderCamera && !FindSortTarget( MainOrderCamera ) )
    {
        MainOrderCamera = nullptr;
    }

    GpuParticleSortTarget* target = FindSortTarget( InCamera );
    if( !target )
    {
        UniquePtr<GpuParticleSortTarget> created = MakeUnique<GpuParticleSortTarget>( InCamera, m_capacity );
        if( !created->IsValid() )
        {
            return nullptr;
        }
        target = created.get();
        m_sortTargets.push_back( std::move( created ) );
    }
    target->LastUsedFrame = InFrame;
    return target;
}


Moonlight::GpuParticleSortTarget::GpuParticleSortTarget( const void* InCamera, uint32_t InCapacity )
    : Camera( InCamera )
    , m_capacity( InCapacity )
    , m_entryCapacity( GpuSortEntryCount( InCapacity ) )
{
    m_instances = bgfx::createDynamicVertexBuffer( m_capacity, ParticleRecordLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
    // Written by the gather pass; only drawn from where the backend has BGFX_CAPS_DRAW_INDIRECT
    // (the gather shader binds it either way).
    m_drawArgs = bgfx::createIndirectBuffer( 1 );
}


Moonlight::GpuParticleSortTarget::~GpuParticleSortTarget()
{
    if( !IsGpuAlive() )
    {
        return;
    }
    if( bgfx::isValid( m_entries ) )
    {
        bgfx::destroy( m_entries );
    }
    if( bgfx::isValid( m_instances ) )
    {
        bgfx::destroy( m_instances );
    }
    if( bgfx::isValid( m_drawArgs ) )
    {
        bgfx::destroy( m_drawArgs );
    }
}


bool Moonlight::GpuParticleSortTarget::EnsureEntries()
{
    if( !bgfx::isValid( m_entries ) )
    {
        m_entries = bgfx::createDynamicVertexBuffer( m_entryCapacity, SortEntryLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
    }
    return bgfx::isValid( m_entries ) && bgfx::isValid( m_drawArgs );
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


uint32_t Moonlight::GpuSortEntryCount( uint32_t InCount )
{
    uint32_t entries = kGpuParticleSortBlock;
    while( entries < InCount )
    {
        entries <<= 1;
    }
    return entries;
}


void Moonlight::BuildGpuSortPasses( uint32_t InEntries, std::vector<GpuSortPass>& OutPasses )
{
    // for h = 2 .. N: flip(h), disperse(h/2 .. 2) (ParticleSort.sh). Every step up to the block
    // size runs in the first local pass; past it, a merge is a global flip, global disperse steps
    // while their blocks are larger than one group's, and a local pass for the rest.
    OutPasses.clear();
    OutPasses.push_back( { GpuSortPass::Kind::LocalSort, kGpuParticleSortBlock } );
    for( uint32_t h = kGpuParticleSortBlock * 2; h <= InEntries; h <<= 1 )
    {
        OutPasses.push_back( { GpuSortPass::Kind::Flip, h } );
        for( uint32_t d = h / 2; d > kGpuParticleSortBlock; d >>= 1 )
        {
            OutPasses.push_back( { GpuSortPass::Kind::Disperse, d } );
        }
        OutPasses.push_back( { GpuSortPass::Kind::LocalMerge, kGpuParticleSortBlock } );
    }
}


Moonlight::GpuSortPlan Moonlight::PlanGpuSort( const GpuSortInputs& InInputs )
{
    GpuSortPlan plan;
    plan.Sort = true;
    if( !InInputs.ReuseAllowed || !InInputs.HasSource || InInputs.Reset || InInputs.SourceFrame > InInputs.Frame
        || InInputs.SourceCount > InInputs.Capacity || InInputs.Capacity == 0 )
    {
        return plan;
    }
    const uint32_t capacity = InInputs.Capacity;
    const uint32_t appended = ( InInputs.RingEnd % capacity + capacity - InInputs.SourceRingEnd % capacity ) % capacity;
    const uint64_t age = InInputs.Frame - InInputs.SourceFrame;
    bool follows = false;
    if( age == 0 )
    {
        // The main camera sorted this frame already: its order has this frame's births.
        follows = appended == 0;
    }
    else if( age == 1 )
    {
        // Last frame's order: this frame's births start where it ended and are all there is.
        follows = appended == InInputs.SpawnCount
            && ( InInputs.SpawnCount == 0 || InInputs.SpawnStart % capacity == InInputs.SourceRingEnd % capacity );
    }
    if( !follows || static_cast<uint64_t>( InInputs.SourceCount ) + appended > capacity )
    {
        return plan;
    }
    plan.Sort = false;
    plan.Reuse = true;
    plan.AppendCount = appended;
    return plan;
}


Moonlight::GpuParticleSorter::GpuParticleSorter()
{
    m_local = AcquireComputeProgram( kLocalShader );
    m_global = AcquireComputeProgram( kGlobalShader );
    m_gather = AcquireComputeProgram( kGatherShader );
    u_particleSort = bgfx::createUniform( "u_particleSort", bgfx::UniformType::Vec4, kParamCount );
    m_drawIndirect = ( bgfx::getCaps()->supported & BGFX_CAPS_DRAW_INDIRECT ) != 0;
    CLog::Log( CLog::LogType::Info, std::string( "GPU particles: alpha sort draws " ) + ( m_drawIndirect ? "indirect (dead particles cut on the GPU)" : "the live estimate (no draw indirect; dead particles are degenerate)" ) );
}


Moonlight::GpuParticleSorter::~GpuParticleSorter()
{
    if( IsGpuAlive() && bgfx::isValid( u_particleSort ) )
    {
        bgfx::destroy( u_particleSort );
    }
}


bool Moonlight::GpuParticleSorter::IsReady() const
{
    return m_local && bgfx::isValid( m_local->Handle ) && m_global && bgfx::isValid( m_global->Handle ) && m_gather && bgfx::isValid( m_gather->Handle );
}


void Moonlight::GpuParticleSorter::Gather( bgfx::ViewId InView, GpuParticleBuffers& InBuffers, GpuParticleSortTarget& InTarget, const GpuParticleSortTarget& InOrder,
    uint32_t InOrderCount, uint32_t InAppendStart, uint32_t InAppendCount, bool InWriteArgs )
{
    const uint32_t drawCount = InOrderCount + InAppendCount;
    glm::vec4 params[kParamCount] = {
        glm::vec4( static_cast<float>( InBuffers.GetCapacity() ), static_cast<float>( drawCount ), static_cast<float>( InAppendStart ), static_cast<float>( InOrderCount ) ),
        glm::vec4( 0.f ),
        glm::vec4( 0.f, 0.f, 0.f, InWriteArgs ? 1.f : 0.f ) };
    bgfx::setUniform( u_particleSort, params, kParamCount );
    bgfx::setBuffer( 0, InOrder.GetEntries(), bgfx::Access::Read );
    bgfx::setBuffer( 1, InBuffers.GetInstances(), bgfx::Access::Read );
    bgfx::setBuffer( 2, InTarget.GetInstances(), bgfx::Access::Write );
    bgfx::setBuffer( 3, InTarget.GetDrawArgs(), bgfx::Access::Write );
    bgfx::dispatch( InView, m_gather->Handle, ComputeGroupCount( drawCount, kGpuParticleGroupSize ), 1, 1 );
    InTarget.DrawCount = drawCount;
}


Moonlight::GpuParticleSortTarget* Moonlight::GpuParticleSorter::Sort( bgfx::ViewId InView, GpuParticleBuffers& InBuffers, const Request& InRequest )
{
    const uint32_t capacity = InBuffers.GetCapacity();
    const uint32_t count = std::min( InRequest.Count, capacity );
    if( !IsReady() || !InBuffers.IsValid() || !InBuffers.Initialized || count == 0 )
    {
        return nullptr;
    }
    GpuParticleSortTarget* target = InBuffers.AcquireSortTarget( InRequest.Camera, InRequest.Frame );
    if( !target )
    {
        return nullptr;
    }
    target->Ready = false;

    // A render texture camera may draw with the main camera's order (one sort for the system).
    const GpuParticleSortTarget* source = nullptr;
    if( InRequest.ExtraCamera && InRequest.ReuseMainOrder && InBuffers.MainOrderCamera && InBuffers.MainOrderCamera != InRequest.Camera )
    {
        source = InBuffers.FindSortTarget( InBuffers.MainOrderCamera );
    }
    GpuSortInputs inputs;
    inputs.ReuseAllowed = InRequest.ExtraCamera && InRequest.ReuseMainOrder;
    inputs.HasSource = source && source->HasOrder && source->HasEntries();
    inputs.Frame = InRequest.Frame;
    if( inputs.HasSource )
    {
        inputs.SourceFrame = source->OrderFrame;
        inputs.SourceCount = source->OrderCount;
        inputs.SourceRingEnd = source->OrderRingEnd;
    }
    inputs.Capacity = capacity;
    inputs.RingEnd = InRequest.RingEnd;
    inputs.SpawnStart = InRequest.SpawnStart;
    inputs.SpawnCount = InRequest.SpawnCount;
    inputs.Reset = InRequest.Reset;
    const GpuSortPlan plan = PlanGpuSort( inputs );
    if( plan.Reuse )
    {
        Gather( InView, InBuffers, *target, *source, source->OrderCount, source->OrderRingEnd, plan.AppendCount, false );
        target->DrawIndirect = false;
        target->Ready = true;
        return target;
    }

    if( !target->EnsureEntries() )
    {
        return nullptr;
    }
    const uint32_t entries = GpuSortEntryCount( count );
    BX_ASSERT( entries <= target->GetEntryCapacity(), "sort entries exceed the target's buffer" );
    BuildGpuSortPasses( entries, m_passes );
    glm::vec4 params[kParamCount] = {
        glm::vec4( static_cast<float>( capacity ), static_cast<float>( count ), static_cast<float>( InRequest.RingEnd % capacity ), static_cast<float>( entries ) ),
        glm::vec4( InRequest.ViewDepth, 0.f ),
        glm::vec4( 0.f ) };
    for( const GpuSortPass& pass : m_passes )
    {
        const bool local = pass.Type == GpuSortPass::Kind::LocalSort || pass.Type == GpuSortPass::Kind::LocalMerge;
        params[2] = glm::vec4( static_cast<float>( pass.Size ), pass.Type == GpuSortPass::Kind::LocalSort ? 1.f : 0.f, pass.Type == GpuSortPass::Kind::Flip ? 1.f : 0.f, 0.f );
        bgfx::setUniform( u_particleSort, params, kParamCount );
        bgfx::setBuffer( 0, target->GetEntries(), bgfx::Access::ReadWrite );
        if( local )
        {
            // Bound for the merge too: the program declares them.
            bgfx::setBuffer( 1, InBuffers.GetState(), bgfx::Access::Read );
            bgfx::setBuffer( 2, InBuffers.GetInstances(), bgfx::Access::Read );
            bgfx::dispatch( InView, m_local->Handle, entries / kGpuParticleSortBlock, 1, 1 );
        }
        else
        {
            bgfx::dispatch( InView, m_global->Handle, entries / kGpuParticleSortBlock, 1, 1 );
        }
    }
    Gather( InView, InBuffers, *target, *target, count, 0, 0, m_drawIndirect );

    target->HasOrder = true;
    target->OrderFrame = InRequest.Frame;
    target->OrderCount = count;
    target->OrderRingEnd = InRequest.RingEnd % capacity;
    target->DrawIndirect = m_drawIndirect;
    target->Ready = true;
    if( !InRequest.ExtraCamera )
    {
        InBuffers.MainOrderCamera = InRequest.Camera;
    }
    if( !m_loggedFirstSort )
    {
        m_loggedFirstSort = true;
        CLog::Log( CLog::LogType::Info, "GPU particles: first alpha sort (" + std::to_string( count ) + " slots, " + std::to_string( entries ) + " entries, "
            + std::to_string( m_passes.size() + 1 ) + " dispatches)" );
    }
    return target;
}
