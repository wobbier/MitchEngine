#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include "MeshBatchRegistry.h"
#include "Math/Bounds.h"
#include "Pointers.h"

// GPU-driven culling and indirect draws (ME-389) for retained static meshes (ME-390).
//
// Every retained, static, opaque instanced mesh command keeps a record on the GPU (its instance
// matrix and world bounds), written only when its command changes. Its batch (one per geometry and
// material key, as the CPU path batches) keeps a stable id in a MeshBatchRegistry. Per camera and per
// shadow pass a compute cull (Assets/Shaders/Culling/GpuCull*.comp) writes each batch's visible
// instance matrices, in member order, into a per-frame output buffer and the batch's indexed indirect
// draw arguments; the draw then binds that output as its instance data and takes the instance count
// from the arguments. The CPU never learns the count (no readback).
//
// Two cull sets share the records: the opaque batches (colour and depth prepass draws) and the shadow
// caster batches (cascades and local shadow views). Everything else stays on the CPU path.
namespace Moonlight
{
    struct ProgramRef;

    // Members per compute group (CULL_GROUP in GpuCull.sh). Every batch starts on a whole group.
    constexpr uint32_t kGpuCullGroup = 256;
    // Views one cull dispatch covers (CULL_MAX_VIEWS): four cascades, or 4 spot + 12 point faces.
    constexpr uint32_t kGpuCullMaxViews = 16;
    // vec4s per mesh record: instance matrix columns, bounds min (w = valid), bounds max.
    constexpr uint32_t kGpuCullRecordVec4s = 6;
    // vec4s per staged record row: (id, 0, 0, 0), then the record.
    constexpr uint32_t kGpuCullStagingVec4s = kGpuCullRecordVec4s + 1;
    constexpr uint32_t kGpuCullNoMember = UINT32_MAX;
    // Batches with fewer members stay on the CPU path: it culls them exactly and skips the ones no
    // view sees, while the GPU path submits a (maybe empty) draw per batch and view.
    constexpr uint32_t kGpuCullMinBatchMembers = 16;

    // ---- CPU bookkeeping (no bgfx; unit tested in GpuCullingTests.cpp) ----

    // One batch of a cull set: where its members sit in the set's member list and group table.
    struct GpuCullBatch
    {
        uint32_t Batch = 0;             // registry id
        uint32_t Representative = 0;    // its lowest member (the mesh command whose state binds the draw)
        uint32_t MemberStart = 0;       // in the set's members (a multiple of kGpuCullGroup)
        uint32_t MemberCount = 0;
        uint32_t GroupStart = 0;        // in the set's groups
        uint32_t GroupCount = 0;
        uint32_t IndexCount = 0;        // the indexed draw's index count
        AABB Bounds;                    // its members' world bounds: a view that misses it skips the draw
    };

    // A cull set's layout in the shared buffers.
    struct GpuCullSetLayout
    {
        uint32_t MemberBase = 0;    // its first member in the shared member list
        uint32_t GroupBase = 0;     // its first group in the shared group -> batch table
        uint32_t BatchBase = 0;     // its first batch in the shared batch table
        uint32_t Padded = 0;        // members with padding: Groups * kGpuCullGroup
        uint32_t Groups = 0;
        uint64_t Triangles = 0;     // every member's triangles (what a frame draws with nothing culled)
        std::vector<GpuCullBatch> Batches;     // ordered by their lowest member
        uint32_t GetBatchCount() const { return static_cast<uint32_t>( Batches.size() ); }
    };

    // Lays out one set from its registry: batches ordered by their lowest member (the order the CPU
    // path first meets them when every member is visible), members ascending inside each, each batch
    // padded to whole groups with kGpuCullNoMember. Appends the members and, per group, its batch's
    // index in the set to the shared lists, and sets OutSet's bases from their sizes before.
    // Batches with fewer than InMinMembers members are left out (they stay on the CPU path).
    void BuildGpuCullSet( const MeshBatchRegistry& InRegistry, uint32_t InBatchBase, GpuCullSetLayout& OutSet, std::vector<uint32_t>& InOutMembers, std::vector<uint32_t>& InOutGroupBatch, uint32_t InMinMembers = 1 );

