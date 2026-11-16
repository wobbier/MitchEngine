#include "GpuMeshCulling.h"
#include "Utils/BGFXUtils.h"
#include "CLog.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>

namespace
{
    constexpr const char* kCountShader = "Assets/Shaders/Culling/GpuCullCount.comp";
    constexpr const char* kOffsetsShader = "Assets/Shaders/Culling/GpuCullOffsets.comp";
    constexpr const char* kWriteShader = "Assets/Shaders/Culling/GpuCullWrite.comp";
    constexpr const char* kScatterShader = "Assets/Shaders/Culling/GpuCullScatter.comp";
    constexpr const char* kOcclusionCountShader = "Assets/Shaders/Culling/GpuCullOcclusionCount.comp";
    constexpr const char* kOcclusionOffsetsShader = "Assets/Shaders/Culling/GpuCullOcclusionOffsets.comp";
    constexpr const char* kOcclusionWriteShader = "Assets/Shaders/Culling/GpuCullOcclusionWrite.comp";
    constexpr const char* kPyramidShader = "Assets/Shaders/Culling/GpuCullDepthPyramid.comp";
    // Stages the occlusion passes add (GpuCullOcclusion.sh): the member state, then the pyramid's textures.
    constexpr uint8_t kOcclusionStateStage = 11;
    constexpr uint8_t kOcclusionPyramidStage = 12;
    // A pyramid not built for this many frames goes (its camera went, or stopped occlusion culling).
    constexpr uint64_t kHiZHistoryFrames = 300;
    // Point sampling, clamped: the pyramid and depth reads fetch whole texels.
    constexpr uint32_t kPointClamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
    // NUM_THREADS of GpuCullScatter.comp.
    constexpr uint32_t kSmallGroup = 64;
    // u_gpuCull: three vec4s of counts, then six planes per view.
    constexpr uint32_t kCullParams = 3 + 6 * Moonlight::kGpuCullMaxViews;

    // One vec4 per element: records, staging rows, the group and batch tables, scratch.
    const bgfx::VertexLayout& Vec4Layout()
    {
        static const bgfx::VertexLayout layout = []() {
            bgfx::VertexLayout result;
            result.begin().add( bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float ).end();
            return result;
        }();
        return layout;
    }

    // Four vec4s (a 64-byte matrix) per element: the culled instances the draws read as i_data0..3.
    const bgfx::VertexLayout& InstanceLayout()
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

    template<typename THandle>
    void DestroyHandle( THandle& InOutHandle )
    {
        if( bgfx::isValid( InOutHandle ) )
        {
            bgfx::destroy( InOutHandle );
        }
        InOutHandle = BGFX_INVALID_HANDLE;
    }

    // Grows a buffer's capacity by half again (at least to InNeeded).
    uint32_t GrownCapacity( uint32_t InNeeded )
    {
        return std::max<uint32_t>( 64u, InNeeded + InNeeded / 2 );
    }
}


void Moonlight::BuildGpuCullSet( const MeshBatchRegistry& InRegistry, uint32_t InBatchBase, GpuCullSetLayout& OutSet, std::vector<uint32_t>& InOutMembers, std::vector<uint32_t>& InOutGroupBatch, uint32_t InMinMembers )
{
    OutSet.MemberBase = static_cast<uint32_t>( InOutMembers.size() );
    OutSet.GroupBase = static_cast<uint32_t>( InOutGroupBatch.size() );
    OutSet.BatchBase = InBatchBase;
    OutSet.Batches.clear();

    // Members by batch, batches in order of their lowest member.
    std::vector<uint32_t> slotOf( InRegistry.GetBatchCapacity(), UINT32_MAX );
    std::vector<std::vector<uint32_t>> members;
    const uint32_t commands = InRegistry.GetCommandCapacity();
    for( uint32_t id = 0; id < commands; ++id )
    {
        const uint32_t batch = InRegistry.GetBatch( id );
        if( batch == MeshBatchRegistry::kNone || InRegistry.GetMemberCount( batch ) < InMinMembers )
        {
            continue;
        }
        if( slotOf[batch] == UINT32_MAX )
        {
            slotOf[batch] = static_cast<uint32_t>( members.size() );
            members.emplace_back();
            GpuCullBatch entry;
            entry.Batch = batch;
            entry.Representative = id;
            OutSet.Batches.push_back( entry );
        }
        members[slotOf[batch]].push_back( id );
    }

    uint32_t memberStart = 0;
    uint32_t groupStart = 0;
    for( size_t b = 0; b < OutSet.Batches.size(); ++b )
    {
        GpuCullBatch& entry = OutSet.Batches[b];
        const std::vector<uint32_t>& ids = members[b];
        entry.MemberStart = memberStart;
        entry.MemberCount = static_cast<uint32_t>( ids.size() );
        entry.GroupStart = groupStart;
        entry.GroupCount = ( entry.MemberCount + kGpuCullGroup - 1 ) / kGpuCullGroup;
        InOutMembers.insert( InOutMembers.end(), ids.begin(), ids.end() );
        InOutMembers.insert( InOutMembers.end(), entry.GroupCount * kGpuCullGroup - entry.MemberCount, kGpuCullNoMember );
        InOutGroupBatch.insert( InOutGroupBatch.end(), entry.GroupCount, static_cast<uint32_t>( b ) );
        memberStart += entry.GroupCount * kGpuCullGroup;
        groupStart += entry.GroupCount;
    }
    OutSet.Groups = groupStart;
    OutSet.Padded = memberStart;
}


