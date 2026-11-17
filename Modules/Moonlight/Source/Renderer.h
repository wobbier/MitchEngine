#pragma once
#include <unordered_map>
#include <Math/Vector2.h>
#include "Camera/CameraData.h"
#include <queue>
#include "Device/FrameBuffer.h"
#include <RenderCommands.h>
#include "Graphics/Texture.h"
#include "Graphics/ShaderCommand.h"
#include <Dementia.h>

#include <Debug/DebugDrawer.h>

class RenderDocManager;
#include <Utils/CommandCache.h>
#include <MeshBatchRegistry.h>
#include <array>
#include "Core/ISystem.h"
#include "Math/Vector4.h"
#include "Core/FrameRenderData.h"
#include <RenderViews.h>
#include "Utils/BGFXCallback.h"
#include "Culling/GpuMeshCulling.h"

class ImGuiRenderer;

namespace Moonlight {
    class DynamicSky;
    class PickingPass;
    class PostProcess;
    class ClusterBuilder;
    class EnvironmentLighting;
    class GpuParticleSimulator;
    class GpuParticleSorter;
    class GpuParticleSortTarget;
}

struct RendererCreationSettings
{
    void* WindowPtr = nullptr;
    void* DisplayPtr = nullptr;
    bgfx::NativeWindowHandleType::Enum WindowType = bgfx::NativeWindowHandleType::Default;
    Vector2 InitialSize = Vector2( 1280.f, 720.f );
    bool InitAssets = true;
    // The backend; Count keeps the platform's own. Noop: headless (dedicated servers, no window).
    bgfx::RendererType::Enum Type = bgfx::RendererType::Count;
};

enum class ViewportMode : uint8_t
{
    Game = 0,
    Editor,
    Count
};