    // Where a (batch, view) of a cull lands: its indirect argument entry, and its first instance in
    // the output buffer. A view's region is the set's padded member count.
    inline uint32_t GpuCullArgsIndex( uint32_t InArgsBase, uint32_t InBatchCount, uint32_t InView, uint32_t InBatch )
    {
        return InArgsBase + InView * InBatchCount + InBatch;
    }
    inline uint32_t GpuCullInstanceStart( uint32_t InOutBase, uint32_t InPadded, uint32_t InView, const GpuCullBatch& InBatch )
    {
        return InOutBase + InView * InPadded + InBatch.MemberStart;
    }

    // The three cull passes run on the CPU, as the shaders do them (count per group, running offsets
    // per batch, write by rank). Per (view * batches + batch), the mesh command ids in output order.
    std::vector<std::vector<uint32_t>> SimulateGpuCull( const GpuCullSetLayout& InSet, const std::vector<uint32_t>& InMembers, uint32_t InViews, bool InReverse,
        const std::function<bool( uint32_t InId, uint32_t InView )>& InVisible );

    // How a frame's changed records go up: the whole record buffer (first frame, after a reset, when
    // it has to grow, or when a quarter or more of the records changed), or the changed rows through
    // the scatter pass.
    struct GpuCullUploadPlan
    {
        bool Full = false;
        uint32_t Capacity = 0;      // records the buffer must hold (grown by half when it grows)
    };
    GpuCullUploadPlan PlanGpuCullUpload( uint32_t InChanged, bool InAllChanged, uint32_t InCommandCount, uint32_t InCapacity );

    // A mesh's record: the instance matrix as BGFXRenderer::InstanceMatrix makes it, and its bounds.
    void PackGpuCullRecord( const glm::mat4& InInstance, const AABB& InBounds, glm::vec4 OutRecord[kGpuCullRecordVec4s] );

    // ---- Occlusion culling against a depth pyramid (ME-400; CPU mirrors of the shaders) ----
    //
    // A camera's depth pyramid (Hi-Z) keeps, per texel, the farthest depth of the depth prepass under
    // it. Mip 0 is the previous powers of two of the depth size (each texel covers 1 to 3 depth texels
    // per axis), each mip halves the last, down to 1x1. It is built in passes of kHiZMipsPerPass mips
    // (a 16x16 group reduces in groupshared memory), each pass into its own R32F texture, since a
    // pass reads the last mip of the pass before through a sampler and writes its own through images.
    // GpuCullDepthPyramid.comp builds it; GpuCullOcclusion.sh tests boxes against it.
    constexpr uint32_t kHiZGroup = 16;          // HIZ_GROUP
    constexpr uint32_t kHiZMipsPerPass = 5;     // HIZ_MIPS_PER_PASS: 16, 8, 4, 2, 1 texels a group
    constexpr uint32_t kHiZMaxTextures = 3;     // HIZ_TEXTURES: up to 15 mips (a 16384 texel mip 0)
    // vec4s of u_gpuCullOcc: the view-projection's columns, the pyramid, the depth convention.
    constexpr uint32_t kHiZCullParams = 6;

    struct HiZLayout
    {
        uint32_t SourceWidth = 0;   // the depth buffer
        uint32_t SourceHeight = 0;
        uint32_t Width = 0;         // mip 0
        uint32_t Height = 0;
        uint32_t Mips = 0;          // down to 1x1
        uint32_t GetPasses() const { return ( Mips + kHiZMipsPerPass - 1 ) / kHiZMipsPerPass; }
        uint32_t GetMipWidth( uint32_t InMip ) const { return std::max<uint32_t>( 1u, Width >> InMip ); }
        uint32_t GetMipHeight( uint32_t InMip ) const { return std::max<uint32_t>( 1u, Height >> InMip ); }
        bool IsValid() const { return Mips > 0 && GetPasses() <= kHiZMaxTextures; }
        bool operator==( const HiZLayout& InOther ) const
        {
            return SourceWidth == InOther.SourceWidth && SourceHeight == InOther.SourceHeight && Width == InOther.Width && Height == InOther.Height && Mips == InOther.Mips;
        }
    };
    HiZLayout MakeHiZLayout( uint32_t InDepthWidth, uint32_t InDepthHeight );

