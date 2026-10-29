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

#if USING( ME_ENABLE_RENDERDOC )
#include <Debug/RenderDocManager.h>
#endif
#include <Debug/DebugDrawer.h>
#include <Utils/CommandCache.h>
#include <array>
#include "Core/ISystem.h"
#include "Math/Vector4.h"
#include "Core/FrameRenderData.h"
#include <RenderViews.h>
#include "Utils/BGFXCallback.h"

class ImGuiRenderer;

namespace Moonlight {
    class DynamicSky;
    class PickingPass;
    class PostProcess;
    class ClusterBuilder;
    class EnvironmentLighting;
}

struct RendererCreationSettings
{
    void* WindowPtr = nullptr;
    void* DisplayPtr = nullptr;
    bgfx::NativeWindowHandleType::Enum WindowType = bgfx::NativeWindowHandleType::Default;
    Vector2 InitialSize = Vector2( 1280.f, 720.f );
    bool InitAssets = true;
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

    void RenderSingleMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state );

    void RenderMeshInstanced( bgfx::ViewId id, const Moonlight::MeshCommand& representative, const glm::mat4* transforms, uint32_t count, uint64_t state );
    // One skinned mesh: the material's skinned program, the weights stream and the bone palette.
    void RenderSkinnedMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state );
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
    // This frame's particles, one batch per system (ParticleCore fills them every update).
    std::vector<Moonlight::ParticleBatch>& GetParticleBatches() { return m_particleBatches; }

    // Caches
    CommandCache<Moonlight::CameraData>& GetCameraCache();
    CommandCache<Moonlight::MeshCommand>& GetMeshCache();
    // Changes whenever ClearMeshes drops every command (holders of mesh command IDs push again).
    uint32_t GetMeshCacheGeneration() const { return m_meshCacheGeneration; }
    CommandCache<Moonlight::DebugColliderCommand>& GetDebugDrawCache();

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
        bool TemporalAA = true;
        bool Instancing = true;              // off: every instanced mesh is its own draw call
    };
    FeatureCapSettings FeatureCaps;

    // Captures the backbuffer at the end of the current frame and writes it as a PNG.
    void RequestScreenshot( const std::string& filePath );
    uint32_t GetScreenshotCount() const;

#if USING( ME_IMGUI )
    ImGuiRenderer* GetImGuiRenderer() const;
#endif

private:
    Moonlight::BGFXCallback m_bgfxCallback;

    bgfx::ProgramHandle BindMeshDrawState( const Moonlight::MeshCommand& mesh, uint64_t state );

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
    void RenderSunShadows( Moonlight::CameraData& camera );
    void RenderLocalShadows();
    // Shadow casters: gathered and grouped into instance batches once a frame, culled against all
    // of a pass's views in one parallel sweep (a bit per view), then submitted view by view.
    void GatherShadowCasters();
    void CullShadowCasters( const Frustum* InFrustums, uint32_t InCount );
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
    SceneCounts* m_counting = nullptr;

    ShadowAtlas m_sunShadowAtlas;
    ShadowAtlas m_spotShadowAtlas;
    Moonlight::ShaderCommand m_shadowProgram;
    Moonlight::ShaderCommand m_shadowSkinnedProgram;
    // Overdraw view: the shadow casters' vertex shaders with an additive fragment.
    Moonlight::ShaderCommand m_overdrawProgram;
    Moonlight::ShaderCommand m_overdrawSkinnedProgram;
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
        AABB Bounds;
    };
    // Casters by chunk of mesh commands (gathered in parallel); a chunk's views start at its offset.
    std::vector<std::vector<ShadowCaster>> m_shadowCasterChunks;
    std::vector<uint32_t> m_shadowCasterChunkOffsets;
    uint32_t m_shadowCasterChunkCount = 0;
    uint32_t m_shadowCasterCount = 0;
    std::vector<uint32_t> m_shadowCasterViews;  // per caster: bit v = inside the pass's view v
    std::unordered_map<uint64_t, uint32_t> m_shadowBatchLookup;
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
    std::vector<InstanceBatch> m_shadowBatches;
    size_t m_activeShadowBatchCount = 0;

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
    uint32_t m_currentFrame = 0;

#if USING( ME_ENABLE_RENDERDOC )
    RenderDocManager* RenderDoc;
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