class BGFXRenderer
    : public ISystem
{
    static constexpr bgfx::ViewId kClearView = Moonlight::RenderView::Clear;
    static constexpr std::size_t kMeshTransparencyTempSize = 128;
    friend class RenderCore;
public:
    ME_SYSTEM_ID( BGFXRenderer );

	BGFXRenderer();
	~BGFXRenderer();
    // move me, dummy :D 
    Vector4 m_time;

    void Create( const RendererCreationSettings& settings );
    void Destroy();

#if USING( ME_IMGUI )
    void BeginFrame( const Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId );
#endif

    void Render( Moonlight::CameraData& EditorCamera, FrameRenderData& inFrameData );
    void SetGuizmoDrawCallback( std::function<void( DebugDrawer* )> GuizmoDrawingFunc );
    // A split screen camera (partial Viewport) draws its final image into its rect of displayBuffer
    // (or the backbuffer) instead of its own target.
    void RenderCameraView( Moonlight::CameraData& camera, bool toBackbuffer, Moonlight::FrameBuffer* displayBuffer = nullptr );
    void SubmitDebugLines( const Moonlight::CameraData& camera, bgfx::ViewId id );
    void GatherFrameStats();

    // The mesh draws. InPrepassView: the camera's depth prepass view (UINT16_MAX = none). A draw
    // that qualifies (BindMeshDrawState) is also submitted there, depth only, and its colour draw
    // in view id then tests against that depth instead of writing its own.
    void RenderSingleMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state, bgfx::ViewId InPrepassView = UINT16_MAX );

    void RenderMeshInstanced( bgfx::ViewId id, const Moonlight::MeshCommand& representative, const glm::mat4* transforms, uint32_t count, uint64_t state, bgfx::ViewId InPrepassView = UINT16_MAX );
    // A mesh's entry in an instance buffer: its transform, with MeshCommand::InstanceData in the
    // spare bottom row (column c's w) when the material draws with the stock Standard program,
    // whose vertex shader reads it and restores the row to (0, 0, 0, 1). Everything else, and
    // every shadow, picking or overdraw draw, gets the transform as it is.
    glm::mat4 InstanceMatrix( const Moonlight::MeshCommand& mesh ) const;
    // One skinned mesh: the material's skinned program, the weights stream and the bone palette.
    void RenderSkinnedMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state, bgfx::ViewId InPrepassView = UINT16_MAX );
    // Overdraw view: any mesh (static, instanced or skinned) as one additive layer.
    void RenderOverdrawMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh );
    // Per-object outline: the mesh inflated along its normals with the opposite cull (Outline component).
    void RenderOutlineHull( bgfx::ViewId id, const Moonlight::MeshCommand& mesh );
    // Per-view mesh triangle counts for the frame stats (bgfx only reports frame totals).
    void CountTriangles( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint32_t instances = 1 );
    std::vector<uint64_t> m_viewTriangles;

    void WindowResized( const Vector2& newSize );

    uint32_t GetResetFlags() const;

    // This frame's lights (RenderCore fills it every update from Light components).
    std::vector<Moonlight::LightCommand>& GetLights() { return m_lights; }
    // This frame's decals (RenderCore fills it every update from Decal components).
    std::vector<Moonlight::DecalCommand>& GetDecals() { return m_decals; }
    // This frame's sprites (SpriteCore fills them every update; LOOK-S2).
    std::vector<Moonlight::SpriteCommand>& GetSprites() { return m_sprites; }
    // This frame's particles, one batch per system (ParticleCore fills them every update).
    std::vector<Moonlight::ParticleBatch>& GetParticleBatches() { return m_particleBatches; }

    // Caches
    CommandCache<Moonlight::CameraData>& GetCameraCache();
    CommandCache<Moonlight::MeshCommand>& GetMeshCache();
    // Changes whenever ClearMeshes drops every command (holders of mesh command IDs push again).
    uint32_t GetMeshCacheGeneration() const { return m_meshCacheGeneration; }
    // Counts Render calls; the one in progress (or last finished) has this index, the next one +1.
    uint64_t GetFrameIndex() const { return m_frameIndex; }
    CommandCache<Moonlight::DebugColliderCommand>& GetDebugDrawCache();

    // Retained mesh commands (ME-390). Mesh cache ids are stable per mesh: RenderCore rewrites a
    // command only when something it is built from changed, and every write (Push, Pop, Update,
    // or MarkChanged after an in-place edit) marks the id in the cache. Once a frame, at the start
    // of Render's lighting setup, the renderer takes those marks: GetMeshChanges() lists the ids
    // changed since the previous frame (ascending, valid until the next frame);
    // AreAllMeshesChanged() means treat every id as changed (the cache was cleared, or retention
    // is off). A command's Visible flag is rewritten in place every frame and is not a change:
    // it follows the cameras. The shadow casters and their batches update from this list.
    const std::vector<uint32_t>& GetMeshChanges() const { return m_meshChanges; }
    bool AreAllMeshesChanged() const { return m_allMeshesChanged; }
    // Off (--no-retained-meshes, or the RenderCore inspector): every mesh command is rebuilt and
    // every shadow caster re-gathered each frame, as before ME-390. The A/B switch.
    bool RetainMeshCommands = true;
    // Debug (--verify-retained-meshes): RenderCore rebuilds every retained command anyway and logs
    // any field the retained one got wrong; the renderer checks its casters the same way.
    bool VerifyRetainedMeshes = false;
    // The shadow batch registry: stable batch ids and membership by mesh command id.
    const Moonlight::MeshBatchRegistry& GetShadowBatches() const { return m_shadowBatchRegistry; }

    // GPU-driven culling and indirect draws (ME-389): retained static opaque instanced meshes and
    // their shadow casters are culled per camera, cascade and local shadow view in compute and drawn
    // with indirect draws; everything else stays on the CPU path. Needs compute, draw indirect and
    // instancing, and retained mesh commands; --no-gpu-culling (or the RenderCore inspector) turns
    // it off, and the CPU path draws everything as before.
    bool EnableGpuCulling = true;
    // On this frame (EnableGpuCulling, and everything it needs is there).
    bool IsGpuCullingActive() const { return m_gpuCullingActive; }
    // The mesh command ids the GPU path draws in camera views (one byte per id; empty when it draws
    // none). RenderCore leaves them out of the cameras' VisibleMeshes.
    const std::vector<uint8_t>& GetGpuCulledMeshes() const;
    // The GPU path's members (Opaque or Shadow set) and last frame's dispatches, for the inspector.
    uint32_t GetGpuCulledCount( bool InShadows ) const;
    uint32_t GetGpuCullDispatches() const;
    // Occlusion culling (ME-400) on top of the GPU path: main and editor cameras with a depth prepass
    // also cull the GPU path's opaque meshes against a depth pyramid, in two phases (last frame's
    // pyramid, then this frame's for what that rejected), so nothing pops. --no-occlusion-culling
    // (or the RenderCore inspector) turns it off: frustum culling only, as before.
    bool EnableOcclusionCulling = true;
    // Occlusion runs only while the GPU path's opaque meshes add up to at least this many triangles
    // (--occlusion-min-triangles N; 0: always). Below it the pyramid and the second phase cost more
    // than culling can save: Drumsmith's venue (1.7 M) lost about 0.1 ms with it, the ME-400 city
    // block (54 M) went 6.8x faster on the GPU. Scenes in between are unmeasured.
    uint64_t OcclusionMinTriangles = 4000000;
    // A check (--occlusion-phase2-only): phase 1 draws nothing, phase 2 everything it finds visible.
    bool OcclusionPhase2Only = false;
    // Depth pyramids built last frame (one per camera that culled against one).
    uint32_t GetOcclusionPyramids() const;

    // Meshes
    void UpdateMeshMatrix( unsigned int Id, const glm::mat4& matrix );
    void ClearMeshes();
    SharedPtr<Moonlight::DynamicSky> GetSky();

    void RecreateFrameBuffer( uint32_t index );


    // Settings
    enum MSAALevel
    {
        None,
        X2,
        X4,
        X8,
        X16
    };
    // Requested backbuffer samples. Camera HDR/depth targets are single-sample.
    MSAALevel GetMSAALevel() const;
    void SetMSAALevel( MSAALevel level );
    bool IsVSyncEnabled() const;
    void SetVSyncEnabled( bool enabled );

    // Skips compositing the HTML UI over the main camera (used by unattended captures).
    bool EnableUIComposite = true;

    // Game builds: the main camera renders into its own output, which a final pass stretches over
    // the back buffer, instead of rendering straight into the back buffer. While the window is
    // being resized the camera's size can lag the back buffer's; stretched, the image still fills
    // the window. (The editor always renders the main camera offscreen, for the scene view.)
    bool PresentMainOffscreen = true;

    // Global wind for the foliage shaders (RenderCore gathers it from the scene's WindZone).
    void SetWind( const Moonlight::WindCommand& InWind ) { m_wind = InWind; }
    const Moonlight::WindCommand& GetWind() const { return m_wind; }

    // Cascaded sun shadows (4 cascades in a 2x2 atlas), spot light shadows (up to 4) and point light
    // shadows (up to 2, six cube faces each) sharing the local shadow atlas.
    struct ShadowSettings
    {
        bool Enabled = true;
        uint16_t CascadeResolution = 2048;   // texels per cascade
        uint16_t SpotResolution = 1024;      // texels per spot light
        uint16_t PointResolution = 512;      // texels per cube face of a point light
        bool DebugCascades = false;          // tint the scene by cascade
    };
    ShadowSettings Shadows;

    // Image-based ambient lighting from each camera's background (sky, skybox or clear colour).
    bool EnableEnvironmentLighting = true;
    float EnvironmentIntensity = 1.f;

    // Depth prepass (ME-363): each camera lays down the depth of its opaque Standard surfaces in a
    // view of its own before the opaque view, which then shades every pixel once instead of once
    // per layer. FeatureCaps.DepthPrepass masks it; --no-depth-prepass starts with it off.
    bool EnableDepthPrepass = true;

    // Froxel volumetric fog (ME-376): where compute is active, volumetric fog is lit into a froxel
    // grid and integrated in compute instead of ray marched per pixel. Off by default: it is several
    // times cheaper than the march but softens narrow beams (Rendering-Pipeline.md > Fog);
    // --froxel-fog starts with it on.
    bool EnableFroxelFog = false;

    // Alpha GPU particle systems sort back to front on the GPU per camera (ME-381);
    // --no-gpu-particle-sort starts with it off (they then draw in slot order: a debug/cost switch).
    bool EnableGpuParticleSort = true;

    // What the target hardware can do (an enforced hardware profile sets these). They mask the
    // settings above and every camera's post-processing as frames render, without changing them,
    // so lifting a cap brings the authored look back.
    struct FeatureCapSettings
    {
        bool Shadows = true;
        uint16_t MaxShadowResolution = 0;    // texels per cascade, spot light or point light face (0 = no cap)
        bool EnvironmentLighting = true;
        bool AmbientOcclusion = true;
        bool Bloom = true;
        bool DepthOfField = true;
        bool VolumetricFog = true;
        int MaxVolumetricSteps = 0;          // raymarch samples per pixel (0 = no cap)
        bool TemporalAA = true;
        bool Instancing = true;              // off: every instanced mesh is its own draw call
        bool DepthPrepass = true;            // off: opaque meshes draw once, testing and writing depth themselves
        bool Compute = true;                 // off: systems with a compute path use their non-compute fallback
        float MaxRenderScale = 0.f;          // cameras' render scale ceiling (0 = no cap)
        uint64_t MaxRenderPixels = 0;        // internal pixels per camera (0 = no cap): the profile's resolution
        // The player's graphics settings (ME-275), on every game camera: a scale on its own
        // RenderScale, dynamic resolution forced on, an upscale filter (-1: the camera's).
        float PlayerRenderScale = 1.f;
        bool PlayerDynamicResolution = false;
        int PlayerUpscale = -1;
    };
    FeatureCapSettings FeatureCaps;

    // Compute shaders: the backend supports them (BGFX_CAPS_COMPUTE) and the renderer draws something.
    bool IsComputeSupported() const { return !m_drawsNothing && ( bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE ) != 0; }
    // Supported and not turned off by the Compute feature cap. Every compute user needs a non-compute
    // fallback for when this is false.
    bool IsComputeActive() const { return FeatureCaps.Compute && IsComputeSupported(); }

    // Captures the backbuffer at the end of the current frame and writes it as a PNG.
    void RequestScreenshot( const std::string& filePath );
    uint32_t GetScreenshotCount() const;
    // RenderDoc (ME-61): whether its in-app API loaded (see Debug/RenderDocManager.h for when it
    // does), and a capture of the next frame through it. The engine binds F11 to the capture.
    bool IsRenderDocLoaded() const;
    void CaptureRenderDocFrame();