std::vector<std::vector<uint32_t>> Moonlight::SimulateGpuCull( const GpuCullSetLayout& InSet, const std::vector<uint32_t>& InMembers, uint32_t InViews, bool InReverse,
    const std::function<bool( uint32_t InId, uint32_t InView )>& InVisible )
{
    const uint32_t batches = InSet.GetBatchCount();
    std::vector<uint32_t> groupBatch( InSet.Groups, 0 );
    for( uint32_t b = 0; b < batches; ++b )
    {
        for( uint32_t g = 0; g < InSet.Batches[b].GroupCount; ++g )
        {
            groupBatch[InSet.Batches[b].GroupStart + g] = b;
        }
    }
    const auto memberAt = [&]( uint32_t InGroup, uint32_t InLocal ) {
        return InMembers[InSet.MemberBase + InGroup * kGpuCullGroup + InLocal];
    };
    const auto visibleAt = [&]( uint32_t InGroup, uint32_t InLocal, uint32_t InView ) {
        const uint32_t id = memberAt( InGroup, InLocal );
        return id != kGpuCullNoMember && InVisible( id, InView );
    };

    // Pass 1: counts per (view, group).
    std::vector<uint32_t> counts( static_cast<size_t>( InViews ) * InSet.Groups, 0 );
    for( uint32_t v = 0; v < InViews; ++v )
    {
        for( uint32_t g = 0; g < InSet.Groups; ++g )
        {
            for( uint32_t l = 0; l < kGpuCullGroup; ++l )
            {
                counts[v * InSet.Groups + g] += visibleAt( g, l, v ) ? 1u : 0u;
            }
        }
    }
    // Pass 2: each group's first slot and each (batch, view)'s total. One workgroup per (batch, view):
    // an inclusive scan of kGpuCullGroup group counts at a time (one per thread), exclusive = inclusive
    // minus the thread's own count plus the carry, and the carry grows by the chunk's last inclusive value.
    std::vector<uint32_t> offsets( counts.size(), 0 );
    std::vector<uint32_t> totals( static_cast<size_t>( InViews ) * batches, 0 );
    for( uint32_t v = 0; v < InViews; ++v )
    {
        for( uint32_t b = 0; b < batches; ++b )
        {
            const uint32_t groupCount = InSet.Batches[b].GroupCount;
            uint32_t carry = 0;
            for( uint32_t chunk = 0; chunk < groupCount; chunk += kGpuCullGroup )
            {
                uint32_t scan[kGpuCullGroup] = {};
                for( uint32_t local = 0; local < kGpuCullGroup; ++local )
                {
                    const uint32_t g = chunk + local;
                    scan[local] = g < groupCount ? counts[v * InSet.Groups + InSet.Batches[b].GroupStart + g] : 0u;
                }
                uint32_t inclusive[kGpuCullGroup];
                uint32_t sum = 0;
                for( uint32_t local = 0; local < kGpuCullGroup; ++local )
                {
                    sum += scan[local];
                    inclusive[local] = sum;
                }
                for( uint32_t local = 0; local < kGpuCullGroup && chunk + local < groupCount; ++local )
                {
                    offsets[v * InSet.Groups + InSet.Batches[b].GroupStart + chunk + local] = carry + inclusive[local] - scan[local];
                }
                carry += inclusive[kGpuCullGroup - 1];
            }
            totals[v * batches + b] = carry;
        }
    }
    // Pass 3: each visible member at its rank.
    std::vector<std::vector<uint32_t>> result( static_cast<size_t>( InViews ) * batches );
    for( size_t i = 0; i < result.size(); ++i )
    {
        result[i].assign( totals[i], kGpuCullNoMember );
    }
    for( uint32_t v = 0; v < InViews; ++v )
    {
        for( uint32_t g = 0; g < InSet.Groups; ++g )
        {
            uint32_t prefix = 0;
            for( uint32_t l = 0; l < kGpuCullGroup; ++l )
            {
                if( !visibleAt( g, l, v ) )
                {
                    continue;
                }
                ++prefix;
                const uint32_t b = groupBatch[g];
                uint32_t rank = offsets[v * InSet.Groups + g] + prefix - 1;
                if( InReverse )
                {
                    rank = totals[v * batches + b] - 1 - rank;
                }
                result[v * batches + b][rank] = memberAt( g, l );
            }
        }
    }
    return result;
}


Moonlight::GpuCullUploadPlan Moonlight::PlanGpuCullUpload( uint32_t InChanged, bool InAllChanged, uint32_t InCommandCount, uint32_t InCapacity )
{
    GpuCullUploadPlan plan;
    plan.Capacity = InCapacity;
    if( InCommandCount > InCapacity )
    {
        plan.Capacity = GrownCapacity( InCommandCount );
        plan.Full = true;
    }
    plan.Full = plan.Full || InAllChanged || ( InCommandCount > 0 && InChanged >= std::max<uint32_t>( 1u, InCommandCount / 4 ) );
    return plan;
}


void Moonlight::PackGpuCullRecord( const glm::mat4& InInstance, const AABB& InBounds, glm::vec4 OutRecord[kGpuCullRecordVec4s] )
{
    OutRecord[0] = InInstance[0];
    OutRecord[1] = InInstance[1];
    OutRecord[2] = InInstance[2];
    OutRecord[3] = InInstance[3];
    const bool valid = InBounds.IsValid();
    OutRecord[4] = glm::vec4( valid ? InBounds.Min.InternalVector : glm::vec3( 0.f ), valid ? 1.f : 0.f );
    OutRecord[5] = glm::vec4( valid ? InBounds.Max.InternalVector : glm::vec3( 0.f ), 0.f );
}


Moonlight::HiZLayout Moonlight::MakeHiZLayout( uint32_t InDepthWidth, uint32_t InDepthHeight )
{
    HiZLayout layout;
    if( InDepthWidth == 0 || InDepthHeight == 0 )
    {
        return layout;
    }
    const auto previousPowerOfTwo = []( uint32_t InValue ) {
        uint32_t result = 1;
        while( result <= InValue / 2 )
        {
            result *= 2;
        }
        return result;
    };
    layout.SourceWidth = InDepthWidth;
    layout.SourceHeight = InDepthHeight;
    layout.Width = previousPowerOfTwo( InDepthWidth );
    layout.Height = previousPowerOfTwo( InDepthHeight );
    layout.Mips = 1;
    while( ( std::max( layout.Width, layout.Height ) >> ( layout.Mips - 1 ) ) > 1 )
    {
        ++layout.Mips;
    }
    return layout;
}


std::vector<std::vector<float>> Moonlight::BuildHiZ( const HiZLayout& InLayout, const std::vector<float>& InDepth )
{
    std::vector<std::vector<float>> mips( InLayout.Mips );
    if( !InLayout.IsValid() || InDepth.size() < static_cast<size_t>( InLayout.SourceWidth ) * InLayout.SourceHeight )
    {
        return {};
    }
    // Mip 0: the max of every depth texel a pyramid texel overlaps (GpuCullDepthPyramid.comp's first
    // pass, in the same integer arithmetic).
    const uint32_t sw = InLayout.SourceWidth;
    const uint32_t sh = InLayout.SourceHeight;
    const uint32_t w = InLayout.Width;
    const uint32_t h = InLayout.Height;
    mips[0].assign( static_cast<size_t>( w ) * h, 0.f );
    for( uint32_t y = 0; y < h; ++y )
    {
        const uint32_t y0 = ( y * sh ) / h;
        const uint32_t y1 = std::min( ( ( y + 1 ) * sh + h - 1 ) / h - 1, sh - 1 );
        for( uint32_t x = 0; x < w; ++x )
        {
            const uint32_t x0 = ( x * sw ) / w;
            const uint32_t x1 = std::min( ( ( x + 1 ) * sw + w - 1 ) / w - 1, sw - 1 );
            float depth = 0.f;
            for( uint32_t j = 0; j < 3; ++j )
            {
                for( uint32_t i = 0; i < 3; ++i )
                {
                    depth = std::max( depth, InDepth[static_cast<size_t>( std::min( y0 + j, y1 ) ) * sw + std::min( x0 + i, x1 )] );
                }
            }
            mips[0][static_cast<size_t>( y ) * w + x] = depth;
        }
    }
    // Every later mip: the max of the 2x2 texels below it that exist (the groupshared reduction
    // counts missing ones as 0, a pass's first mip clamps to the edge: the same max).
    for( uint32_t m = 1; m < InLayout.Mips; ++m )
    {
        const uint32_t pw = InLayout.GetMipWidth( m - 1 );
        const uint32_t ph = InLayout.GetMipHeight( m - 1 );
        const uint32_t mw = InLayout.GetMipWidth( m );
        const uint32_t mh = InLayout.GetMipHeight( m );
        mips[m].assign( static_cast<size_t>( mw ) * mh, 0.f );
        for( uint32_t y = 0; y < mh; ++y )
        {
            for( uint32_t x = 0; x < mw; ++x )
            {
                float depth = 0.f;
                for( uint32_t j = 0; j < 2; ++j )
                {
                    for( uint32_t i = 0; i < 2; ++i )
                    {
                        const uint32_t px = x * 2 + i;
                        const uint32_t py = y * 2 + j;
                        if( px < pw && py < ph )
                        {
                            depth = std::max( depth, mips[m - 1][static_cast<size_t>( py ) * pw + px] );
                        }
                    }
                }
                mips[m][static_cast<size_t>( y ) * mw + x] = depth;
            }
        }
    }
    return mips;
}


