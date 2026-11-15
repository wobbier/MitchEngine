#include "TextureHistogram.h"
#include "Utils/BGFXUtils.h"
#include <algorithm>

namespace
{
    // One vec4 per element: compute buffers stay vec4-only for D3D11.
    const bgfx::VertexLayout& HistogramLayout()
    {
        static const bgfx::VertexLayout layout = []() {
            bgfx::VertexLayout result;
            result.begin().add( bgfx::Attrib::Position, 4, bgfx::AttribType::Float ).end();
            return result;
        }();
        return layout;
    }

    constexpr const char* kBinShader = "Assets/Shaders/Post/HistogramBin.comp";
    constexpr const char* kMergeShader = "Assets/Shaders/Post/HistogramMerge.comp";
}


Moonlight::TextureHistogram::TextureHistogram()
{
    m_binProgram = AcquireComputeProgram( kBinShader );
    m_mergeProgram = AcquireComputeProgram( kMergeShader );
    const uint32_t vec4sPerHistogram = kBins / 4;
    m_groups = bgfx::createDynamicVertexBuffer( kMaxGroups * vec4sPerHistogram, HistogramLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
    m_merged = bgfx::createDynamicVertexBuffer( vec4sPerHistogram, HistogramLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
    s_histSource = bgfx::createUniform( "s_histSource", bgfx::UniformType::Sampler );
    u_histGrid = bgfx::createUniform( "u_histGrid", bgfx::UniformType::Vec4 );
    u_histWeights = bgfx::createUniform( "u_histWeights", bgfx::UniformType::Vec4 );
    u_histRange = bgfx::createUniform( "u_histRange", bgfx::UniformType::Vec4 );
    u_histParams = bgfx::createUniform( "u_histParams", bgfx::UniformType::Vec4 );
}


Moonlight::TextureHistogram::~TextureHistogram()
{
    if( !IsGpuAlive() )
    {
        return;
    }
    if( bgfx::isValid( m_groups ) )
    {
        bgfx::destroy( m_groups );
    }
    if( bgfx::isValid( m_merged ) )
    {
        bgfx::destroy( m_merged );
    }
    for( bgfx::UniformHandle uniform : { s_histSource, u_histGrid, u_histWeights, u_histRange, u_histParams } )
    {
        if( bgfx::isValid( uniform ) )
        {
            bgfx::destroy( uniform );
        }
    }
}


bool Moonlight::TextureHistogram::IsReady() const
{
    return m_binProgram && m_mergeProgram && bgfx::isValid( m_binProgram->Handle ) && bgfx::isValid( m_mergeProgram->Handle ) && bgfx::isValid( m_groups ) && bgfx::isValid( m_merged );
}


void Moonlight::TextureHistogram::SetRangeUniform( const Params& InParams ) const
{
    const float range = std::max( InParams.Max - InParams.Min, 0.0001f );
    const float values[4] = { InParams.Min, InParams.Max, 1.f / range, 0.f };
    bgfx::setUniform( u_histRange, values );
}


bool Moonlight::TextureHistogram::Dispatch( bgfx::ViewId InBinView, bgfx::ViewId InMergeView, bgfx::TextureHandle InSource, const Params& InParams )
{
    if( !IsReady() || !bgfx::isValid( InSource ) )
    {
        return false;
    }
    const uint32_t groupsX = std::clamp<uint32_t>( InParams.GroupsX, 1u, kMaxGroups );
    const uint32_t groupsY = std::clamp<uint32_t>( InParams.GroupsY, 1u, kMaxGroups / groupsX );
    const uint32_t groups = groupsX * groupsY;

    const float grid[4] = { static_cast<float>( groupsX * kGroupSize ), static_cast<float>( groupsY * kGroupSize ), 0.f, 0.f };
    const float params[4] = { static_cast<float>( kBins ), static_cast<float>( groupsX ), 0.f, InParams.Logarithmic ? 1.f : 0.f };
    bgfx::setUniform( u_histGrid, grid );
    bgfx::setUniform( u_histWeights, &InParams.Weights[0] );
    SetRangeUniform( InParams );
    bgfx::setUniform( u_histParams, params );
    bgfx::setTexture( 0, s_histSource, InSource, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
    bgfx::setBuffer( 1, m_groups, bgfx::Access::Write );
    bgfx::dispatch( InBinView, m_binProgram->Handle, groupsX, groupsY, 1 );

    const float mergeParams[4] = { static_cast<float>( groups ), 0.f, 0.f, 0.f };
    bgfx::setUniform( u_histParams, mergeParams );
    bgfx::setBuffer( 0, m_groups, bgfx::Access::Read );
    bgfx::setBuffer( 1, m_merged, bgfx::Access::Write );
    bgfx::dispatch( InMergeView, m_mergeProgram->Handle, 1, 1, 1 );
    return true;
}