    // The pyramid as the build passes make it, from a depth buffer (InDepth: SourceWidth x
    // SourceHeight, texture rows): per mip, GetMipWidth x GetMipHeight depths.
    std::vector<std::vector<float>> BuildHiZ( const HiZLayout& InLayout, const std::vector<float>& InDepth );

    // How the pyramid's depths were made: the (jittered) view-projection the depth was drawn with,
    // and the backend's conventions (bgfx: depth 0 near, 1 far, LESS; homogeneous NDC z on GL).
    struct HiZProjection
    {
        glm::mat4 ViewProjection = glm::mat4( 1.f );
        bool HomogeneousDepth = false;  // NDC z in -1..1 (GL)
        bool OriginBottomLeft = false;  // texture row 0 is the bottom of the image (GL)
        float DepthEpsilon = 1e-6f;     // depth quantisation slack: the box must be behind by more
    };
    // The box test GpuCullOcclusion.sh makes: false only when the box is certainly behind every
    // depth under its screen rectangle (all its corners in front of the eye and past the near
    // plane, and its nearest corner farther than the pyramid's farthest depth over the 2x2 texels
    // of the finest mip whose texels cover the rectangle). A box that crosses the near plane, leaves
    // the image, or needs a mip coarser than the last is visible. InTexel reads one pyramid texel.
    bool HiZBoxVisible( const HiZLayout& InLayout, const HiZProjection& InProjection, const glm::vec3& InMin, const glm::vec3& InMax,
        const std::function<float( uint32_t InMip, uint32_t InX, uint32_t InY )>& InTexel );

    // Two-phase occlusion (per member of a cull): phase 1 draws what is in the frustum and visible
    // against last frame's pyramid; phase 2 draws what phase 1 rejected for occlusion and is visible
    // against this frame's pyramid. Never both, and a member visible against this frame's pyramid
    // is always drawn by one of them.
    struct GpuOcclusionPhases
    {
        bool Phase1 = false;
        bool Candidate = false;     // in the frustum, rejected by phase 1: phase 2 tests it
        bool Phase2 = false;
    };
    inline GpuOcclusionPhases GpuOcclusionClassify( bool InFrustum, bool InVisibleLast, bool InVisibleNow )
    {
        GpuOcclusionPhases result;
        result.Phase1 = InFrustum && InVisibleLast;
        result.Candidate = InFrustum && !result.Phase1;
        result.Phase2 = result.Candidate && InVisibleNow;
        return result;
    }

    // A frame's membership and record bookkeeping, on the CPU (GpuMeshCulling uploads what it
    // decides). Begin, Set for every changed mesh command id (every id when all changed), Finish.
    class GpuCullState
    {
    public:
        enum class Set : uint8_t
        {
            Opaque = 0,
            Shadow = 1,
            Count
        };

        // Forgets every member and record: the next update is a full one.
        void Reset();
        bool IsEmpty() const { return m_recordCount == 0; }

        void Begin( uint32_t InCommandCount, bool InAll );
        // InOpaque / InShadow: the command's batch key in that set, or null when it isn't in it. A
        // command in neither set keeps no record (nothing reads it).
        void SetMesh( uint32_t InId, const MeshBatchRegistry::Key* InOpaque, const MeshBatchRegistry::Key* InShadow, const glm::mat4& InInstance, const AABB& InBounds );
        // If any membership changed, lays both sets out again (InIndexCount: a member's index count),
        // marks who the GPU path draws and who left it; refreshes the batches' bounds. Returns
        // whether the layout changed.
        bool Finish( const std::function<uint32_t( uint32_t InId )>& InIndexCount );
        // Batches below this many members stay on the CPU path (kGpuCullMinBatchMembers).
        void SetMinBatchMembers( uint32_t InMembers ) { m_minMembers = InMembers; m_layoutDirty = true; }