bool Moonlight::HiZBoxVisible( const HiZLayout& InLayout, const HiZProjection& InProjection, const glm::vec3& InMin, const glm::vec3& InMax,
    const std::function<float( uint32_t InMip, uint32_t InX, uint32_t InY )>& InTexel )
{
    // GpuCullOcclusion.sh's hizVisible, step for step.
    glm::vec3 low( 1.0e30f );
    glm::vec3 high( -1.0e30f );
    for( uint32_t corner = 0; corner < 8; ++corner )
    {
        const glm::vec3 p( ( corner & 1u ) ? InMax.x : InMin.x, ( corner & 2u ) ? InMax.y : InMin.y, ( corner & 4u ) ? InMax.z : InMin.z );
        const glm::mat4& m = InProjection.ViewProjection;
        const glm::vec4 clip = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
        if( clip.w <= 1.0e-6f )
        {
            return true;
        }
        const glm::vec3 ndc = glm::vec3( clip ) / clip.w;
        low = glm::min( low, ndc );
        high = glm::max( high, ndc );
    }
    if( low.z < ( InProjection.HomogeneousDepth ? -1.f : 0.f ) )
    {
        return true;
    }
    const float nearest = InProjection.HomogeneousDepth ? low.z * 0.5f + 0.5f : low.z;
    const float u0 = low.x * 0.5f + 0.5f - 1.0e-5f;
    const float u1 = high.x * 0.5f + 0.5f + 1.0e-5f;
    float v0 = 0.5f - high.y * 0.5f - 1.0e-5f;
    float v1 = 0.5f - low.y * 0.5f + 1.0e-5f;
    if( InProjection.OriginBottomLeft )
    {
        v0 = low.y * 0.5f + 0.5f - 1.0e-5f;
        v1 = high.y * 0.5f + 0.5f + 1.0e-5f;
    }
    if( u1 < 0.f || u0 > 1.f || v1 < 0.f || v0 > 1.f )
    {
        return true;
    }
    const uint32_t width = InLayout.Width;
    const uint32_t height = InLayout.Height;
    const auto texelOf = []( float InUv, uint32_t InSize ) {
        return static_cast<uint32_t>( std::clamp( std::floor( InUv * static_cast<float>( InSize ) ), 0.f, static_cast<float>( InSize - 1 ) ) );
    };
    uint32_t x0 = texelOf( u0, width );
    uint32_t x1 = texelOf( u1, width );
    uint32_t y0 = texelOf( v0, height );
    uint32_t y1 = texelOf( v1, height );
    uint32_t mip = 0;
    for( uint32_t k = 0; k < 16; ++k )
    {
        if( ( x1 >> mip ) - ( x0 >> mip ) <= 1u && ( y1 >> mip ) - ( y0 >> mip ) <= 1u )
        {
            break;
        }
        ++mip;
    }
    if( mip >= InLayout.Mips )
    {
        return true;
    }
    x0 >>= mip;
    x1 >>= mip;
    y0 >>= mip;
    y1 >>= mip;
    const float farthest = std::max( std::max( InTexel( mip, x0, y0 ), InTexel( mip, x1, y0 ) ), std::max( InTexel( mip, x0, y1 ), InTexel( mip, x1, y1 ) ) );
    return nearest <= farthest + InProjection.DepthEpsilon;
}


void Moonlight::GpuCullState::Reset()
{
    for( MeshBatchRegistry& registry : m_registries )
    {
        registry.Clear();
    }
    for( GpuCullSetLayout& layout : m_layouts )
    {
        layout = GpuCullSetLayout();
    }
    m_opaqueMask.clear();
    m_shadowMask.clear();
    m_opaqueLeft.clear();
    m_records.clear();
    m_recordCount = 0;
    m_staging.clear();
    m_members.clear();
    m_groupBatch.clear();
    m_layoutDirty = true;
    m_boundsDirty[0] = m_boundsDirty[1] = true;
    m_updateAll = false;
}


uint32_t Moonlight::GpuCullState::GetMemberCount( Set InSet ) const
{
    uint32_t count = 0;
    for( const GpuCullBatch& batch : GetLayout( InSet ).Batches )
    {
        count += batch.MemberCount;
    }
    return count;
}


void Moonlight::GpuCullState::Begin( uint32_t InCommandCount, bool InAll )
{
    m_opaqueLeft.clear();
    m_staging.clear();
    m_updateAll = InAll || m_recordCount == 0;
    if( m_updateAll )
    {
        // Every id comes through SetMesh again: the sets start from nothing (the masks stay as they
        // were until Finish, which works out who left from them).
        for( MeshBatchRegistry& registry : m_registries )
        {
            registry.Clear();
        }
        m_recordCount = InCommandCount;
        m_layoutDirty = true;
    }
    m_recordCount = std::max( m_recordCount, InCommandCount );
    m_records.resize( static_cast<size_t>( m_recordCount ) * kGpuCullRecordVec4s, glm::vec4( 0.f ) );
}


void Moonlight::GpuCullState::SetMesh( uint32_t InId, const MeshBatchRegistry::Key* InOpaque, const MeshBatchRegistry::Key* InShadow, const glm::mat4& InInstance, const AABB& InBounds )
{
    if( InId >= m_recordCount )
    {
        m_recordCount = InId + 1;
        m_records.resize( static_cast<size_t>( m_recordCount ) * kGpuCullRecordVec4s, glm::vec4( 0.f ) );
    }
    const auto assign = [this]( size_t InSet, uint32_t InCommand, const MeshBatchRegistry::Key* InKey ) {
        MeshBatchRegistry& registry = m_registries[InSet];
        const uint32_t before = registry.GetBatch( InCommand );
        uint32_t after = MeshBatchRegistry::kNone;
        if( InKey )
        {
            after = registry.Assign( InCommand, *InKey );
        }
        else
        {
            registry.Remove( InCommand );
        }
        m_layoutDirty |= before != after;
        // A member moved, or the set changed: its batch's bounds follow.
        m_boundsDirty[InSet] |= before != MeshBatchRegistry::kNone || after != MeshBatchRegistry::kNone;
    };
    assign( static_cast<size_t>( Set::Opaque ), InId, InOpaque );
    assign( static_cast<size_t>( Set::Shadow ), InId, InShadow );

    if( !InOpaque && !InShadow )
    {
        return;     // nothing reads its record
    }
    glm::vec4* record = m_records.data() + static_cast<size_t>( InId ) * kGpuCullRecordVec4s;
    PackGpuCullRecord( InInstance, InBounds, record );
    if( !m_updateAll )
    {
        m_staging.push_back( glm::vec4( static_cast<float>( InId ), 0.f, 0.f, 0.f ) );
        m_staging.insert( m_staging.end(), record, record + kGpuCullRecordVec4s );
    }
}