#if USING( ME_IMGUI )
    ImGuiRenderer* GetImGuiRenderer() const;
#endif

private:
    Moonlight::BGFXCallback m_bgfxCallback;

    // Binds a mesh draw (buffers, textures, lighting, material, render state) and returns the
    // program to submit it with; the caller has already set its transform or instance data.
    // InSkinned: the material's skinned program, with the mesh's weights stream and bone palette.
    // With a prepass view, a draw of the stock Standard programs is submitted to it here first,
    // depth only (InInstances: its instance count, for the statistics).
    // InDepthOnly: only that prepass draw, if the mesh takes part; nothing stays bound and the
    // result is invalid (ME-404: occlusion phase 2's depth, whose colour joins phase 1's draw).
    bgfx::ProgramHandle BindMeshDrawState( const Moonlight::MeshCommand& mesh, uint64_t state, bgfx::ViewId InPrepassView = UINT16_MAX, bool InSkinned = false, uint32_t InInstances = 1, bool InDepthOnly = false );
    // A render texture camera: it has a loaded target texture and isn't the main camera.
    static bool RendersToTexture( const Moonlight::CameraData& InCamera );

    // Compute work that runs once per frame, after the frame's lights are gathered and before any
    // camera view, so every camera reads its results. Each pass allocates its own named view here
    // (so it is ordered before the camera views and shows in a debugger) and does nothing when
    // IsComputeActive() is false; a second compute pass (froxel fog, ...) joins it the same way.
    void RunFrameCompute();
    // GPU particles: dispatches each batch's pending step once (a frame rendered twice doesn't
    // step twice).
    void DispatchGpuParticles();

    // Lighting: per frame (light data, directional lights) and per camera (clusters, ambient).
    void PrepareFrameLighting();
    void PrepareCameraLighting( Moonlight::CameraData& camera );
    void BindLighting();

    // Shadows: atlases, caster passes and the matrices the lighting shader samples with.
    struct ShadowAtlas
    {
        bgfx::TextureHandle Texture = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle Buffer = BGFX_INVALID_HANDLE;
        uint16_t TileSize = 0;
        uint16_t PointTileSize = 0;     // local atlas: point light faces
        uint16_t Width = 0;
        uint16_t Height = 0;
    };
    void EnsureShadowAtlas( ShadowAtlas& atlas, uint16_t tileSize, const char* name );
    // Spot tiles in a 2x2 block, point light faces in a 4x4 block of smaller tiles to its right.
    void EnsureLocalShadowAtlas( uint16_t spotTile, uint16_t pointTile );
    void DestroyShadowAtlas( ShadowAtlas& atlas );
    // InCullView: the camera's GPU cull view (UINT16_MAX: the casters are culled on the CPU only).
    void RenderSunShadows( Moonlight::CameraData& camera, bgfx::ViewId InCullView = UINT16_MAX );
    void RenderLocalShadows();
    // Shadow casters: gathered and grouped into instance batches once a frame, culled against all
    // of a pass's views in one parallel sweep (a bit per view), then submitted view by view.
    void GatherShadowCasters();
    // InSubset: only these caster records (the ones the GPU path doesn't draw); null: all of them.
    void CullShadowCasters( const Frustum* InFrustums, uint32_t InCount, const std::vector<uint32_t>* InSubset = nullptr );
    // The renderer's own depth-only caster: alpha cutoff, tiling and the diffuse/opacity maps.
    void BindStockShadowCaster( const Moonlight::MeshCommand& InMesh, const float InAlpha[4] );
    // The casters of one render group inside pass view InViewIndex (see CullShadowCasters).
    void SubmitShadowCasters( bgfx::ViewId InView, uint32_t InViewIndex, uint8_t InGroup );
    // World -> atlas uv / depth for one tile of a 2x2 atlas.
    glm::mat4 ShadowAtlasMatrix( uint32_t tile, const glm::mat4& viewProjection ) const;
    // World -> atlas uv / depth for a pixel rectangle of an atlas (top-left origin).
    glm::mat4 AtlasRectMatrix( const ShadowAtlas& atlas, uint16_t x, uint16_t y, uint16_t size, const glm::mat4& viewProjection ) const;

    // Shadow settings through the feature caps.
    bool AreShadowsActive() const { return Shadows.Enabled && FeatureCaps.Shadows && m_shadowsSupported; }
    uint16_t CapShadowResolution( uint16_t InResolution ) const;

    // Mesh draws and triangles by who they were for: the game's cameras, the editor's views, and
    // the frame's local shadow passes (shared by every camera). The frame stats report them.
    struct SceneCounts
    {
        uint32_t Draws = 0;
        uint64_t Triangles = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool Rendered = false;
    };
    SceneCounts m_gameCounts;
    SceneCounts m_editorCounts;
    SceneCounts m_frameCounts;
    SceneCounts m_previewCounts;    // other render groups (not reported)
    SceneCounts m_textureCounts;    // render texture cameras (added to the scene's draws)
    // This frame's render texture cameras, in the order they render.
    std::vector<Moonlight::CameraData*> m_textureCameras;
    std::vector<Moonlight::CameraData*> m_overlayCameras;
    SceneCounts* m_counting = nullptr;

    ShadowAtlas m_sunShadowAtlas;
    ShadowAtlas m_spotShadowAtlas;
    Moonlight::ShaderCommand m_shadowProgram;
    Moonlight::ShaderCommand m_shadowSkinnedProgram;
    // Overdraw view: the shadow casters' vertex shaders with an additive fragment.
    Moonlight::ShaderCommand m_overdrawProgram;
    Moonlight::ShaderCommand m_overdrawSkinnedProgram;
    // Depth prepass: the Standard colour programs (to recognise the draws that can take part), and
    // their vertex shaders paired with the depth-only fragment shaders, [skinned][drops fragments].
    Moonlight::ShaderCommand m_standardProgram;
    Moonlight::ShaderCommand m_standardSkinnedProgram;
    Moonlight::ShaderCommand m_depthPrepassPrograms[2][2];
    // Inverted-hull outlines (Outline component).
    Moonlight::ShaderCommand m_outlineHullProgram;
    Moonlight::ShaderCommand m_outlineHullSkinnedProgram;
    bgfx::UniformHandle u_outlineHull = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bones = BGFX_INVALID_HANDLE;
    std::vector<size_t> m_skinnedShadowCasters;
    // What the cull and the batching need of a caster, copied compactly out of its mesh command.
    struct ShadowCaster
    {
        uint32_t Command = 0;               // index into the mesh commands
        uint32_t Batch = UINT32_MAX;        // shadow instance batch; UINT32_MAX = skinned (drawn alone)
        uint64_t MaterialKey = 0;
        uint16_t VertexBuffer = 0;
        uint16_t IndexBuffer = 0;
        uint8_t Group = 0;
        bool Skinned = false;
        bool Retained = false;              // a retained, active command (the GPU path may draw it)
        AABB Bounds;
        // It may be in the GPU-driven path's shadow set (ME-389; GpuShadowKey).
        bool OnGpuPath() const { return Retained && !Skinned && Group == 0; }
    };
    // Whether a mesh command casts, and its record (false: not a caster).
    static bool MakeShadowCaster( const Moonlight::MeshCommand& InMesh, uint32_t InCommand, ShadowCaster& OutCaster );
    // The casters, retained across frames (ME-390): dense and in no particular order (shadow maps
    // only keep depth, so caster order never shows). m_shadowCasterOf maps a mesh command id to its
    // record. Changed commands (the mesh cache's change marks) are re-made each frame; everything
    // is re-made when the cache was cleared, too much changed, or retention is off.
    std::vector<ShadowCaster> m_shadowCasters;
    std::vector<uint32_t> m_shadowCasterOf;
    Moonlight::MeshBatchRegistry m_shadowBatchRegistry;
    std::vector<std::vector<ShadowCaster>> m_shadowRebuildChunks;  // RebuildShadowCasters' scratch
    void RebuildShadowCasters();
    void UpdateShadowCasters();
    void RecomputeCasterBounds();
    // CullShadowCasters' output: per chunk of casters and per view (up to 32), the casters inside
    // it, so a view's submit walks only its own casters.
    static constexpr uint32_t kShadowCullChunk = 2048;
    static constexpr uint32_t kMaxShadowCullViews = 32;
    std::vector<std::vector<uint32_t>> m_shadowCullLists;   // [chunk * kMaxShadowCullViews + view]
    uint32_t m_shadowCullChunks = 0;
    uint32_t m_shadowCullViews = 0;
    // The mesh command ids changed since the last frame (ascending), taken from the mesh cache's
    // change marks once a frame (TakeMeshChanges); m_allMeshesChanged: treat every id as changed.
    std::vector<uint32_t> m_meshChanges;
    bool m_allMeshesChanged = true;
    void TakeMeshChanges();
    // VerifyRetainedMeshes: checks the retained casters against a full rebuild and logs any
    // difference.
    void VerifyShadowCasters();

    // GPU-driven culling (ME-389). Null without compute support.
    UniquePtr<Moonlight::GpuMeshCulling> m_gpuCulling;
    bool m_gpuCullingActive = false;
    bool m_loggedGpuCulling = false;
    // Once a frame after the shadow casters: membership and records from the mesh changes, the
    // record scatter, and the per-frame output.
    void UpdateGpuCulling();
    // The batch keys a command has in the GPU path's sets (false: not in that set).
    bool GpuOpaqueKey( const Moonlight::MeshCommand& InMesh, Moonlight::MeshBatchRegistry::Key& OutKey ) const;
    bool GpuShadowKey( const Moonlight::MeshCommand& InMesh, uint32_t InId, Moonlight::MeshBatchRegistry::Key& OutKey ) const;
    // The caster records the CPU culls while the GPU path draws the rest (rebuilt when casters change).
    const std::vector<uint32_t>& GetCpuShadowCasters();
    std::vector<uint32_t> m_cpuShadowCasters;
    bool m_cpuShadowCastersDirty = true;
    // One GPU-culled shadow view: every shadow batch, indirect, from view InViewIndex of InCull.
    // Batches whose bounds miss InFrustum are not submitted.
    void SubmitGpuShadowCasters( bgfx::ViewId InView, const Moonlight::GpuMeshCulling::CullResult& InCull, uint32_t InViewIndex, const Frustum& InFrustum );
    // A camera's GPU-culled opaque batches (backwards under a prepass): depth from the cull's own
    // arguments, colour from its colour arguments (ME-404: with occlusion, both phases' survivors).
    // InDepthOnly: just the prepass draws (occlusion phase 2, whose meshes phase 1's colour draws
    // include and count).
    void SubmitGpuOpaque( bgfx::ViewId InView, const Moonlight::GpuMeshCulling::CullResult& InCull, uint64_t InState, bgfx::ViewId InPrepassView, const Frustum& InFrustum, bool InDepthOnly = false );
    // The occlusion decision last logged: 0 none yet, 1 on, 2 below OcclusionMinTriangles, 3 unsupported.
    int m_loggedOcclusion = 0;
    // Set while BindMeshDrawState binds an indirect draw: its prepass submit is indirect too.
    bgfx::IndirectBufferHandle m_bindIndirect = BGFX_INVALID_HANDLE;
    uint32_t m_bindIndirectIndex = 0;
    bool m_shadowsSupported = false;
    int m_directionalLightCount = 0;
    bool m_hasSunShadow = false;
    Moonlight::LightCommand m_sunShadowLight;
    // Per render group (cascades extend towards the light to include them).
    std::array<AABB, Moonlight::kMaxRenderGroups> m_casterBounds;
    static constexpr uint32_t kMaxSpotShadows = 4;
    static constexpr uint32_t kMaxPointShadows = 2;
    struct SpotShadow
    {
        glm::mat4 View = glm::mat4( 1.f );
        glm::mat4 Projection = glm::mat4( 1.f );
        uint16_t X = 0;     // atlas pixel rectangle
        uint16_t Y = 0;
        uint16_t Size = 0;
    };
    std::vector<SpotShadow> m_spotShadows;      // spot lights, then point light faces

    std::vector<Moonlight::LightCommand> m_lights;

    // Sprites (RenderPasses/SpritePass.cpp): after transparent meshes, before particles, into
    // HDR colour, depth-tested in the shader like particles.
    void RenderSprites( Moonlight::CameraData& camera );
    std::vector<Moonlight::SpriteCommand> m_sprites;
    std::vector<std::pair<float, uint32_t>> m_spriteOrder;
    uint32_t m_droppedParticles = 0;    // this frame's CPU particle instances that found no transient buffer room (FrameStats::RenderStats::DroppedParticles)
    uint32_t m_spriteBatches = 0;       // this frame's (FrameStats::RenderStats::SpriteBatches)
    Moonlight::ShaderCommand m_spriteProgram;
    bgfx::TextureHandle m_spriteWhite = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texSprite = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texSpriteNormal = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_eraParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_spriteParams = BGFX_INVALID_HANDLE;
    // Particles: drawn after transparent meshes into HDR colour, depth-tested in the shader.
    void RenderParticles( Moonlight::CameraData& camera );
    // Decals onto the opaque scene (before SSAO and fog), lit like the surfaces around them.
    void RenderDecals( Moonlight::CameraData& camera );
    std::vector<Moonlight::ParticleBatch> m_particleBatches;
    std::vector<std::pair<float, uint32_t>> m_particleOrder;
    std::vector<Moonlight::ParticleInstance> m_particleSorted;
    Moonlight::ShaderCommand m_particleProgram;
    bgfx::VertexBufferHandle m_particleQuadVB = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_particleQuadIB = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_particleTexture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_particleParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_particleParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_particleDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texParticle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_sceneDepth = BGFX_INVALID_HANDLE;
    // GPU particle simulation (compute); null when compute isn't supported.
    UniquePtr<Moonlight::GpuParticleSimulator> m_gpuParticles;
    // The per-camera back-to-front sort of Alpha GPU systems (ME-381); null without compute.
    UniquePtr<Moonlight::GpuParticleSorter> m_gpuParticleSorter;
    // Per m_particleOrder entry this camera: the sorted copy an Alpha GPU system draws from.
    std::vector<Moonlight::GpuParticleSortTarget*> m_particleSortTargets;
    std::vector<Moonlight::DecalCommand> m_decals;
    std::vector<const Moonlight::DecalCommand*> m_decalOrder;
    Moonlight::ShaderCommand m_decalProgram;
    bgfx::VertexBufferHandle m_decalBoxVB = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_decalBoxIB = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_decalAlbedo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_decalNormal = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_decalColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_decalEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_decalSurface = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_decalDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_decalTexSize = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_decalWorldToLocal = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_decalAxes = BGFX_INVALID_HANDLE;
    std::vector<Moonlight::LightCommand> m_localLights;
    std::vector<float> m_lightData;
    bgfx::TextureHandle m_lightDataTexture = BGFX_INVALID_HANDLE;
    UniquePtr<Moonlight::ClusterBuilder> m_clusterBuilder;
    struct LightingUniforms
    {
        float ViewShading[4] = {};
        // Era effects on Standard materials (Era.sh, LOOK-S11): snap grid, affine, three-point.
        float EraParams[4] = {};
        bool Wireframe = false;
        bool BackfaceCulling = true;
        float Params[4] = { 0.f, 0.f, 1.f, 0.f };
        float DirectionalDirection[4][4] = {};
        float DirectionalColor[4][4] = {};
        float EclipseSun[4] = {};
        float EclipseMoon[4] = {};
        float EclipseParams[4] = {};
        float ClusterParams[4] = {};
        float ClusterGrid[4] = {};
        float AmbientSky[4] = {};
        float AmbientGround[4] = {};
        bgfx::TextureHandle ClusterGridTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle ClusterIndexTexture = BGFX_INVALID_HANDLE;
        glm::mat4 ShadowMatrix[4] = { glm::mat4( 1.f ), glm::mat4( 1.f ), glm::mat4( 1.f ), glm::mat4( 1.f ) };
        float CascadeSplits[4] = {};
        float CascadeTexel[4] = {};
        float ShadowParams[4] = {};
        glm::mat4 LocalShadowMatrix[16];    // 0-3 spots, 4-15 point light faces
        float SpotShadowParams[4] = {};
        float PointShadowParams[4] = {};
        bgfx::TextureHandle EnvSpecular = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle EnvIrradiance = BGFX_INVALID_HANDLE;
        float EnvParams[4] = {};
        // Fog (Fog.sh), from the camera's post settings; FogForward[0] is 1 while drawing passes
        // after the fog pass (transparents, particles), which fog themselves.
        float FogParams[4] = {};
        float FogColor[4] = {};
        float FogVolume[4] = {};
        float FogForward[4] = {};
    } m_lighting;
    UniquePtr<Moonlight::EnvironmentLighting> m_environment;
    uint64_t m_frameIndex = 0;
    bgfx::UniformHandle u_lightParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_viewShading = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_dirLightDirection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_dirLightColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_eclipseSun = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_eclipseMoon = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_eclipseParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_clusterParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_clusterGrid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ambientSky = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ambientGround = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_lightData = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_clusterGrid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_clusterIndices = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texMetallicRoughness = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texOcclusion = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texMetallic = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texRoughness = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowMatrix = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cascadeSplits = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cascadeTexel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_localShadowMatrix = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_spotShadowParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pointShadowParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowAlpha = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_portalClipPlane = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_drawParams = BGFX_INVALID_HANDLE;     // x: the draw's screen-door fade (ME-279)
    bgfx::UniformHandle u_shadowUvOffset = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_spotShadowMap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_envSpecular = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_envIrradiance = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_brdfLut = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_envParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogVolume = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogForward = BGFX_INVALID_HANDLE;

    struct InstanceBatch
    {
        uint16_t vertexBuffer = 0;
        uint16_t indexBuffer = 0;
        uint64_t materialKey = 0;
        uint8_t group = 0;                  // shadow batches: the casters' render group
        size_t representativeIndex = 0;
        std::vector<glm::mat4> transforms;
        std::vector<uint32_t> commands;     // shadow batches: the casters' mesh command indices
    };
    std::vector<InstanceBatch> m_instanceBatches;
    size_t m_activeBatchCount = 0;
    // The camera's opaque meshes that draw one at a time, in the order its commands are visited.
    struct OpaqueDraw
    {
        uint32_t Command = 0;       // index into the mesh commands
        bool Outgoing = false;      // the level a LOD crossfade fades out, not the mesh's own
    };
    std::vector<OpaqueDraw> m_opaqueDraws;
    std::unordered_map<uint64_t, uint32_t> m_batchLookup;   // key hash -> m_instanceBatches index
    // Shadow batches by m_shadowBatchRegistry id (unused ids have no members).
    std::vector<InstanceBatch> m_shadowBatches;

    Vector2 PreviousSize;
    Vector2 CurrentSize;
    uint32_t m_resetFlags = 0u;
    // Applies CurrentSize and m_resetFlags to the main window. bgfx takes per-surface flags
    // (MSAA, sRGB) on the swap chain and the rest on reset, so this splits m_resetFlags.
    void ResetMainSwapChain();

    CommandCache<Moonlight::CameraData> m_cameraCache;
    CommandCache<Moonlight::MeshCommand> m_meshCache;
    CommandCache<Moonlight::DebugColliderCommand> m_debugDrawCache;
    std::vector<size_t> TransparentIndicies;

    Moonlight::FrameBuffer* EditorCameraBuffer = nullptr;
    // Scene targets for a split screen main camera, whose own buffer stays the full size display.
    Moonlight::FrameBuffer* m_splitMainBuffer = nullptr;
    std::function<void( DebugDrawer* )> m_guizmoCallback;
    bgfx::VertexBufferHandle m_vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_ibh = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle UIProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texDiffuse = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texNormal = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAlpha = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texUI = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_time = BGFX_INVALID_HANDLE;
    // Frame uniforms: xyz wind direction, w strength; x gusts, y gust frequency, z turbulence.
    bgfx::UniformHandle u_wind = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_windParams = BGFX_INVALID_HANDLE;
    Moonlight::WindCommand m_wind;
    uint32_t m_meshCacheGeneration = 0;
    // PortalMaterial: tint rgb, w = 1 while the linked camera's image is bound.
    bgfx::UniformHandle u_screenView = BGFX_INVALID_HANDLE;
    // The target of the camera view being drawn: a material cannot sample it (PortalMaterial).
    const Moonlight::FrameBuffer* m_drawingBuffer = nullptr;
    int32_t m_pt;
    int64_t m_timeOffset;
    Moonlight::CameraData DummyCameraData;
    SharedPtr<Moonlight::Texture> m_defaultOpacityTexture;
    // Loaded at startup and kept cached for the renderer's lifetime (PrefetchShaders).
    std::vector<SharedPtr<Moonlight::ShaderFile>> m_engineShaders;
    // 1x1 fallbacks bound when a material has no texture in a slot.
    bgfx::TextureHandle m_whiteTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_blackTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_flatNormalTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_grayTexture = BGFX_INVALID_HANDLE;
    SharedPtr<Moonlight::DynamicSky> m_dynamicSky;
    bool EnableDebugDraw = false;
    UniquePtr<DebugDrawer> m_debugDraw;
    DebugDraw::FrameLines m_debugLines;
    Moonlight::ViewAllocator m_views;
    UniquePtr<Moonlight::PostProcess> m_postProcess;
    bool NeedsReset = false;
    // Created with InitAssets (false for tool windows, which only draw ImGui).
    bool m_hasAssets = true;
    // The Noop backend (headless servers): frames only advance bgfx, nothing is prepared (ME-286).
    bool m_drawsNothing = false;
    uint32_t m_currentFrame = 0;

#if USING( ME_ENABLE_RENDERDOC )
    RenderDocManager* RenderDoc = nullptr;
#endif

#if USING( ME_IMGUI )
    ImGuiRenderer* ImGuiRender = nullptr;
#endif

#if USING( ME_EDITOR )
    SharedPtr<Moonlight::PickingPass> m_pickingPass;
#endif

public:
    void SetDebugDrawEnabled( bool inEnabled );
};