        // This update rewrote everything (Begin with InAll, or the first one).
        bool IsFullUpdate() const { return m_updateAll; }
        // Per mesh command id: in a batch the GPU path draws (opaque colour and depth; shadows).
        const std::vector<uint8_t>& GetOpaqueMask() const { return m_opaqueMask; }
        bool IsShadowMember( uint32_t InId ) const { return InId < m_shadowMask.size() && m_shadowMask[InId] != 0; }
        const std::vector<uint32_t>& GetOpaqueLeft() const { return m_opaqueLeft; }
        const MeshBatchRegistry& GetRegistry( Set InSet ) const { return m_registries[static_cast<size_t>( InSet )]; }
        const GpuCullSetLayout& GetLayout( Set InSet ) const { return m_layouts[static_cast<size_t>( InSet )]; }
        uint32_t GetMemberCount( Set InSet ) const;
        // Both sets' members (opaque first), and per group its batch's index in its set.
        const std::vector<uint32_t>& GetMembers() const { return m_members; }
        const std::vector<uint32_t>& GetGroupBatch() const { return m_groupBatch; }
        // Every record (kGpuCullRecordVec4s per mesh command id), and this update's changed rows
        // (kGpuCullStagingVec4s each) when it wasn't a full one.
        const std::vector<glm::vec4>& GetRecords() const { return m_records; }
        uint32_t GetRecordCount() const { return m_recordCount; }
        const std::vector<glm::vec4>& GetStaging() const { return m_staging; }
        uint32_t GetStagingRows() const { return static_cast<uint32_t>( m_staging.size() / kGpuCullStagingVec4s ); }
        void DropStaging() { m_staging.clear(); }

    private:
        MeshBatchRegistry m_registries[static_cast<size_t>( Set::Count )];
        GpuCullSetLayout m_layouts[static_cast<size_t>( Set::Count )];
        std::vector<uint8_t> m_opaqueMask;
        std::vector<uint8_t> m_shadowMask;
        std::vector<uint32_t> m_opaqueLeft;
        bool m_layoutDirty = true;
        bool m_boundsDirty[static_cast<size_t>( Set::Count )] = { true, true };
        uint32_t m_minMembers = kGpuCullMinBatchMembers;
        bool m_updateAll = false;
        std::vector<glm::vec4> m_records;
        uint32_t m_recordCount = 0;
        std::vector<glm::vec4> m_staging;
        std::vector<uint32_t> m_members;
        std::vector<uint32_t> m_groupBatch;
    };

    // ---- The GPU side (main thread only) ----
    class GpuMeshCulling
    {
    public:
        using Set = GpuCullState::Set;

        GpuMeshCulling();
        ~GpuMeshCulling();
        GpuMeshCulling( const GpuMeshCulling& ) = delete;
        GpuMeshCulling& operator=( const GpuMeshCulling& ) = delete;

        // The four compute programs are loaded.
        bool IsReady() const;

        // Forgets every member and record: the next update must be a full one.
        void Reset() { m_state.Reset(); }
        bool IsEmpty() const { return m_state.IsEmpty(); }

        // A frame's membership and records (GpuCullState): BeginUpdate, SetMesh for every changed id,
        // EndUpdate, which queues the buffer updates and returns true when changed rows wait for
        // DispatchScatter.
        void BeginUpdate( uint32_t InCommandCount, bool InAll ) { m_state.Begin( InCommandCount, InAll ); }
        void SetMesh( uint32_t InId, const MeshBatchRegistry::Key* InOpaque, const MeshBatchRegistry::Key* InShadow, const glm::mat4& InInstance, const AABB& InBounds )
        {
            m_state.SetMesh( InId, InOpaque, InShadow, InInstance, InBounds );
        }
        bool EndUpdate( const std::function<uint32_t( uint32_t InId )>& InIndexCount );
        // Writes the changed rows into the record buffer; InView must come before every cull view.
        void DispatchScatter( bgfx::ViewId InView );

        // The opaque set's members by mesh command id (1 = drawn by the GPU path), and the ids that left
        // it in this frame's update (ascending).
        const std::vector<uint8_t>& GetOpaqueMask() const { return m_state.GetOpaqueMask(); }
        const std::vector<uint32_t>& GetOpaqueLeft() const { return m_state.GetOpaqueLeft(); }
        const GpuCullSetLayout& GetLayout( Set InSet ) const { return m_state.GetLayout( InSet ); }
        bool IsShadowMember( uint32_t InId ) const { return m_state.IsShadowMember( InId ); }
        bool EndUpdateChangedLayout() const { return m_layoutChanged; }
        uint32_t GetMemberCount( Set InSet ) const { return m_state.GetMemberCount( InSet ); }