bool Moonlight::GpuCullState::Finish( const std::function<uint32_t( uint32_t InId )>& InIndexCount )
{
    const bool relayout = m_layoutDirty;
    if( relayout )
    {
        m_layoutDirty = false;
        m_members.clear();
        m_groupBatch.clear();
        uint32_t batchBase = 0;
        for( size_t s = 0; s < static_cast<size_t>( Set::Count ); ++s )
        {
            BuildGpuCullSet( m_registries[s], batchBase, m_layouts[s], m_members, m_groupBatch, std::max( m_minMembers, 1u ) );
            m_layouts[s].Triangles = 0;
            for( GpuCullBatch& batch : m_layouts[s].Batches )
            {
                batch.IndexCount = InIndexCount ? InIndexCount( batch.Representative ) : 0u;
                m_layouts[s].Triangles += static_cast<uint64_t>( batch.MemberCount ) * ( batch.IndexCount / 3 );
            }
            batchBase += m_layouts[s].GetBatchCount();
            m_boundsDirty[s] = true;
        }
        // Who the GPU path draws now (members of the laid-out batches), and who it drew before and
        // doesn't any more: the camera loops visit those this frame although RenderCore's lists
        // (made from the mask as it was) left them out.
        std::vector<uint8_t> opaque( m_recordCount, 0 );
        std::vector<uint8_t> shadow( m_recordCount, 0 );
        const auto mark = [this]( Set InSet, std::vector<uint8_t>& OutMask ) {
            const GpuCullSetLayout& layout = GetLayout( InSet );
            for( const GpuCullBatch& batch : layout.Batches )
            {
                for( uint32_t m = 0; m < batch.MemberCount; ++m )
                {
                    const uint32_t id = m_members[layout.MemberBase + batch.MemberStart + m];
                    if( id < OutMask.size() ) OutMask[id] = 1;
                }
            }
        };
        mark( Set::Opaque, opaque );
        mark( Set::Shadow, shadow );
        for( size_t id = 0; id < m_opaqueMask.size(); ++id )
        {
            if( m_opaqueMask[id] && ( id >= opaque.size() || !opaque[id] ) )
            {
                m_opaqueLeft.push_back( static_cast<uint32_t>( id ) );
            }
        }
        m_opaqueMask = std::move( opaque );
        m_shadowMask = std::move( shadow );
    }
    // Each batch's bounds, from its members' records, when any member of the set changed.
    for( size_t s = 0; s < static_cast<size_t>( Set::Count ); ++s )
    {
        if( !m_boundsDirty[s] )
        {
            continue;
        }
        m_boundsDirty[s] = false;
        GpuCullSetLayout& layout = m_layouts[s];
        for( GpuCullBatch& batch : layout.Batches )
        {
            glm::vec3 low( FLT_MAX );
            glm::vec3 high( -FLT_MAX );
            for( uint32_t m = 0; m < batch.MemberCount; ++m )
            {
                const uint32_t id = m_members[layout.MemberBase + batch.MemberStart + m];
                const glm::vec4* record = m_records.data() + static_cast<size_t>( id ) * kGpuCullRecordVec4s;
                if( record[4].w < 0.5f )
                {
                    continue;
                }
                low = glm::min( low, glm::vec3( record[4] ) );
                high = glm::max( high, glm::vec3( record[5] ) );
            }
            batch.Bounds = AABB( Vector3( low ), Vector3( high ) );
        }
    }
    return relayout;
}


Moonlight::GpuMeshCulling::GpuMeshCulling()
{
    m_count = AcquireComputeProgram( kCountShader );
    m_offsets = AcquireComputeProgram( kOffsetsShader );
    m_write = AcquireComputeProgram( kWriteShader );
    m_scatter = AcquireComputeProgram( kScatterShader );
    m_occlusionCount = AcquireComputeProgram( kOcclusionCountShader );
    m_occlusionOffsets = AcquireComputeProgram( kOcclusionOffsetsShader );
    m_occlusionWrite = AcquireComputeProgram( kOcclusionWriteShader );
    m_pyramid = AcquireComputeProgram( kPyramidShader );
    u_gpuCull = bgfx::createUniform( "u_gpuCull", bgfx::UniformType::Vec4, kCullParams );
    u_gpuCullScatter = bgfx::createUniform( "u_gpuCullScatter", bgfx::UniformType::Vec4 );
    u_gpuCullOcc = bgfx::createUniform( "u_gpuCullOcc", bgfx::UniformType::Vec4, kHiZCullParams );
    u_hizBuild = bgfx::createUniform( "u_hizBuild", bgfx::UniformType::Vec4, 2 );
    s_hizSource = bgfx::createUniform( "s_hizSource", bgfx::UniformType::Sampler );
    for( uint32_t t = 0; t < kHiZMaxTextures; ++t )
    {
        s_hiz[t] = bgfx::createUniform( ( "s_hiz" + std::to_string( t ) ).c_str(), bgfx::UniformType::Sampler );
    }
    // Stands in for pyramid textures a cull doesn't read (a small pyramid, phase 2 without one).
    const float farthest = 1.f;
    m_hizDummy = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::R32F, kPointClamp, bgfx::copy( &farthest, sizeof( farthest ) ) );
    bgfx::setName( m_hizDummy, "GPU Cull Pyramid Dummy" );
}


Moonlight::GpuMeshCulling::~GpuMeshCulling()
{
    if( !IsGpuAlive() )
    {
        return;
    }
    DestroyBuffers();
    for( auto& [key, history] : m_histories )
    {
        DestroyHistory( history );
    }
    m_histories.clear();
    DestroyHandle( u_gpuCull );
    DestroyHandle( u_gpuCullScatter );
    DestroyHandle( u_gpuCullOcc );
    DestroyHandle( u_hizBuild );
    DestroyHandle( s_hizSource );
    for( bgfx::UniformHandle& sampler : s_hiz )
    {
        DestroyHandle( sampler );
    }
    DestroyHandle( m_hizDummy );
}


bool Moonlight::GpuMeshCulling::IsReady() const
{
    const auto ready = []( const SharedPtr<ProgramRef>& InProgram ) { return InProgram && bgfx::isValid( InProgram->Handle ); };
    return ready( m_count ) && ready( m_offsets ) && ready( m_write ) && ready( m_scatter );
}


void Moonlight::GpuMeshCulling::DestroyBuffers()
{
    DestroyHandle( m_recordBuffer );
    DestroyHandle( m_stagingBuffer );
    DestroyHandle( m_memberBuffer );
    DestroyHandle( m_groupBatchBuffer );
    DestroyHandle( m_batchInfoBuffer );
    DestroyHandle( m_groupScratch );
    DestroyHandle( m_totals );
    DestroyHandle( m_instances );
    DestroyHandle( m_drawArgs );
    DestroyHandle( m_occlusionState );
    m_recordCapacity = m_stagingCapacity = m_memberCapacity = m_groupCapacity = m_batchCapacity = 0;
    m_groupScratchCapacity = m_totalsCapacity = m_instanceCapacity = m_argsCapacity = 0;
    m_occlusionStateCapacity = 0;
}


