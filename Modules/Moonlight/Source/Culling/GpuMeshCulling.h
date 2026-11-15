#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
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

    private:
        void DestroyBuffers();

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