        // Once a frame, before any Cull: rewinds the output and argument buffers, growing them to
        // what the last frame asked for.
        void BeginFrame();

        // One cull of a set against InViewCount views (InPlanes: six per view, as Frustum stores
        // them), dispatched into InView. Invalid when the programs aren't loaded or this frame's
        // output is full (the caller then draws those meshes on the CPU path; the buffers grow for
        // the next frame). Valid with nothing to draw when the set is empty.
        struct CullResult
        {
            bool Valid = false;
            Set Which = Set::Opaque;
            uint32_t Views = 0;
            uint32_t ArgsBase = 0;
            uint32_t OutBase = 0;
        };
        CullResult Cull( bgfx::ViewId InView, Set InSet, const glm::vec4* InPlanes, uint32_t InViewCount, bool InReverse, bool InShadowMatrices );

        // A (batch, view) of a cull's draw: binds its output as the instance data; then submit with
        // GetDrawArgs() at GetArgsIndex().
        void SetInstances( const CullResult& InCull, uint32_t InView, uint32_t InBatch ) const;
        uint32_t GetArgsIndex( const CullResult& InCull, uint32_t InView, uint32_t InBatch ) const;
        bgfx::IndirectBufferHandle GetDrawArgs() const { return m_drawArgs; }

        // Last frame's dispatches, and culls that fell back to the CPU path.
        uint32_t GetDispatchCount() const { return m_lastDispatches; }
        uint32_t GetFallbackCount() const { return m_lastFallbacks; }

        // ---- Occlusion culling, two-phase (ME-400; opaque set, main cameras) ----

        // A camera's depth pyramid and the projection it was built with, kept across frames. Keyed by
        // the camera's target (AcquireHistory); dropped when unused for a few seconds.
        struct HiZHistory
        {
            HiZLayout Layout;
            bgfx::TextureHandle Textures[kHiZMaxTextures] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
            HiZProjection Projection;
            bool Orthographic = false;
            uint64_t BuiltFrame = 0;    // this object's frame count when it was last built (0: never)
            uint64_t UsedFrame = 0;
        };

        // The occlusion programs are loaded and R32F images can be written (the backend's caps).
        bool IsOcclusionReady() const;
        // A camera target's pyramid at this depth size: its textures are (re)made when the size
        // changes, which forgets the history. Null when the size needs more than kHiZMaxTextures.
        HiZHistory* AcquireHistory( const void* InKey, uint32_t InDepthWidth, uint32_t InDepthHeight );
        // It holds last frame's pyramid, made with the same kind of projection.
        bool HasHistory( const HiZHistory& InHistory, bool InOrthographic ) const;

        // Phase 1: the opaque set against InPlanes (one view) and last frame's pyramid (InHistory).
        // Reserves phase 2's output with it, so phase 2 can't run out of room once phase 1 has
        // rejected anything. Invalid as Cull is (the caller draws on the CPU path).
        struct OcclusionCull
        {
            CullResult Phase1;
            uint32_t Phase2OutBase = 0;
            uint32_t Phase2ArgsBase = 0;
        };
        OcclusionCull CullOcclusion( bgfx::ViewId InView, const glm::vec4* InPlanes, bool InReverse, const HiZHistory& InHistory );
        // This frame's pyramid from InDepth (the depth prepass), one pass per view of InViews
        // (GetPasses() of them, after the prepass, before phase 2). False when it couldn't be built.
        bool BuildPyramid( HiZHistory& InOutHistory, const bgfx::ViewId* InViews, bgfx::TextureHandle InDepth, const HiZProjection& InProjection, bool InOrthographic );
        // Phase 2: phase 1's rejects against this frame's pyramid (null: no pyramid was built, so
        // every reject is drawn). In a view after the pyramid's; drawn after phase 1's draws.
        CullResult CullOcclusionPhase2( bgfx::ViewId InView, const OcclusionCull& InPhase1, bool InReverse, const HiZHistory* InHistory );
        // A check of phase 2 (--occlusion-phase2-only): phase 1 rejects every mesh, so phase 2 draws
        // everything visible; the image must match the frustum-only path's.
        void SetPhase2Only( bool InOnly ) { m_phase2Only = InOnly; }
        // Pyramids built last frame (for the inspector).
        uint32_t GetPyramidCount() const { return m_lastPyramids; }