bool Moonlight::GpuMeshCulling::EndUpdate( const std::function<uint32_t( uint32_t InId )>& InIndexCount )
{
    const bool full = m_state.IsFullUpdate();
    const bool layoutChanged = m_state.Finish( InIndexCount );
    m_layoutChanged = layoutChanged;
    m_stagingRows = 0;

    // The records: the whole buffer, or the changed rows for the scatter pass.
    const uint32_t recordCount = m_state.GetRecordCount();
    const GpuCullUploadPlan plan = PlanGpuCullUpload( m_state.GetStagingRows(), full, recordCount, m_recordCapacity );
    bool recreated = false;
    if( plan.Capacity != m_recordCapacity || !bgfx::isValid( m_recordBuffer ) )
    {
        DestroyHandle( m_recordBuffer );
        m_recordCapacity = std::max<uint32_t>( plan.Capacity, 64u );
        m_recordBuffer = bgfx::createDynamicVertexBuffer( m_recordCapacity * kGpuCullRecordVec4s, Vec4Layout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
        recreated = true;
    }
    // The record buffer is written by compute (BGFX_BUFFER_COMPUTE_WRITE), so it is never updated
    // from the CPU (bgfx asserts on that): rows always go up through the staging buffer and the
    // scatter pass. A full upload stages every record; a partial one only the changed rows.
    const bool all = plan.Full || recreated;
    const uint32_t rows = all ? recordCount : m_state.GetStagingRows();
    if( rows > 0 )
    {
        if( rows > m_stagingCapacity || !bgfx::isValid( m_stagingBuffer ) )
        {
            DestroyHandle( m_stagingBuffer );
            m_stagingCapacity = GrownCapacity( rows );
            m_stagingBuffer = bgfx::createDynamicVertexBuffer( m_stagingCapacity * kGpuCullStagingVec4s, Vec4Layout(), BGFX_BUFFER_COMPUTE_READ );
        }
        if( all )
        {
            const bgfx::Memory* memory = bgfx::alloc( rows * kGpuCullStagingVec4s * static_cast<uint32_t>( sizeof( glm::vec4 ) ) );
            glm::vec4* staged = reinterpret_cast<glm::vec4*>( memory->data );
            const glm::vec4* records = m_state.GetRecords().data();
            for( uint32_t id = 0; id < rows; ++id )
            {
                glm::vec4* row = staged + static_cast<size_t>( id ) * kGpuCullStagingVec4s;
                row[0] = glm::vec4( static_cast<float>( id ), 0.f, 0.f, 0.f );
                std::memcpy( row + 1, records + static_cast<size_t>( id ) * kGpuCullRecordVec4s, sizeof( glm::vec4 ) * kGpuCullRecordVec4s );
            }
            bgfx::update( m_stagingBuffer, 0, memory );
        }
        else
        {
            const std::vector<glm::vec4>& staging = m_state.GetStaging();
            bgfx::update( m_stagingBuffer, 0, bgfx::copy( staging.data(), static_cast<uint32_t>( staging.size() * sizeof( glm::vec4 ) ) ) );
        }
        m_stagingRows = rows;
    }
    m_state.DropStaging();

    // The layout, when any membership changed.
    if( layoutChanged )
    {
        const std::vector<uint32_t>& members = m_state.GetMembers();
        const std::vector<uint32_t>& groupBatch = m_state.GetGroupBatch();
        const uint32_t memberCount = static_cast<uint32_t>( members.size() );
        const uint32_t groupCount = static_cast<uint32_t>( groupBatch.size() );
        if( memberCount > 0 )
        {
            if( memberCount > m_memberCapacity || !bgfx::isValid( m_memberBuffer ) )
            {
                DestroyHandle( m_memberBuffer );
                m_memberCapacity = GrownCapacity( memberCount );
                m_memberBuffer = bgfx::createDynamicIndexBuffer( m_memberCapacity, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ );
            }
            bgfx::update( m_memberBuffer, 0, bgfx::copy( members.data(), memberCount * sizeof( uint32_t ) ) );

            std::vector<glm::vec4> groups( groupCount );
            for( uint32_t g = 0; g < groupCount; ++g )
            {
                groups[g] = glm::vec4( static_cast<float>( groupBatch[g] ), 0.f, 0.f, 0.f );
            }
            if( groupCount > m_groupCapacity || !bgfx::isValid( m_groupBatchBuffer ) )
            {
                DestroyHandle( m_groupBatchBuffer );
                m_groupCapacity = GrownCapacity( groupCount );
                m_groupBatchBuffer = bgfx::createDynamicVertexBuffer( m_groupCapacity, Vec4Layout(), BGFX_BUFFER_COMPUTE_READ );
            }
            bgfx::update( m_groupBatchBuffer, 0, bgfx::copy( groups.data(), groupCount * sizeof( glm::vec4 ) ) );

            uint32_t batchTotal = 0;
            uint32_t maxGroups = 0;
            uint32_t maxBatches = 0;
            for( size_t s = 0; s < static_cast<size_t>( Set::Count ); ++s )
            {
                const GpuCullSetLayout& layout = m_state.GetLayout( static_cast<Set>( s ) );
                batchTotal += layout.GetBatchCount();
                maxGroups = std::max( maxGroups, layout.Groups );
                maxBatches = std::max( maxBatches, layout.GetBatchCount() );
            }
            std::vector<glm::vec4> info( static_cast<size_t>( batchTotal ) * 2 );
            for( size_t s = 0; s < static_cast<size_t>( Set::Count ); ++s )
            {
                const GpuCullSetLayout& layout = m_state.GetLayout( static_cast<Set>( s ) );
                for( uint32_t b = 0; b < layout.GetBatchCount(); ++b )
                {
                    const GpuCullBatch& batch = layout.Batches[b];
                    info[( layout.BatchBase + b ) * 2] = glm::vec4( static_cast<float>( batch.MemberStart ), static_cast<float>( batch.MemberCount ), static_cast<float>( batch.GroupStart ), static_cast<float>( batch.GroupCount ) );
                    info[( layout.BatchBase + b ) * 2 + 1] = glm::vec4( static_cast<float>( batch.IndexCount ), 0.f, 0.f, 0.f );
                }
            }
            if( batchTotal > m_batchCapacity || !bgfx::isValid( m_batchInfoBuffer ) )
            {
                DestroyHandle( m_batchInfoBuffer );
                m_batchCapacity = GrownCapacity( batchTotal );
                m_batchInfoBuffer = bgfx::createDynamicVertexBuffer( m_batchCapacity * 2, Vec4Layout(), BGFX_BUFFER_COMPUTE_READ );
            }
            bgfx::update( m_batchInfoBuffer, 0, bgfx::copy( info.data(), static_cast<uint32_t>( info.size() * sizeof( glm::vec4 ) ) ) );

            // Pass scratch for the largest set at the most views.
            const uint32_t scratch = maxGroups * kGpuCullMaxViews;
            if( scratch > m_groupScratchCapacity || !bgfx::isValid( m_groupScratch ) )
            {
                DestroyHandle( m_groupScratch );
                m_groupScratchCapacity = GrownCapacity( scratch );
                m_groupScratch = bgfx::createDynamicVertexBuffer( m_groupScratchCapacity, Vec4Layout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
            }
            const uint32_t totals = maxBatches * kGpuCullMaxViews;
            if( totals > m_totalsCapacity || !bgfx::isValid( m_totals ) )
            {
                DestroyHandle( m_totals );
                m_totalsCapacity = GrownCapacity( totals );
                m_totals = bgfx::createDynamicVertexBuffer( m_totalsCapacity, Vec4Layout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
            }
        }
    }
    return m_stagingRows > 0;
}


void Moonlight::GpuMeshCulling::DispatchScatter( bgfx::ViewId InView )
{
    if( m_stagingRows == 0 || !bgfx::isValid( m_stagingBuffer ) || !bgfx::isValid( m_recordBuffer ) || !m_scatter || !bgfx::isValid( m_scatter->Handle ) )
    {
        return;
    }
    const float params[4] = { static_cast<float>( m_stagingRows ), 0.f, 0.f, 0.f };
    bgfx::setUniform( u_gpuCullScatter, params );
    bgfx::setBuffer( 0, m_stagingBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 1, m_recordBuffer, bgfx::Access::Write );
    bgfx::dispatch( InView, m_scatter->Handle, ComputeGroupCount( m_stagingRows, kSmallGroup ), 1, 1 );
    ++m_dispatches;
    m_stagingRows = 0;
}


void Moonlight::GpuMeshCulling::BeginFrame()
{
    m_lastDispatches = m_dispatches;
    m_lastFallbacks = m_fallbacks;
    m_lastPyramids = m_pyramids;
    m_dispatches = 0;
    m_fallbacks = 0;
    m_pyramids = 0;
    ++m_frame;
    for( auto it = m_histories.begin(); it != m_histories.end(); )
    {
        if( it->second.UsedFrame + kHiZHistoryFrames < m_frame )
        {
            DestroyHistory( it->second );
            it = m_histories.erase( it );
        }
        else
        {
            ++it;
        }
    }
    // Room for what the last frame asked for; a first guess of a camera and four cascades.
    uint32_t instances = m_instanceDemand;
    uint32_t args = m_argsDemand;
    if( m_instanceCapacity == 0 )
    {
        instances = std::max( instances, GetLayout( Set::Opaque ).Padded * 2 + GetLayout( Set::Shadow ).Padded * 4 );
        args = std::max( args, GetLayout( Set::Opaque ).GetBatchCount() * 2 + GetLayout( Set::Shadow ).GetBatchCount() * 4 );
    }
    if( instances > m_instanceCapacity )
    {
        DestroyHandle( m_instances );
        m_instanceCapacity = GrownCapacity( instances );
        m_instances = bgfx::createDynamicVertexBuffer( m_instanceCapacity, InstanceLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
    }
    if( args > m_argsCapacity )
    {
        DestroyHandle( m_drawArgs );
        m_argsCapacity = GrownCapacity( args );
        m_drawArgs = bgfx::createIndirectBuffer( m_argsCapacity );
    }
    m_instanceUsed = 0;
    m_argsUsed = 0;
    m_instanceDemand = 0;
    m_argsDemand = 0;
}


Moonlight::GpuMeshCulling::CullResult Moonlight::GpuMeshCulling::Cull( bgfx::ViewId InView, Set InSet, const glm::vec4* InPlanes, uint32_t InViewCount, bool InReverse, bool InShadowMatrices )
{
    CullResult result;
    result.Which = InSet;
    result.Views = InViewCount;
    const GpuCullSetLayout& layout = GetLayout( InSet );
    const uint32_t batches = layout.GetBatchCount();
    if( batches == 0 )
    {
        result.Valid = true;    // nothing of this set to draw
        return result;
    }
    if( InViewCount == 0 || InViewCount > kGpuCullMaxViews || InView == UINT16_MAX || !IsReady() || !bgfx::isValid( m_recordBuffer ) || !bgfx::isValid( m_memberBuffer ) )
    {
        ++m_fallbacks;
        return result;
    }
    const uint32_t instances = layout.Padded * InViewCount;
    const uint32_t args = batches * InViewCount;
    m_instanceDemand += instances;
    m_argsDemand += args;
    if( m_instanceUsed + instances > m_instanceCapacity || m_argsUsed + args > m_argsCapacity || !bgfx::isValid( m_instances ) || !bgfx::isValid( m_drawArgs ) )
    {
        ++m_fallbacks;
        return result;
    }
    result.Valid = true;
    result.OutBase = m_instanceUsed;
    result.ArgsBase = m_argsUsed;
    result.ColourArgsBase = result.ArgsBase;
    m_instanceUsed += instances;
    m_argsUsed += args;

    DispatchCull( InView, InSet, InPlanes, InViewCount, InReverse, InShadowMatrices, result.OutBase, result.ArgsBase, nullptr, nullptr );
    return result;
}


void Moonlight::GpuMeshCulling::SetInstances( const CullResult& InCull, uint32_t InView, uint32_t InBatch ) const
{
    const GpuCullSetLayout& layout = GetLayout( InCull.Which );
    const GpuCullBatch& batch = layout.Batches[InBatch];
    bgfx::setInstanceDataBuffer( m_instances, GpuCullInstanceStart( InCull.OutBase, layout.Padded, InView, batch ), batch.MemberCount );
}


uint32_t Moonlight::GpuMeshCulling::GetArgsIndex( const CullResult& InCull, uint32_t InView, uint32_t InBatch ) const
{
    return GpuCullArgsIndex( InCull.ArgsBase, GetLayout( InCull.Which ).GetBatchCount(), InView, InBatch );
}


uint32_t Moonlight::GpuMeshCulling::GetColourArgsIndex( const CullResult& InCull, uint32_t InView, uint32_t InBatch ) const
{
    return GpuCullArgsIndex( InCull.ColourArgsBase, GetLayout( InCull.Which ).GetBatchCount(), InView, InBatch );
}


void Moonlight::GpuMeshCulling::DispatchCull( bgfx::ViewId InView, Set InSet, const glm::vec4* InPlanes, uint32_t InViewCount, bool InReverse, bool InShadowMatrices, uint32_t InOutBase, uint32_t InArgsBase,
    const glm::vec4* InOcclusion, const HiZHistory* InPyramid, uint32_t InOcclusionPhase )
{
    const GpuCullSetLayout& layout = GetLayout( InSet );
    const uint32_t batches = layout.GetBatchCount();
    glm::vec4 params[kCullParams] = {};
    params[0] = glm::vec4( static_cast<float>( layout.MemberBase ), static_cast<float>( layout.Padded ), static_cast<float>( layout.Groups ), static_cast<float>( batches ) );
    params[1] = glm::vec4( static_cast<float>( layout.BatchBase ), static_cast<float>( InViewCount ), static_cast<float>( InArgsBase ), static_cast<float>( InOutBase ) );
    params[2] = glm::vec4( InReverse ? 1.f : 0.f, InShadowMatrices ? 1.f : 0.f, static_cast<float>( layout.GroupBase ), static_cast<float>( InOcclusionPhase ) );
    std::memcpy( &params[3], InPlanes, sizeof( glm::vec4 ) * 6 * InViewCount );
    const bool occlusion = InOcclusion != nullptr;

    bgfx::setUniform( u_gpuCull, params, kCullParams );
    bgfx::setBuffer( 0, m_memberBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 1, m_recordBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 2, m_groupScratch, bgfx::Access::Write );
    if( occlusion )
    {
        bgfx::setUniform( u_gpuCullOcc, InOcclusion, kHiZCullParams );
        bgfx::setBuffer( kOcclusionStateStage, m_occlusionState, bgfx::Access::ReadWrite );
        for( uint32_t t = 0; t < kHiZMaxTextures; ++t )
        {
            const bgfx::TextureHandle texture = InPyramid && bgfx::isValid( InPyramid->Textures[t] ) ? InPyramid->Textures[t] : m_hizDummy;
            bgfx::setTexture( static_cast<uint8_t>( kOcclusionPyramidStage + t ), s_hiz[t], texture, kPointClamp );
        }
    }
    bgfx::dispatch( InView, occlusion ? m_occlusionCount->Handle : m_count->Handle, layout.Groups, InViewCount, 1 );

    bgfx::setUniform( u_gpuCull, params, kCullParams );
    bgfx::setBuffer( 0, m_batchInfoBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 1, m_groupScratch, bgfx::Access::ReadWrite );
    bgfx::setBuffer( 2, m_totals, bgfx::Access::Write );
    bgfx::setBuffer( 3, m_drawArgs, bgfx::Access::Write );
    if( occlusion )
    {
        // Phase 1 hands its totals to phase 2 here; phase 2 writes the merged arguments (ME-404).
        bgfx::setBuffer( kOcclusionStateStage, m_occlusionState, bgfx::Access::ReadWrite );
    }
    bgfx::dispatch( InView, occlusion ? m_occlusionOffsets->Handle : m_offsets->Handle, batches * InViewCount, 1, 1 );

    bgfx::setUniform( u_gpuCull, params, kCullParams );
    bgfx::setBuffer( 0, m_memberBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 1, m_recordBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 2, m_groupScratch, bgfx::Access::Read );
    bgfx::setBuffer( 3, m_groupBatchBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 4, m_batchInfoBuffer, bgfx::Access::Read );
    bgfx::setBuffer( 5, m_totals, bgfx::Access::Read );
    bgfx::setBuffer( 6, m_instances, bgfx::Access::Write );
    if( occlusion )
    {
        bgfx::setBuffer( kOcclusionStateStage, m_occlusionState, bgfx::Access::Read );
    }
    bgfx::dispatch( InView, occlusion ? m_occlusionWrite->Handle : m_write->Handle, layout.Groups, InViewCount, 1 );
    m_dispatches += 3;
}


bool Moonlight::GpuMeshCulling::IsOcclusionReady() const
{
    const auto ready = []( const SharedPtr<ProgramRef>& InProgram ) { return InProgram && bgfx::isValid( InProgram->Handle ); };
    const uint32_t r32f = bgfx::getCaps()->formats[bgfx::TextureFormat::R32F];
    return IsReady() && ready( m_occlusionCount ) && ready( m_occlusionOffsets ) && ready( m_occlusionWrite ) && ready( m_pyramid ) && bgfx::isValid( m_hizDummy )
        && ( r32f & BGFX_CAPS_FORMAT_TEXTURE_2D ) != 0 && ( r32f & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE ) != 0;
}


void Moonlight::GpuMeshCulling::DestroyHistory( HiZHistory& InOutHistory )
{
    for( bgfx::TextureHandle& texture : InOutHistory.Textures )
    {
        DestroyHandle( texture );
    }
    InOutHistory.BuiltFrame = 0;
}


Moonlight::GpuMeshCulling::HiZHistory* Moonlight::GpuMeshCulling::AcquireHistory( const void* InKey, uint32_t InDepthWidth, uint32_t InDepthHeight )
{
    const HiZLayout layout = MakeHiZLayout( InDepthWidth, InDepthHeight );
    if( !layout.IsValid() )
    {
        return nullptr;
    }
    HiZHistory& history = m_histories[InKey];
    history.UsedFrame = m_frame;
    if( history.Layout == layout && bgfx::isValid( history.Textures[0] ) )
    {
        return &history;
    }
    // A new size: new textures, and no history until it is built.
    DestroyHistory( history );
    history.Layout = layout;
    for( uint32_t pass = 0; pass < layout.GetPasses(); ++pass )
    {
        const uint32_t first = pass * kHiZMipsPerPass;
        const uint32_t mips = std::min( kHiZMipsPerPass, layout.Mips - first );
        history.Textures[pass] = bgfx::createTexture2D( static_cast<uint16_t>( layout.GetMipWidth( first ) ), static_cast<uint16_t>( layout.GetMipHeight( first ) ), mips > 1, 1,
            bgfx::TextureFormat::R32F, BGFX_TEXTURE_COMPUTE_WRITE | kPointClamp );
        if( !bgfx::isValid( history.Textures[pass] ) )
        {
            DestroyHistory( history );
            history.Layout = HiZLayout();
            return nullptr;
        }
        bgfx::setName( history.Textures[pass], "GPU Cull Depth Pyramid" );
    }
    return &history;
}


bool Moonlight::GpuMeshCulling::HasHistory( const HiZHistory& InHistory, bool InOrthographic ) const
{
    return InHistory.BuiltFrame != 0 && InHistory.BuiltFrame + 1 == m_frame && InHistory.Orthographic == InOrthographic && bgfx::isValid( InHistory.Textures[0] );
}


Moonlight::GpuMeshCulling::OcclusionCull Moonlight::GpuMeshCulling::CullOcclusion( bgfx::ViewId InView, const glm::vec4* InPlanes, bool InReverse, const HiZHistory& InHistory )
{
    OcclusionCull result;
    result.Phase1.Which = Set::Opaque;
    result.Phase1.Views = 1;
    const GpuCullSetLayout& layout = GetLayout( Set::Opaque );
    const uint32_t batches = layout.GetBatchCount();
    if( batches == 0 )
    {
        result.Phase1.Valid = true;
        return result;
    }
    if( InView == UINT16_MAX || !IsOcclusionReady() || !bgfx::isValid( m_recordBuffer ) || !bgfx::isValid( m_memberBuffer ) )
    {
        ++m_fallbacks;
        return result;
    }
    // Both phases' output at once: phase 2 must never find it full after phase 1 rejected meshes.
    // Arguments for phase 1, phase 2 and the merged colour draws (ME-404).
    const uint32_t instances = layout.Padded * 2;
    const uint32_t args = batches * 3;
    m_instanceDemand += instances;
    m_argsDemand += args;
    if( m_instanceUsed + instances > m_instanceCapacity || m_argsUsed + args > m_argsCapacity || !bgfx::isValid( m_instances ) || !bgfx::isValid( m_drawArgs ) )
    {
        ++m_fallbacks;
        return result;
    }
    if( layout.Padded > m_occlusionStateCapacity || !bgfx::isValid( m_occlusionState ) )
    {
        DestroyHandle( m_occlusionState );
        m_occlusionStateCapacity = GrownCapacity( layout.Padded );
        m_occlusionState = bgfx::createDynamicVertexBuffer( m_occlusionStateCapacity, Vec4Layout(), BGFX_BUFFER_COMPUTE_READ_WRITE );
        if( !bgfx::isValid( m_occlusionState ) )
        {
            m_occlusionStateCapacity = 0;
            ++m_fallbacks;
            return result;
        }
    }
    result.Phase1.Valid = true;
    result.Phase1.OutBase = m_instanceUsed;
    result.Phase1.ArgsBase = m_argsUsed;
    result.Phase1.ColourArgsBase = m_argsUsed;
    result.Phase2OutBase = m_instanceUsed + layout.Padded;
    result.Phase2ArgsBase = m_argsUsed + batches;
    // GpuCullOffsets.sh writes them right after phase 2's.
    result.MergedArgsBase = m_argsUsed + batches * 2;
    m_instanceUsed += instances;
    m_argsUsed += args;

    glm::vec4 occlusion[kHiZCullParams];
    const glm::mat4& viewProjection = InHistory.Projection.ViewProjection;
    for( uint32_t c = 0; c < 4; ++c )
    {
        occlusion[c] = viewProjection[c];
    }
    occlusion[4] = glm::vec4( static_cast<float>( InHistory.Layout.Width ), static_cast<float>( InHistory.Layout.Height ), static_cast<float>( InHistory.Layout.Mips ), 1.f );
    occlusion[5] = glm::vec4( InHistory.Projection.HomogeneousDepth ? 1.f : 0.f, InHistory.Projection.OriginBottomLeft ? 1.f : 0.f, InHistory.Projection.DepthEpsilon, m_phase2Only ? 1.f : 0.f );
    DispatchCull( InView, Set::Opaque, InPlanes, 1, InReverse, false, result.Phase1.OutBase, result.Phase1.ArgsBase, occlusion, &InHistory, 1 );
    return result;
}


bool Moonlight::GpuMeshCulling::BuildPyramid( HiZHistory& InOutHistory, const bgfx::ViewId* InViews, bgfx::TextureHandle InDepth, const HiZProjection& InProjection, bool InOrthographic )
{
    const HiZLayout& layout = InOutHistory.Layout;
    InOutHistory.BuiltFrame = 0;
    if( !m_pyramid || !bgfx::isValid( m_pyramid->Handle ) || !bgfx::isValid( InDepth ) || !layout.IsValid() )
    {
        return false;
    }
    for( uint32_t pass = 0; pass < layout.GetPasses(); ++pass )
    {
        if( InViews[pass] == UINT16_MAX || !bgfx::isValid( InOutHistory.Textures[pass] ) )
        {
            return false;
        }
    }
    for( uint32_t pass = 0; pass < layout.GetPasses(); ++pass )
    {
        const uint32_t first = pass * kHiZMipsPerPass;
        const uint32_t mips = std::min( kHiZMipsPerPass, layout.Mips - first );
        const uint32_t width = layout.GetMipWidth( first );
        const uint32_t height = layout.GetMipHeight( first );
        // The depth buffer, or the last mip of the pass before.
        const uint32_t sourceWidth = pass == 0 ? layout.SourceWidth : layout.GetMipWidth( first - 1 );
        const uint32_t sourceHeight = pass == 0 ? layout.SourceHeight : layout.GetMipHeight( first - 1 );
        const glm::vec4 params[2] = {
            glm::vec4( static_cast<float>( sourceWidth ), static_cast<float>( sourceHeight ), static_cast<float>( width ), static_cast<float>( height ) ),
            glm::vec4( pass == 0 ? 1.f : 0.f, static_cast<float>( mips ), pass == 0 ? 0.f : static_cast<float>( kHiZMipsPerPass - 1 ), 0.f ),
        };
        bgfx::setUniform( u_hizBuild, params, 2 );
        bgfx::setTexture( 0, s_hizSource, pass == 0 ? InDepth : InOutHistory.Textures[pass - 1], kPointClamp );
        for( uint32_t m = 0; m < kHiZMipsPerPass; ++m )
        {
            // Every image stage gets a mip; the ones past this pass's last are never written.
            bgfx::setImage( static_cast<uint8_t>( 1 + m ), InOutHistory.Textures[pass], static_cast<uint8_t>( std::min( m, mips - 1 ) ), bgfx::Access::Write, bgfx::TextureFormat::R32F );
        }
        bgfx::dispatch( InViews[pass], m_pyramid->Handle, ComputeGroupCount( width, kHiZGroup ), ComputeGroupCount( height, kHiZGroup ), 1 );
        ++m_dispatches;
    }
    InOutHistory.Projection = InProjection;
    InOutHistory.Orthographic = InOrthographic;
    InOutHistory.BuiltFrame = m_frame;
    ++m_pyramids;
    return true;
}


Moonlight::GpuMeshCulling::CullResult Moonlight::GpuMeshCulling::CullOcclusionPhase2( bgfx::ViewId InView, const OcclusionCull& InPhase1, bool InReverse, const HiZHistory* InHistory )
{
    CullResult result;
    result.Which = Set::Opaque;
    result.Views = 1;
    result.OutBase = InPhase1.Phase2OutBase;
    result.ArgsBase = InPhase1.Phase2ArgsBase;
    result.ColourArgsBase = InPhase1.Phase2ArgsBase;
    const GpuCullSetLayout& layout = GetLayout( Set::Opaque );
    if( !InPhase1.Phase1.Valid || layout.GetBatchCount() == 0 )
    {
        result.Valid = InPhase1.Phase1.Valid;
        return result;
    }
    if( InView == UINT16_MAX )
    {
        // Phase 1's rejects would be missing: the caller can't draw this camera at all (its later
        // views are out of the band too).
        ++m_fallbacks;
        return result;
    }
    result.Valid = true;
    const bool pyramid = InHistory && InHistory->BuiltFrame == m_frame;
    glm::vec4 occlusion[kHiZCullParams] = {};
    if( pyramid )
    {
        for( uint32_t c = 0; c < 4; ++c )
        {
            occlusion[c] = InHistory->Projection.ViewProjection[c];
        }
        occlusion[4] = glm::vec4( static_cast<float>( InHistory->Layout.Width ), static_cast<float>( InHistory->Layout.Height ), static_cast<float>( InHistory->Layout.Mips ), 2.f );
        occlusion[5] = glm::vec4( InHistory->Projection.HomogeneousDepth ? 1.f : 0.f, InHistory->Projection.OriginBottomLeft ? 1.f : 0.f, InHistory->Projection.DepthEpsilon, 0.f );
    }
    else
    {
        occlusion[4] = glm::vec4( 1.f, 1.f, 1.f, 3.f );     // every candidate
    }
    // Phase 2 tests only phase 1's candidates, which passed the frustum already.
    const glm::vec4 planes[6] = {};
    DispatchCull( InView, Set::Opaque, planes, 1, InReverse, false, result.OutBase, result.ArgsBase, occlusion, pyramid ? InHistory : nullptr, 2 );
    return result;
}