    private:
        void DestroyBuffers();
        void DestroyHistory( HiZHistory& InOutHistory );
        // The three dispatches of a cull into regions already taken (InOcclusion: the occlusion
        // programs with u_gpuCullOcc and the pyramid bound).
        void DispatchCull( bgfx::ViewId InView, Set InSet, const glm::vec4* InPlanes, uint32_t InViewCount, bool InReverse, bool InShadowMatrices, uint32_t InOutBase, uint32_t InArgsBase,
            const glm::vec4* InOcclusion, const HiZHistory* InPyramid );

        SharedPtr<ProgramRef> m_occlusionCount;
        SharedPtr<ProgramRef> m_occlusionWrite;
        SharedPtr<ProgramRef> m_pyramid;
        bgfx::UniformHandle u_gpuCullOcc = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_hizBuild = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_hizSource = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_hiz[kHiZMaxTextures] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
        bgfx::TextureHandle m_hizDummy = BGFX_INVALID_HANDLE;    // bound for missing pyramid textures
        // Per opaque member: (visible, phase 2 candidate). Written by the occlusion count pass,
        // read by its write pass and by phase 2.
        bgfx::DynamicVertexBufferHandle m_occlusionState = BGFX_INVALID_HANDLE;
        uint32_t m_occlusionStateCapacity = 0;
        std::unordered_map<const void*, HiZHistory> m_histories;
        uint64_t m_frame = 0;
        bool m_phase2Only = false;
        uint32_t m_pyramids = 0;
        uint32_t m_lastPyramids = 0;

        GpuCullState m_state;
        bool m_layoutChanged = false;
        SharedPtr<ProgramRef> m_count;
        SharedPtr<ProgramRef> m_offsets;
        SharedPtr<ProgramRef> m_write;
        SharedPtr<ProgramRef> m_scatter;
        bgfx::UniformHandle u_gpuCull = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_gpuCullScatter = BGFX_INVALID_HANDLE;

        bgfx::DynamicVertexBufferHandle m_recordBuffer = BGFX_INVALID_HANDLE;
        uint32_t m_recordCapacity = 0;
        bgfx::DynamicVertexBufferHandle m_stagingBuffer = BGFX_INVALID_HANDLE;
        uint32_t m_stagingCapacity = 0;
        uint32_t m_stagingRows = 0;         // waiting for DispatchScatter
        bgfx::DynamicIndexBufferHandle m_memberBuffer = BGFX_INVALID_HANDLE;
        uint32_t m_memberCapacity = 0;
        bgfx::DynamicVertexBufferHandle m_groupBatchBuffer = BGFX_INVALID_HANDLE;
        uint32_t m_groupCapacity = 0;
        bgfx::DynamicVertexBufferHandle m_batchInfoBuffer = BGFX_INVALID_HANDLE;
        uint32_t m_batchCapacity = 0;
        // Pass scratch (reused by every cull: dispatches run in order).
        bgfx::DynamicVertexBufferHandle m_groupScratch = BGFX_INVALID_HANDLE;
        uint32_t m_groupScratchCapacity = 0;
        bgfx::DynamicVertexBufferHandle m_totals = BGFX_INVALID_HANDLE;
        uint32_t m_totalsCapacity = 0;

        // Per frame: culled instances (64-byte matrices) and draw arguments, handed out per cull.
        bgfx::DynamicVertexBufferHandle m_instances = BGFX_INVALID_HANDLE;
        uint32_t m_instanceCapacity = 0;
        uint32_t m_instanceUsed = 0;
        uint32_t m_instanceDemand = 0;
        bgfx::IndirectBufferHandle m_drawArgs = BGFX_INVALID_HANDLE;
        uint32_t m_argsCapacity = 0;
        uint32_t m_argsUsed = 0;
        uint32_t m_argsDemand = 0;
        uint32_t m_dispatches = 0;
        uint32_t m_fallbacks = 0;
        uint32_t m_lastDispatches = 0;
        uint32_t m_lastFallbacks = 0;
    };
}
