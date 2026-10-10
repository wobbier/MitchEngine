#include <Renderer.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <ImGui/ImGuiRenderer.h>
#include "optick.h"
#include "imgui.h"
#include "Utils/BGFXUtils.h"
#include "bx/timer.h"
#include <bx/string.h>
#include "Graphics/Material.h"
#include "Graphics/ShaderStructures.h"
#include "Primitives/Cube.h"
#include "Graphics/MeshData.h"
#include "Graphics/ShaderCommand.h"
#include <algorithm>
#include <cstring>
#include "Resource/ResourceCache.h"
#include <Graphics/SkyBox.h>
#include <Math/Matrix4.h>
#include <Graphics/DynamicSky.h>
#include <Graphics/ModelResource.h>
#include <Debug/DebugDrawer.h>
#include <stack>
#include <Mathf.h>
#include "Core/Assert.h"
#include "RenderPasses/PickingPass.h"
#include "RenderPasses/PostProcess.h"
#include "Lighting/ClusterBuilder.h"
#include "Lighting/ShadowCascades.h"
#include "Lighting/EnvironmentLighting.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Primitives/Primitives.h"
#include "Profiling/FrameStats.h"

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
//#include <bgfx/embedded_shader.h>
//
//// embedded shaders
//#include "Graphics/cubes_frag.h"
//#include "Graphics/cubes_vert.h"

static bool s_showStats = false;

namespace
{
    static const char* s_ptNames[]
    {
        "Triangle List",
        "Triangle Strip",
        "Lines",
        "Line Strip",
        "Points",
    };

    static const uint64_t s_ptState[]
    {
        UINT64_C( 0 ),
        BGFX_STATE_PT_TRISTRIP,
        BGFX_STATE_PT_LINES,
        BGFX_STATE_PT_LINESTRIP,
        BGFX_STATE_PT_POINTS,
    };
    static_assert( BX_COUNTOF( s_ptState ) == BX_COUNTOF( s_ptNames ) );
}


//static const bgfx::EmbeddedShader s_embeddedShaders[] =
//{
//	BGFX_EMBEDDED_SHADER(cubes_vert),
//	BGFX_EMBEDDED_SHADER(cubes_frag),

//	BGFX_EMBEDDED_SHADER_END()
//};


/*static const uint64_t s_ptState[]
{
    UINT64_C(0),
    BGFX_STATE_PT_TRISTRIP,
    BGFX_STATE_PT_LINES,
    BGFX_STATE_PT_LINESTRIP,
    BGFX_STATE_PT_POINTS,
};*/


void BGFXRenderer::Create( const RendererCreationSettings& settings )
{
    OPTICK_EVENT( "BGFXRenderer::Create" );
    PreviousSize = settings.InitialSize;
    // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
    // Most graphics APIs must be used on the same thread that created the window.
    bgfx::renderFrame();
    // Initialize bgfx using the native window handle and window resolution.
    bgfx::Init init;
    init.platformData.nwh  = settings.WindowPtr;
    init.platformData.ndt  = settings.DisplayPtr;
    init.platformData.type = settings.WindowType;
    init.callback = &m_bgfxCallback;
    init.resolution.width  = static_cast<uint32_t>( PreviousSize.x );
    init.resolution.height = static_cast<uint32_t>( PreviousSize.y );
#if USING( ME_PLATFORM_MACOS )
    init.resolution.reset = BGFX_RESET_VSYNC;
#else
    init.resolution.reset = BGFX_RESET_NONE;// | BGFX_RESET_MSAA_X16;
#endif

#if USING( ME_PLATFORM_UWP )
    // Something is up with using DX12 and my shaders, so this is the fix for now.
    init.type = bgfx::RendererType::Direct3D11;
#elif USING( ME_PLATFORM_LINUX )
    init.type = bgfx::RendererType::Vulkan;
#endif
    m_resetFlags = init.resolution.reset;
    CurrentSize = settings.InitialSize;
#if USING( ME_ENABLE_RENDERDOC )
    RenderDoc = new RenderDocManager();
#endif
    {
        OPTICK_EVENT( "BGFXRenderer::bgfx_init" );
        if( !bgfx::init( init ) )
        {
            CLog::Log( CLog::LogType::Error, "BGFX Failed to Init." );
            return;
        }
    }

    // Set view 0 clear state.
    bgfx::setViewClear( kClearView
        , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        , 0x303030ff
        , 1.0f
        , 0
    );

    if( settings.InitAssets )
    {
        EditorCameraBuffer = new Moonlight::FrameBuffer( init.resolution.width, init.resolution.height );
        EditorCameraBuffer->ReCreate( m_resetFlags );

        m_debugDraw.reset( new DebugDrawer() );
        //m_debugDraw->End();
        //bgfx::setViewClear(1
        //	, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        //	, 0x303030ff
        //	, 1.0f
        //	, 0
        //);
        // Create vertex stream declaration.
        Moonlight::PosColorVertex::Init();
        Moonlight::PosNormTexTanBiVertex::Init();
        Moonlight::SkinWeightsVertex::Init();
        Moonlight::PosTexCoordVertex::Init();
        Moonlight::Vertex_2f_4ub_2f::Init();
        Moonlight::Vertex_2f_4ub_2f_2f_28f::Init();

        // Create static vertex buffer.
        m_vbh = bgfx::createVertexBuffer(
            // Static data can be passed with bgfx::makeRef
            bgfx::makeRef( Moonlight::s_cubeVertices, sizeof( Moonlight::s_cubeVertices ) )
            , Moonlight::PosColorVertex::ms_layout
        );

        // Create static index buffer for triangle list rendering.
        m_ibh = bgfx::createIndexBuffer(
            // Static data can be passed with bgfx::makeRef
            bgfx::makeRef( Moonlight::s_cubeTriList, sizeof( Moonlight::s_cubeTriList ) )
        );
        OPTICK_EVENT( "BGFXRenderer::LoadShaders" );
        UIProgram = Moonlight::LoadProgram( "Assets/Shaders/UI.vert", "Assets/Shaders/UI.frag" );
        s_texDiffuse = bgfx::createUniform( "s_texDiffuse", bgfx::UniformType::Sampler );
        s_texNormal = bgfx::createUniform( "s_texNormal", bgfx::UniformType::Sampler );
        s_texAlpha = bgfx::createUniform( "s_texAlpha", bgfx::UniformType::Sampler );
        s_texUI = bgfx::createUniform( "s_texUI", bgfx::UniformType::Sampler );
        // Per-frame uniforms apply to every draw in the frame regardless of view/sort order.

        m_timeOffset = bx::getHPCounter();

        m_dynamicSky = MakeShared<Moonlight::DynamicSky>( 32, 32 );

        m_dynamicSky->m_sun.Update( 0 );
        m_defaultOpacityTexture = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Textures/DefaultAlpha.png" ) );

        auto makeSolidTexture = []( uint32_t rgba, const char* name ) {
            const bgfx::TextureHandle handle = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, bgfx::copy( &rgba, sizeof( rgba ) ) );
            bgfx::setName( handle, name );
            return handle;
        };
        // Little-endian RGBA8: 0xAABBGGRR.
        m_whiteTexture = makeSolidTexture( 0xFFFFFFFFu, "Default White" );
        m_blackTexture = makeSolidTexture( 0xFF000000u, "Default Black" );
        m_flatNormalTexture = makeSolidTexture( 0xFFFF8080u, "Default Flat Normal" );

#if USING( ME_EDITOR )
        m_pickingPass = MakeShared<Moonlight::PickingPass>();
#endif
        m_postProcess = MakeUnique<Moonlight::PostProcess>();

        // Lighting resources (see PrepareFrameLighting / PrepareCameraLighting).
        m_clusterBuilder = MakeUnique<Moonlight::ClusterBuilder>();
        m_lightDataTexture = bgfx::createTexture2D( 2048, 1, false, 1, bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        bgfx::setName( m_lightDataTexture, "Light Data" );
        m_lightData.assign( 2048 * 4, 0.f );
        u_lightParams = bgfx::createUniform( "u_lightParams", bgfx::UniformType::Vec4 );
        u_dirLightDirection = bgfx::createUniform( "u_dirLightDirection", bgfx::UniformType::Vec4, 4 );
        u_dirLightColor = bgfx::createUniform( "u_dirLightColor", bgfx::UniformType::Vec4, 4 );
        u_clusterParams = bgfx::createUniform( "u_clusterParams", bgfx::UniformType::Vec4 );
        u_clusterGrid = bgfx::createUniform( "u_clusterGrid", bgfx::UniformType::Vec4 );
        u_ambientSky = bgfx::createUniform( "u_ambientSky", bgfx::UniformType::Vec4 );
        u_ambientGround = bgfx::createUniform( "u_ambientGround", bgfx::UniformType::Vec4 );
        s_lightData = bgfx::createUniform( "s_lightData", bgfx::UniformType::Sampler );
        s_clusterGrid = bgfx::createUniform( "s_clusterGrid", bgfx::UniformType::Sampler );
        s_clusterIndices = bgfx::createUniform( "s_clusterIndices", bgfx::UniformType::Sampler );
        s_texMetallicRoughness = bgfx::createUniform( "s_texMetallicRoughness", bgfx::UniformType::Sampler );
        s_texEmissive = bgfx::createUniform( "s_texEmissive", bgfx::UniformType::Sampler );
        s_texOcclusion = bgfx::createUniform( "s_texOcclusion", bgfx::UniformType::Sampler );

        // Shadows (see RenderSunShadows / RenderLocalShadows).
        u_shadowMatrix = bgfx::createUniform( "u_shadowMatrix", bgfx::UniformType::Mat4, 4 );
        u_cascadeSplits = bgfx::createUniform( "u_cascadeSplits", bgfx::UniformType::Vec4 );
        u_cascadeTexel = bgfx::createUniform( "u_cascadeTexel", bgfx::UniformType::Vec4 );
        u_shadowParams = bgfx::createUniform( "u_shadowParams", bgfx::UniformType::Vec4 );
        u_localShadowMatrix = bgfx::createUniform( "u_localShadowMatrix", bgfx::UniformType::Mat4, 16 );
        u_spotShadowParams = bgfx::createUniform( "u_spotShadowParams", bgfx::UniformType::Vec4 );
        u_pointShadowParams = bgfx::createUniform( "u_pointShadowParams", bgfx::UniformType::Vec4 );
        u_shadowAlpha = bgfx::createUniform( "u_shadowAlpha", bgfx::UniformType::Vec4 );
        s_shadowMap = bgfx::createUniform( "s_shadowMap", bgfx::UniformType::Sampler );
        s_spotShadowMap = bgfx::createUniform( "s_spotShadowMap", bgfx::UniformType::Sampler );
        m_shadowProgram = Moonlight::ShaderCommand( "Assets/Shaders/ShadowDepth" );
        m_shadowSkinnedProgram = Moonlight::ShaderCommand( "Assets/Shaders/ShadowDepthSkinned", "Assets/Shaders/ShadowDepth" );
        u_bones = bgfx::createUniform( "u_bones", bgfx::UniformType::Mat4, Moonlight::MeshData::kMaxBones );
        m_shadowsSupported = ( bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL ) != 0 && m_shadowProgram.IsLoaded();
        if( !m_shadowsSupported )
        {
            YIKES( "Shadows disabled: the renderer has no depth compare samplers or the shadow shader failed to load." );
        }
        s_envSpecular = bgfx::createUniform( "s_envSpecular", bgfx::UniformType::Sampler );
        s_envIrradiance = bgfx::createUniform( "s_envIrradiance", bgfx::UniformType::Sampler );
        s_brdfLut = bgfx::createUniform( "s_brdfLut", bgfx::UniformType::Sampler );
        u_envParams = bgfx::createUniform( "u_envParams", bgfx::UniformType::Vec4 );
        m_environment = MakeUnique<Moonlight::EnvironmentLighting>();
        m_environment->Init();

        // Particles: a unit quad, a soft round default texture and the particle shader.
        {
            const Moonlight::PosTexCoordVertex quad[4] = {
                { Vector3( -0.5f, -0.5f, 0.f ), Vector2( 0.f, 1.f ) },
                { Vector3( 0.5f, -0.5f, 0.f ), Vector2( 1.f, 1.f ) },
                { Vector3( 0.5f, 0.5f, 0.f ), Vector2( 1.f, 0.f ) },
                { Vector3( -0.5f, 0.5f, 0.f ), Vector2( 0.f, 0.f ) } };
            const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
            m_particleQuadVB = bgfx::createVertexBuffer( bgfx::copy( quad, sizeof( quad ) ), Moonlight::PosTexCoordVertex::ms_layout );
            m_particleQuadIB = bgfx::createIndexBuffer( bgfx::copy( indices, sizeof( indices ) ) );

            constexpr int kDotSize = 64;
            std::vector<uint8_t> dot( kDotSize * kDotSize * 4 );
            for( int y = 0; y < kDotSize; ++y )
            {
                for( int x = 0; x < kDotSize; ++x )
                {
                    const float dx = ( x + 0.5f ) / kDotSize * 2.f - 1.f;
                    const float dy = ( y + 0.5f ) / kDotSize * 2.f - 1.f;
                    const float falloff = std::clamp( 1.f - ( dx * dx + dy * dy ), 0.f, 1.f );
                    uint8_t* texel = &dot[( y * kDotSize + x ) * 4];
                    texel[0] = texel[1] = texel[2] = 255;
                    texel[3] = static_cast<uint8_t>( falloff * falloff * 255.f );
                }
            }
            m_particleTexture = bgfx::createTexture2D( kDotSize, kDotSize, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy( dot.data(), static_cast<uint32_t>( dot.size() ) ) );
            bgfx::setName( m_particleTexture, "Particle Dot" );
            u_particleParams = bgfx::createUniform( "u_particleParams", bgfx::UniformType::Vec4 );
            u_particleParams2 = bgfx::createUniform( "u_particleParams2", bgfx::UniformType::Vec4 );
            u_particleDepth = bgfx::createUniform( "u_particleDepth", bgfx::UniformType::Vec4 );
            s_texParticle = bgfx::createUniform( "s_texParticle", bgfx::UniformType::Sampler );
            s_sceneDepth = bgfx::createUniform( "s_sceneDepth", bgfx::UniformType::Sampler );
            m_particleProgram = Moonlight::ShaderCommand( "Assets/Shaders/Particle" );
        }
        // The lighting shader always declares the shadow samplers, so the atlases always exist.
        EnsureShadowAtlas( m_sunShadowAtlas, Shadows.CascadeResolution, "Sun Shadow Atlas" );
        EnsureLocalShadowAtlas( Shadows.SpotResolution, Shadows.PointResolution );
    }
    s_time = bgfx::createUniform( "u_time", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4 );
    TransparentIndicies.reserve( kMeshTransparencyTempSize );

#if USING( ME_IMGUI )
    {
        OPTICK_EVENT( "BGFXRenderer::ImGuiCreate" );
        ImGuiRender = new ImGuiRenderer();
        ImGuiRender->Create();
    }
#endif


    bgfx::reset( (uint32_t)CurrentSize.x, (uint32_t)CurrentSize.y, m_resetFlags );
    bgfx::setViewRect( kClearView, 0, 0, bgfx::BackbufferRatio::Equal );
}

BGFXRenderer::BGFXRenderer()
    : m_pt( 0 )
    , m_timeOffset( 0 )
{
}


BGFXRenderer::~BGFXRenderer() = default;


void BGFXRenderer::Destroy()
{
    // Release everything the renderer owns before shutting bgfx down.
#if USING( ME_EDITOR )
    m_pickingPass.reset();
#endif
    m_postProcess.reset();
    Moonlight::Primitives::Shutdown();
    m_dynamicSky.reset();
    m_debugDraw.reset();
    m_defaultOpacityTexture.reset();
    delete EditorCameraBuffer;
    EditorCameraBuffer = nullptr;

    const bgfx::UniformHandle uniforms[] = { s_texDiffuse, s_texNormal, s_texAlpha, s_texUI, s_time,
        u_lightParams, u_dirLightDirection, u_dirLightColor, u_clusterParams, u_clusterGrid, u_ambientSky, u_ambientGround,
        s_lightData, s_clusterGrid, s_clusterIndices, s_texMetallicRoughness, s_texEmissive, s_texOcclusion,
        u_shadowMatrix, u_cascadeSplits, u_cascadeTexel, u_shadowParams, u_localShadowMatrix, u_spotShadowParams, u_pointShadowParams, u_shadowAlpha, s_shadowMap, s_spotShadowMap,
        s_envSpecular, s_envIrradiance, s_brdfLut, u_envParams,
        u_particleParams, u_particleParams2, u_particleDepth, s_texParticle, s_sceneDepth, u_bones };
    m_particleProgram = Moonlight::ShaderCommand();
    for( bgfx::TextureHandle* texture : { &m_particleTexture } )
    {
        if( bgfx::isValid( *texture ) )
        {
            bgfx::destroy( *texture );
        }
    }
    if( bgfx::isValid( m_particleQuadVB ) )
    {
        bgfx::destroy( m_particleQuadVB );
    }
    if( bgfx::isValid( m_particleQuadIB ) )
    {
        bgfx::destroy( m_particleQuadIB );
    }
    if( m_environment )
    {
        m_environment->Destroy();
        m_environment.reset();
    }
    DestroyShadowAtlas( m_sunShadowAtlas );
    DestroyShadowAtlas( m_spotShadowAtlas );
    m_shadowProgram = Moonlight::ShaderCommand();
    m_shadowSkinnedProgram = Moonlight::ShaderCommand();
    if( bgfx::isValid( m_lightDataTexture ) )
    {
        bgfx::destroy( m_lightDataTexture );
    }
    m_clusterBuilder.reset();
    for( const bgfx::UniformHandle& uniform : uniforms )
    {
        if( bgfx::isValid( uniform ) )
        {
            bgfx::destroy( uniform );
        }
    }
    if( bgfx::isValid( UIProgram ) )
    {
        bgfx::destroy( UIProgram );
    }
    if( bgfx::isValid( m_vbh ) )
    {
        bgfx::destroy( m_vbh );
    }
    if( bgfx::isValid( m_ibh ) )
    {
        bgfx::destroy( m_ibh );
    }
    // Shaders referenced by hot-reloadable programs.
    Moonlight::ReleaseProgramRegistry();

    bgfx::shutdown();

#if USING( ME_ENABLE_RENDERDOC )
    delete RenderDoc;
    RenderDoc = nullptr;
#endif
}

#if USING( ME_IMGUI )

void BGFXRenderer::BeginFrame( const Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId )
{
    OPTICK_EVENT( "BGFXRenderer::BeginFrame" );

#if USING( ME_EDITOR )
    bgfx::setViewClear( viewId
        , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        , 0x0f0f0fff
        , 1.0f
        , 0
    );
#endif
    ImGuiRender->NewFrame( mousePosition, mouseButton, scroll, outputSize, inputChar, viewId );
}

#endif

void BGFXRenderer::Render( Moonlight::CameraData& EditorCamera, FrameRenderData& inFrameData )
{
    inFrameData.m_currentFrame = m_currentFrame;
    OPTICK_EVENT( "Renderer::Render", Optick::Category::Rendering );

    // Use debug font to print information about this example.
    bgfx::dbgTextClear();
    // Enable stats or debug text.
    bgfx::setDebug( ( s_showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT ) | ( FrameStats::Get().DetailedGpuTimings ? BGFX_DEBUG_PROFILER : 0 ) );
    // Advance to next frame. Process submitted rendering primitives.

    if( CurrentSize != PreviousSize || NeedsReset )
    {
        PreviousSize = CurrentSize;

        bgfx::reset( (uint32_t)CurrentSize.x, (uint32_t)CurrentSize.y, m_resetFlags );
        //bgfx::setViewRect( kClearView, 0, 0, bgfx::BackbufferRatio::Equal );
        if( NeedsReset )
        {
            for( auto& cam : m_cameraCache.Commands )
            {
                if( cam.Buffer )
                {
                    cam.Buffer->ReCreate( m_resetFlags );
                }
            }
#if USING( ME_EDITOR )
            EditorCamera.Buffer->ReCreate( m_resetFlags );
#endif
        }
        NeedsReset = false;
    }

    bgfx::setFrameUniform( s_time, &m_time.x );

    m_views.Reset();
    ++m_frameIndex;
    DebugDraw::CollectFrame( m_debugLines );
    PrepareFrameLighting();

#if USING( ME_EDITOR )
    EditorCamera.Buffer = EditorCameraBuffer;
    EditorCamera.IsEditorView = true;
    if( EditorCamera.Buffer && EditorCamera.ShouldRender )
    {
        RenderCameraView( EditorCamera, false );
        m_pickingPass->Render( this, &EditorCamera, inFrameData );
    }

    // The editor shows the main camera inside the scene view widget, so it renders to its buffer.
    constexpr bool kMainCameraToBackbuffer = false;
#else
    constexpr bool kMainCameraToBackbuffer = true;
#endif

    // Secondary cameras first so the main camera can sample their output this frame.
    for( auto& camData : m_cameraCache.Commands )
    {
        if( camData.IsMain || !camData.ShouldRender )
        {
            continue;
        }
        RenderCameraView( camData, false );
    }

    for( auto& camData : m_cameraCache.Commands )
    {
        if( camData.IsMain && camData.ShouldRender )
        {
            RenderCameraView( camData, kMainCameraToBackbuffer );
            break;
        }
    }

    {
#if USING( ME_IMGUI )
        ImGuiRender->EndFrame();
#endif
        {
            OPTICK_EVENT( "Renderer::Frame", Optick::Category::Rendering );
            m_currentFrame = bgfx::frame();
            inFrameData.m_currentFrame = m_currentFrame;
        }
    }
    GatherFrameStats();
}


void BGFXRenderer::GatherFrameStats()
{
    const bgfx::Stats* stats = bgfx::getStats();
    FrameStats::RenderStats& out = FrameStats::Get().GetRenderStats();
    if( !stats )
    {
        return;
    }
    const double gpuFrequency = stats->gpuTimerFreq > 0 ? static_cast<double>( stats->gpuTimerFreq ) : 1.0;
    const double cpuFrequency = stats->cpuTimerFreq > 0 ? static_cast<double>( stats->cpuTimerFreq ) : 1.0;
    out.GpuMilliseconds = stats->gpuTimeEnd > stats->gpuTimeBegin ? double( stats->gpuTimeEnd - stats->gpuTimeBegin ) * 1000.0 / gpuFrequency : 0.0;
    out.RenderThreadMilliseconds = double( stats->cpuTimeEnd - stats->cpuTimeBegin ) * 1000.0 / cpuFrequency;
    out.DrawCalls = stats->numDraw;
    out.Triangles = stats->numPrims[bgfx::Topology::TriList] + stats->numPrims[bgfx::Topology::TriStrip];
    out.GpuMemoryUsed = stats->gpuMemoryUsed;
    out.GpuMemoryMax = stats->gpuMemoryMax;
    out.Views.clear();
    for( uint16_t i = 0; i < stats->numViews; ++i )
    {
        const bgfx::ViewStats& view = stats->viewStats[i];
        out.Views.push_back( { view.name, double( view.cpuTimeEnd - view.cpuTimeBegin ) * 1000.0 / cpuFrequency, double( view.gpuTimeEnd - view.gpuTimeBegin ) * 1000.0 / gpuFrequency } );
    }
}


void BGFXRenderer::SetGuizmoDrawCallback( std::function<void( DebugDrawer* )> GuizmoDrawingFunc )
{
    m_guizmoCallback = GuizmoDrawingFunc;
}


void BGFXRenderer::RenderCameraView( Moonlight::CameraData& camera, bool toBackbuffer )
{
    OPTICK_CATEGORY( "Render Camera", Optick::Category::Camera );
    if( CurrentSize.IsZero() )
    {
        return;
    }
    if( !camera.Buffer )
    {
        return;
    }

    // Camera targets match the output size exactly: post passes process whole textures.
    const uint16_t width = static_cast<uint16_t>( std::max( 1.f, camera.OutputSize.x ) );
    const uint16_t height = static_cast<uint16_t>( std::max( 1.f, camera.OutputSize.y ) );
    if( camera.Buffer->Width != width || camera.Buffer->Height != height || !bgfx::isValid( camera.Buffer->SceneBuffer ) )
    {
        camera.Buffer->Width = width;
        camera.Buffer->Height = height;
        camera.Buffer->ReCreate( m_resetFlags );
    }

    PrepareCameraLighting( camera );

    // Ambient: the camera's environment probe (captured and filtered on demand).
    const Moonlight::EnvironmentLighting::Probe* probe = EnableEnvironmentLighting ? m_environment->Prepare( m_views, camera, m_dynamicSky.get(), m_frameIndex ) : nullptr;
    m_lighting.EnvSpecular = probe ? probe->Specular : m_environment->GetFallbackCube();
    m_lighting.EnvIrradiance = probe ? probe->Irradiance : m_environment->GetFallbackCube();
    m_lighting.EnvParams[0] = EnvironmentIntensity;
    m_lighting.EnvParams[1] = static_cast<float>( Moonlight::EnvironmentLighting::kSpecularMips - 1 );
    m_lighting.EnvParams[2] = probe ? 1.f : 0.f;

    if( Shadows.Enabled && m_shadowsSupported )
    {
        RenderSunShadows( camera );
    }
    else
    {
        m_lighting.ShadowParams[3] = 0.f;
    }

    const char* label = camera.IsEditorView ? "Editor" : ( camera.IsMain ? "Main" : "Camera" );
    char viewName[64];
    bx::snprintf( viewName, sizeof( viewName ), "%s Opaque", label );
    const bgfx::ViewId id = m_views.Allocate( viewName );
    if( id == UINT16_MAX )
    {
        return;
    }

    // The scene renders in linear HDR; post-processing writes the final image here.
    const bgfx::FrameBufferHandle target = toBackbuffer ? bgfx::FrameBufferHandle( BGFX_INVALID_HANDLE ) : camera.Buffer->Buffer;
    const uint16_t targetWidth = toBackbuffer ? static_cast<uint16_t>( CurrentSize.x ) : width;
    const uint16_t targetHeight = toBackbuffer ? static_cast<uint16_t>( CurrentSize.y ) : height;

    auto setupSceneView = [&]( bgfx::ViewId view ) {
        bgfx::setViewTransform( view, &camera.View.GetInternalMatrix()[0][0], &camera.ProjectionMatrix.GetInternalMatrix()[0][0] );
        bgfx::setViewFrameBuffer( view, camera.Buffer->SceneBuffer );
        bgfx::setViewRect( view, 0, 0, width, height );
        bgfx::setViewMode( view, bgfx::ViewMode::Sequential );
        bgfx::touch( view );
    };
    setupSceneView( id );

    // Clear colours are authored in sRGB.
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    const uint32_t color = Moonlight::PackClearColor( toLinear( camera.ClearColor.x ), toLinear( camera.ClearColor.y ), toLinear( camera.ClearColor.z ) );
    bgfx::setViewClear( id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, color, 1.0f, 0 );

    if( camera.ClearType == Moonlight::ClearColorType::Procedural )
    {
        m_dynamicSky->Draw( id );
    }
    else if( camera.ClearType == Moonlight::ClearColorType::Skybox && camera.Skybox )
    {
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            //| BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_CULL_CW
            | BGFX_STATE_MSAA
            | s_ptState[m_pt]
            ;

        // Set model matrix for rendering.
        glm::mat4 model = glm::mat4( 1.f );
        model = glm::translate( model, camera.Position.InternalVector );
        model = glm::scale( model, glm::vec3( 10.f, 10.f, 10.f ) );

        bgfx::setTransform( &model[0] );

        // Set vertex and index buffer.
        bgfx::setVertexBuffer( 0, camera.Skybox->SkyModel->GetAllMeshes()[0]->GetVertexBuffer() );
        bgfx::setIndexBuffer( camera.Skybox->SkyModel->GetAllMeshes()[0]->GetIndexuffer() );

        if( const Moonlight::Texture* diffuse = camera.Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse ) )
        {
            if( bgfx::isValid( diffuse->TexHandle ) )
            {
                bgfx::setTexture( 0, s_texDiffuse, diffuse->TexHandle );
            }
        }

        //mesh.MeshMaterial->Use();

        // Set render states.
        bgfx::setState( state );

        // Submit primitive for rendering to view 0.
        bgfx::submit( id, camera.Skybox->SkyMaterial->MeshShader.GetProgram() );
    }

    uint64_t state = 0
        | BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_Z
        | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA
        | s_ptState[m_pt]
        ;

    TransparentIndicies.clear();

    for( size_t b = 0; b < m_activeBatchCount; ++b )
    {
        m_instanceBatches[b].transforms.clear();
    }
    m_activeBatchCount = 0;

    auto getBatch = [this]( uint16_t vbh, uint16_t ibh, uint64_t key, size_t cmdIndex ) -> InstanceBatch& {
        for( size_t b = 0; b < m_activeBatchCount; ++b )
        {
            InstanceBatch& batch = m_instanceBatches[b];
            if( batch.vertexBuffer == vbh && batch.indexBuffer == ibh && batch.materialKey == key )
            {
                return batch;
            }
        }
        if( m_activeBatchCount == m_instanceBatches.size() )
        {
            m_instanceBatches.emplace_back();
        }
        InstanceBatch& batch = m_instanceBatches[m_activeBatchCount++];
        batch.vertexBuffer = vbh;
        batch.indexBuffer = ibh;
        batch.materialKey = key;
        batch.representativeIndex = cmdIndex;
        return batch;
        };

    {
        OPTICK_CATEGORY( "Meshes", Optick::Category::GPU_Scene );
        bool hasCullingInfo = camera.ShouldCull && !camera.VisibleFlags.empty();
        for( size_t i = 0; i < m_meshCache.Commands.size(); ++i )
        {
            const Moonlight::MeshCommand& mesh = m_meshCache.Commands[i];
            if( hasCullingInfo && !camera.VisibleFlags[mesh.VisibilityIndex] )
            {
                continue;
            }

            {
                if( !mesh.MeshMaterial )
                {
                    continue;
                }

                if( mesh.IsTransparent )
                {
                    TransparentIndicies.push_back( i );
                    continue;
                }

                if( mesh.SkinBoneCount > 0 )
                {
                    RenderSkinnedMesh( id, mesh, state );
                    continue;
                }

                if( mesh.SupportsInstancing && mesh.VertexBufferIdx != UINT16_MAX )
                {
                    InstanceBatch& batch = getBatch( mesh.VertexBufferIdx, mesh.IndexBufferIdx, mesh.BatchKey, i );
                    batch.transforms.push_back( mesh.Transform );
                    continue;
                }

                RenderSingleMesh( id, mesh, state );
            }
        }
    }

    {
        OPTICK_CATEGORY( "Instanced Meshes", Optick::Category::GPU_Scene );

        for( size_t b = 0; b < m_activeBatchCount; ++b )
        {
            InstanceBatch& batch = m_instanceBatches[b];
            RenderMeshInstanced( id, m_meshCache.Commands[batch.representativeIndex], batch.transforms.data(), (uint32_t)batch.transforms.size(), state );
        }
    }

    {
        OPTICK_CATEGORY( "Transparent Sorting", Optick::Category::Camera );
        ME_ASSERT_MSG( TransparentIndicies.size() < kMeshTransparencyTempSize, "Transparent Indicies vector was resized, consider upping the cached size." );

        std::sort( TransparentIndicies.begin(), TransparentIndicies.end(), [this, &camera]( auto object1, auto object2 ) {
            glm::vec3 pos = m_meshCache.Commands[object1].Transform[3];
            glm::vec3 pos2 = m_meshCache.Commands[object2].Transform[3];

            float distancesq1 = ( pos.x - camera.Position.x ) * ( pos.x - camera.Position.x ) + ( pos.y - camera.Position.y ) * ( pos.y - camera.Position.y ) + ( pos.z - camera.Position.z ) * ( pos.z - camera.Position.z );
            float distancesq2 = ( pos2.x - camera.Position.x ) * ( pos2.x - camera.Position.x ) + ( pos2.y - camera.Position.y ) * ( pos2.y - camera.Position.y ) + ( pos2.z - camera.Position.z ) * ( pos2.z - camera.Position.z );

            return distancesq1 > distancesq2;
            } );
    }

    // Ambient occlusion darkens the opaque scene before transparents are composited on top.
    m_postProcess->RenderAmbientOcclusion( m_views, camera, *camera.Buffer );

    bx::snprintf( viewName, sizeof( viewName ), "%s Transparent", label );
    bgfx::ViewId transparentView = m_views.Allocate( viewName );
    if( transparentView == UINT16_MAX )
    {
        transparentView = id;
    }
    else
    {
        setupSceneView( transparentView );
    }
    if( EnableDebugDraw )
    {
        m_debugDraw->Begin( transparentView, true );
    }

    uint64_t transparentState = 0
        | BGFX_STATE_WRITE_RGB
        //| BGFX_STATE_WRITE_Z
        | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA
        | s_ptState[m_pt] // triangle strip cause I'm not changing that??
        ;

    {
        OPTICK_CATEGORY( "Transparent Meshes", Optick::Category::GPU_Scene )

            for( auto index : TransparentIndicies )
            {
                const Moonlight::MeshCommand& mesh = m_meshCache.Commands[index];
                if( mesh.SkinBoneCount > 0 )
                {
                    RenderSkinnedMesh( transparentView, mesh, transparentState );
                }
                else if( mesh.SupportsInstancing )
                {
                    RenderMeshInstanced( transparentView, mesh, &mesh.Transform, 1, transparentState );
                }
                else
                {
                    RenderSingleMesh( transparentView, mesh, transparentState );
                }
            }
    }

    if( EnableDebugDraw )
    {
        m_guizmoCallback( m_debugDraw.get() );

        for( auto& debugDrawCommand : m_debugDrawCache.Commands )
        {
            if( debugDrawCommand.Type != Moonlight::MeshType::MeshCount )
            {
                m_debugDraw->Push();
                m_debugDraw->Draw( &debugDrawCommand.Transform[0][0] );
                m_debugDraw->Pop();
            }
        }

        m_debugDraw->End();
    }
    SubmitDebugLines( camera, transparentView );
    RenderParticles( camera );

    // HDR -> final image (bloom, exposure, tonemapping, grading, FXAA).
    m_postProcess->DeltaSeconds = std::max( m_time.x, 0.0001f );
    m_postProcess->Render( m_views, camera, *camera.Buffer, target, targetWidth, targetHeight );

    float orthoProj[16];
    bx::mtxOrtho( orthoProj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, bgfx::getCaps()->homogeneousDepth );
    {
        // clear out transform stack
        float identity[16];
        bx::mtxIdentity( identity );
        bgfx::setTransform( identity );
    }

    float m_texelHalf = 0.0f;
    if( EnableUIComposite && camera.IsMain && bgfx::isValid( camera.UITexture ) )
    {
        // Runs after the UI resolve and all camera views, so the UI texture is from this frame.
        const bgfx::ViewId view = Moonlight::RenderView::UIComposite;
        bgfx::setViewName( view, "UI Composite" );

        bgfx::setViewRect( view, 0, 0, uint16_t( camera.OutputSize.x ), uint16_t( camera.OutputSize.y ) );
        bgfx::setViewTransform( view, NULL, orthoProj );
        bgfx::setViewFrameBuffer( view, target );
        bgfx::setState( 0
            | BGFX_STATE_WRITE_RGB
            //| BGFX_STATE_BLEND_ALPHA // - Not it, creates artifacts
            //| BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA) // - Almost there
            | BGFX_STATE_BLEND_FUNC_SEPARATE( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_INV_DST_ALPHA, BGFX_STATE_BLEND_ONE )
        );
        bgfx::setTexture( 0, s_texUI, camera.UITexture );
        Moonlight::screenSpaceQuad( camera.OutputSize.x, camera.OutputSize.y, m_texelHalf, bgfx::getCaps()->originBottomLeft );
        bgfx::submit( view, UIProgram );
    }
}

void BGFXRenderer::SubmitDebugLines( const Moonlight::CameraData& camera, bgfx::ViewId id )
{
    if( !m_debugDraw )
    {
        return;
    }
    // Fills first, so outlines drawn over them stay crisp.
    m_debugDraw->DrawTriangles( id, m_debugLines.Fill.data(), static_cast<uint32_t>( m_debugLines.Fill.size() ) );
    if( camera.IsEditorView )
    {
        m_debugDraw->DrawTriangles( id, m_debugLines.EditorFill.data(), static_cast<uint32_t>( m_debugLines.EditorFill.size() ) );
    }
    m_debugDraw->DrawLines( id, m_debugLines.Depth.data(), static_cast<uint32_t>( m_debugLines.Depth.size() ), true );
    m_debugDraw->DrawLines( id, m_debugLines.Overlay.data(), static_cast<uint32_t>( m_debugLines.Overlay.size() ), false );
    if( camera.IsEditorView )
    {
        m_debugDraw->DrawLines( id, m_debugLines.EditorDepth.data(), static_cast<uint32_t>( m_debugLines.EditorDepth.size() ), true );
        m_debugDraw->DrawLines( id, m_debugLines.EditorOverlay.data(), static_cast<uint32_t>( m_debugLines.EditorOverlay.size() ), false );
    }
}


void BGFXRenderer::PrepareFrameLighting()
{
    OPTICK_EVENT( "Renderer::PrepareFrameLighting" );
    const bool shadowsOn = Shadows.Enabled && m_shadowsSupported;
    if( shadowsOn )
    {
        EnsureShadowAtlas( m_sunShadowAtlas, Shadows.CascadeResolution, "Sun Shadow Atlas" );
        EnsureLocalShadowAtlas( Shadows.SpotResolution, Shadows.PointResolution );
    }

    // Directional lights go to uniforms; the first shadow caster takes slot 0 (the "sun").
    std::vector<const Moonlight::LightCommand*> directional;
    m_localLights.clear();
    for( const Moonlight::LightCommand& light : m_lights )
    {
        if( light.Type == Moonlight::LightType::Directional )
        {
            if( directional.size() < 4 )
            {
                directional.push_back( &light );
            }
        }
        else if( m_localLights.size() < 256 )
        {
            m_localLights.push_back( light );
        }
    }
    auto sun = std::find_if( directional.begin(), directional.end(), []( const Moonlight::LightCommand* light ) { return light->CastShadows; } );
    if( sun != directional.end() )
    {
        std::iter_swap( directional.begin(), sun );
    }

    m_hasSunShadow = false;
    for( size_t i = 0; i < directional.size(); ++i )
    {
        const Moonlight::LightCommand& light = *directional[i];
        float* direction = m_lighting.DirectionalDirection[i];
        float* color = m_lighting.DirectionalColor[i];
        const Vector3 dir = light.Direction.Normalized();
        const bool shadowed = i == 0 && light.CastShadows && shadowsOn;
        direction[0] = dir.x; direction[1] = dir.y; direction[2] = dir.z; direction[3] = shadowed ? 1.f : 0.f;
        color[0] = light.Color.x; color[1] = light.Color.y; color[2] = light.Color.z; color[3] = 0.f;
        if( shadowed )
        {
            m_hasSunShadow = true;
            m_sunShadowLight = light;
        }
    }
    int directionalCount = static_cast<int>( directional.size() );

    // Scenes without a directional light are lit (and shadowed) by the procedural sky's sun.
    if( directionalCount == 0 && m_dynamicSky )
    {
        const bx::Vec3& sunDir = m_dynamicSky->m_sun.m_sunDir;
        bx::Vec3 sunRgb = m_dynamicSky->xyzToRgb( m_dynamicSky->m_sunLuminanceXYZ.GetValue( m_dynamicSky->m_time ) );
        const float peak = std::max( { sunRgb.x, sunRgb.y, sunRgb.z, 1e-4f } );
        const float elevation = std::clamp( sunDir.y * 4.f, 0.f, 1.f );
        const float intensity = 3.f * elevation;
        float* direction = m_lighting.DirectionalDirection[0];
        float* color = m_lighting.DirectionalColor[0];
        direction[0] = -sunDir.x; direction[1] = -sunDir.y; direction[2] = -sunDir.z; direction[3] = shadowsOn ? 1.f : 0.f;
        color[0] = sunRgb.x / peak * intensity; color[1] = sunRgb.y / peak * intensity; color[2] = sunRgb.z / peak * intensity;
        directionalCount = 1;
        if( shadowsOn && elevation > 0.f )
        {
            m_hasSunShadow = true;
            m_sunShadowLight = Moonlight::LightCommand();
            m_sunShadowLight.Type = Moonlight::LightType::Directional;
            m_sunShadowLight.Direction = Vector3( -sunDir.x, -sunDir.y, -sunDir.z );
            m_sunShadowLight.CastShadows = true;
        }
        else
        {
            direction[3] = 0.f;
        }
    }

    m_lighting.Params[0] = static_cast<float>( directionalCount );
    m_lighting.Params[1] = static_cast<float>( m_localLights.size() );
    m_lighting.Params[2] = 1.f;

    // Bounds of everything that casts shadows: cascades extend towards the light to include them.
    m_casterBounds = AABB();
    if( m_hasSunShadow )
    {
        for( const Moonlight::MeshCommand& mesh : m_meshCache.Commands )
        {
            if( mesh.MeshMaterial && mesh.CastShadows && !mesh.IsTransparent )
            {
                m_casterBounds.Encapsulate( mesh.WorldBounds );
            }
        }
    }

    // Local shadows: the first four shadowed spots get an atlas tile each, the first two shadowed
    // point lights six (one per cube face).
    m_spotShadows.clear();
    for( glm::mat4& matrix : m_lighting.LocalShadowMatrix )
    {
        matrix = glm::mat4( 1.f );
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    const uint16_t spotTile = m_spotShadowAtlas.TileSize;
    const uint16_t pointTile = m_spotShadowAtlas.PointTileSize;
    const float spotTexelScaleBase = 2.f / std::max<float>( spotTile, 1.f );
    uint32_t spotCount = 0;
    uint32_t pointCount = 0;

    // Light data: 5 RGBA32F texels per point/spot light.
    if( !m_localLights.empty() && bgfx::isValid( m_lightDataTexture ) )
    {
        for( size_t i = 0; i < m_localLights.size(); ++i )
        {
            const Moonlight::LightCommand& light = m_localLights[i];
            const Vector3 dir = light.Direction.Normalized();
            float shadowSlot = -1.f;
            float texelScale = 0.f;
            if( shadowsOn && light.Type == Moonlight::LightType::Spot && light.CastShadows && spotCount < kMaxSpotShadows )
            {
                // A little wider than the cone so PCF at the rim stays inside the projection.
                const float fov = std::min( light.OuterConeAngle * 2.f + 4.f, 170.f );
                SpotShadow shadow;
                const glm::vec3 position = light.Position.InternalVector;
                const glm::vec3 forward = glm::normalize( dir.InternalVector );
                const glm::vec3 up = std::abs( forward.y ) > 0.99f ? glm::vec3( 0.f, 0.f, 1.f ) : glm::vec3( 0.f, 1.f, 0.f );
                shadow.View = glm::lookAtLH( position, position + forward, up );
                bx::mtxProj( &shadow.Projection[0][0], fov, 1.f, 0.05f, std::max( light.Range, 0.1f ), caps->homogeneousDepth );
                shadow.X = static_cast<uint16_t>( ( spotCount % 2 ) * spotTile );
                shadow.Y = static_cast<uint16_t>( ( spotCount / 2 ) * spotTile );
                shadow.Size = spotTile;
                shadowSlot = static_cast<float>( spotCount );
                texelScale = std::tan( glm::radians( fov ) * 0.5f ) * spotTexelScaleBase;
                m_lighting.LocalShadowMatrix[spotCount] = AtlasRectMatrix( m_spotShadowAtlas, shadow.X, shadow.Y, shadow.Size, shadow.Projection * shadow.View );
                m_spotShadows.push_back( shadow );
                ++spotCount;
            }
            else if( shadowsOn && light.Type == Moonlight::LightType::Point && light.CastShadows && pointCount < kMaxPointShadows && pointTile > 0 )
            {
                // Six 90-degree faces, a hair wider so PCF at a face's edge stays inside it.
                static const glm::vec3 kFaces[6][2] = {
                    { { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f } }, { { -1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f } },
                    { { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f } }, { { 0.f, -1.f, 0.f }, { 0.f, 0.f, 1.f } },
                    { { 0.f, 0.f, 1.f }, { 0.f, 1.f, 0.f } }, { { 0.f, 0.f, -1.f }, { 0.f, 1.f, 0.f } },
                };
                const float fov = glm::degrees( 2.f * std::atan( 1.f + 3.f / static_cast<float>( pointTile ) ) );
                const glm::vec3 position = light.Position.InternalVector;
                for( uint32_t face = 0; face < 6; ++face )
                {
                    const uint32_t tile = pointCount * 6 + face;
                    SpotShadow shadow;
                    shadow.View = glm::lookAtLH( position, position + kFaces[face][0], kFaces[face][1] );
                    bx::mtxProj( &shadow.Projection[0][0], fov, 1.f, 0.05f, std::max( light.Range, 0.1f ), caps->homogeneousDepth );
                    shadow.X = static_cast<uint16_t>( spotTile * 2 + ( tile % 4 ) * pointTile );
                    shadow.Y = static_cast<uint16_t>( ( tile / 4 ) * pointTile );
                    shadow.Size = pointTile;
                    m_lighting.LocalShadowMatrix[kMaxSpotShadows + tile] = AtlasRectMatrix( m_spotShadowAtlas, shadow.X, shadow.Y, shadow.Size, shadow.Projection * shadow.View );
                    m_spotShadows.push_back( shadow );
                }
                shadowSlot = static_cast<float>( pointCount );
                texelScale = std::tan( glm::radians( fov ) * 0.5f ) * 2.f / static_cast<float>( pointTile );
                ++pointCount;
            }
            const float values[20] = {
                light.Position.x, light.Position.y, light.Position.z, light.Range,
                light.Color.x, light.Color.y, light.Color.z, light.Type == Moonlight::LightType::Spot ? 2.f : 1.f,
                dir.x, dir.y, dir.z, light.CosOuter,
                light.CosInner, 0.f, 0.f, 0.f,
                shadowSlot, texelScale, light.ShadowBias, light.ShadowNormalBias };
            std::memcpy( &m_lightData[i * 20], values, sizeof( values ) );
        }
        const uint16_t width = static_cast<uint16_t>( m_localLights.size() * 5 );
        bgfx::updateTexture2D( m_lightDataTexture, 0, 0, 0, 0, width, 1, bgfx::copy( m_lightData.data(), width * 4 * sizeof( float ) ) );
    }
    const float width = std::max<float>( m_spotShadowAtlas.Width, 1.f );
    const float height = std::max<float>( m_spotShadowAtlas.Height, 1.f );
    m_lighting.SpotShadowParams[0] = 1.f / width;
    m_lighting.SpotShadowParams[1] = 1.f / height;
    m_lighting.SpotShadowParams[2] = spotTile / width;
    m_lighting.SpotShadowParams[3] = spotTile / height;
    m_lighting.PointShadowParams[0] = spotTile * 2.f / width;
    m_lighting.PointShadowParams[1] = pointTile / width;
    m_lighting.PointShadowParams[2] = pointTile / height;

    RenderLocalShadows();
}


void BGFXRenderer::PrepareCameraLighting( Moonlight::CameraData& camera )
{
    OPTICK_EVENT( "Renderer::PrepareCameraLighting" );
    Moonlight::FrameBuffer& buffer = *camera.Buffer;
    m_lighting.ClusterGridTexture = buffer.ClusterGrid;
    m_lighting.ClusterIndexTexture = buffer.ClusterIndices;

    if( !m_localLights.empty() )
    {
        m_clusterBuilder->Build( m_localLights, camera.View, camera.ProjectionMatrix, camera.Near, camera.Far, static_cast<float>( buffer.Width ), static_cast<float>( buffer.Height ) );
        const std::vector<float>& grid = m_clusterBuilder->GetGrid();
        bgfx::updateTexture2D( buffer.ClusterGrid, 0, 0, 0, 0, Moonlight::ClusterBuilder::kGridX * Moonlight::ClusterBuilder::kGridY, Moonlight::ClusterBuilder::kGridZ, bgfx::copy( grid.data(), static_cast<uint32_t>( grid.size() * sizeof( float ) ) ) );
        const uint32_t rows = m_clusterBuilder->GetUsedIndexRows();
        bgfx::updateTexture2D( buffer.ClusterIndices, 0, 0, 0, 0, Moonlight::ClusterBuilder::kIndexWidth, static_cast<uint16_t>( rows ), bgfx::copy( m_clusterBuilder->GetIndices().data(), rows * Moonlight::ClusterBuilder::kIndexWidth * sizeof( float ) ) );

        const Moonlight::ClusterBuilder::Params& params = m_clusterBuilder->GetParams();
        m_lighting.ClusterParams[0] = params.TileWidth;
        m_lighting.ClusterParams[1] = params.TileHeight;
        m_lighting.ClusterParams[2] = params.SliceScale;
        m_lighting.ClusterParams[3] = params.SliceBias;
    }
    m_lighting.ClusterGrid[0] = static_cast<float>( Moonlight::ClusterBuilder::kGridX );
    m_lighting.ClusterGrid[1] = static_cast<float>( Moonlight::ClusterBuilder::kGridY );
    m_lighting.ClusterGrid[2] = static_cast<float>( Moonlight::ClusterBuilder::kGridZ );
    m_lighting.ClusterGrid[3] = bgfx::getCaps()->originBottomLeft ? 1.f : 0.f;

    // Hemisphere ambient from what the camera shows behind the scene.
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    Vector3 sky( 0.3f, 0.32f, 0.36f );
    if( camera.ClearType == Moonlight::ClearColorType::Procedural && m_dynamicSky )
    {
        const bx::Vec3 skyRgb = m_dynamicSky->xyzToRgb( m_dynamicSky->m_skyLuminanceXYZ.GetValue( m_dynamicSky->m_time ) );
        const float peak = std::max( { skyRgb.x, skyRgb.y, skyRgb.z, 1e-4f } );
        const float daylight = std::clamp( m_dynamicSky->m_sun.m_sunDir.y * 3.f + 0.3f, 0.05f, 1.f );
        sky = Vector3( skyRgb.x / peak, skyRgb.y / peak, skyRgb.z / peak ) * ( 0.6f * daylight );
    }
    else if( camera.ClearType == Moonlight::ClearColorType::Color )
    {
        sky = Vector3( toLinear( camera.ClearColor.x ), toLinear( camera.ClearColor.y ), toLinear( camera.ClearColor.z ) ) * 2.f + Vector3( 0.12f, 0.12f, 0.14f );
    }
    m_lighting.AmbientSky[0] = sky.x; m_lighting.AmbientSky[1] = sky.y; m_lighting.AmbientSky[2] = sky.z;
    m_lighting.AmbientGround[0] = sky.x * 0.45f; m_lighting.AmbientGround[1] = sky.y * 0.4f; m_lighting.AmbientGround[2] = sky.z * 0.35f;
}


void BGFXRenderer::EnsureShadowAtlas( ShadowAtlas& atlas, uint16_t tileSize, const char* name )
{
    tileSize = std::clamp<uint16_t>( tileSize, 128, 4096 );
    if( atlas.TileSize == tileSize && bgfx::isValid( atlas.Texture ) )
    {
        return;
    }
    DestroyShadowAtlas( atlas );

    // Hardware-compared depth (bilinear PCF per tap); prefer 32-bit float depth.
    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::D16;
    for( bgfx::TextureFormat::Enum candidate : { bgfx::TextureFormat::D32F, bgfx::TextureFormat::D24, bgfx::TextureFormat::D16 } )
    {
        if( bgfx::isTextureValid( 0, false, 1, candidate, flags ) )
        {
            format = candidate;
            break;
        }
    }
    const uint16_t size = static_cast<uint16_t>( tileSize * 2 );
    atlas.Texture = bgfx::createTexture2D( size, size, false, 1, format, flags );
    bgfx::setName( atlas.Texture, name );
    atlas.Buffer = bgfx::createFrameBuffer( 1, &atlas.Texture, false );
    atlas.TileSize = tileSize;
}


void BGFXRenderer::EnsureLocalShadowAtlas( uint16_t spotTile, uint16_t pointTile )
{
    spotTile = std::clamp<uint16_t>( spotTile, 128, 4096 );
    pointTile = std::clamp<uint16_t>( pointTile, 64, 2048 );
    ShadowAtlas& atlas = m_spotShadowAtlas;
    if( atlas.TileSize == spotTile && atlas.PointTileSize == pointTile && bgfx::isValid( atlas.Texture ) )
    {
        return;
    }
    DestroyShadowAtlas( atlas );

    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::D16;
    for( bgfx::TextureFormat::Enum candidate : { bgfx::TextureFormat::D32F, bgfx::TextureFormat::D24, bgfx::TextureFormat::D16 } )
    {
        if( bgfx::isTextureValid( 0, false, 1, candidate, flags ) )
        {
            format = candidate;
            break;
        }
    }
    atlas.Width = static_cast<uint16_t>( spotTile * 2 + pointTile * 4 );
    atlas.Height = static_cast<uint16_t>( std::max( spotTile * 2, pointTile * 4 ) );
    atlas.Texture = bgfx::createTexture2D( atlas.Width, atlas.Height, false, 1, format, flags );
    bgfx::setName( atlas.Texture, "Local Shadow Atlas" );
    atlas.Buffer = bgfx::createFrameBuffer( 1, &atlas.Texture, false );
    atlas.TileSize = spotTile;
    atlas.PointTileSize = pointTile;
}


glm::mat4 BGFXRenderer::AtlasRectMatrix( const ShadowAtlas& atlas, uint16_t x, uint16_t y, uint16_t size, const glm::mat4& viewProjection ) const
{
    // Clip space -> texture space (as ShadowAtlasMatrix), then into the rectangle. View rects are
    // top-left based; on bottom-left-origin backends the texture's v runs the other way.
    const bgfx::Caps* caps = bgfx::getCaps();
    const float sy = caps->originBottomLeft ? 0.5f : -0.5f;
    const float sz = caps->homogeneousDepth ? 0.5f : 1.f;
    const float tz = caps->homogeneousDepth ? 0.5f : 0.f;
    glm::mat4 crop( 1.f );
    crop[0][0] = 0.5f;
    crop[1][1] = sy;
    crop[2][2] = sz;
    crop[3] = glm::vec4( 0.5f, 0.5f, tz, 1.f );

    const float width = std::max<float>( atlas.Width, 1.f );
    const float height = std::max<float>( atlas.Height, 1.f );
    const float w = size / width;
    const float h = size / height;
    const float u0 = x / width;
    const float v0 = caps->originBottomLeft ? 1.f - ( y + size ) / height : y / height;
    glm::mat4 rect( 1.f );
    rect[0][0] = w;
    rect[1][1] = h;
    rect[3] = glm::vec4( u0, v0, 0.f, 1.f );
    return rect * crop * viewProjection;
}


void BGFXRenderer::DestroyShadowAtlas( ShadowAtlas& atlas )
{
    if( bgfx::isValid( atlas.Buffer ) )
    {
        bgfx::destroy( atlas.Buffer );
    }
    if( bgfx::isValid( atlas.Texture ) )
    {
        bgfx::destroy( atlas.Texture );
    }
    atlas = ShadowAtlas();
}


glm::mat4 BGFXRenderer::ShadowAtlasMatrix( uint32_t tile, const glm::mat4& viewProjection ) const
{
    // Clip space -> texture space (y flips unless the backend's origin is bottom-left; GL depth is
    // [-1, 1]), then into the tile's quarter of the atlas. Tile 0 is the top-left of the target.
    const bgfx::Caps* caps = bgfx::getCaps();
    const float sy = caps->originBottomLeft ? 0.5f : -0.5f;
    const float sz = caps->homogeneousDepth ? 0.5f : 1.f;
    const float tz = caps->homogeneousDepth ? 0.5f : 0.f;
    glm::mat4 crop( 1.f );
    crop[0][0] = 0.5f;
    crop[1][1] = sy;
    crop[2][2] = sz;
    crop[3] = glm::vec4( 0.5f, 0.5f, tz, 1.f );

    const float column = static_cast<float>( tile % 2 );
    const float row = static_cast<float>( tile / 2 );
    glm::mat4 tileMatrix( 1.f );
    tileMatrix[0][0] = 0.5f;
    tileMatrix[1][1] = 0.5f;
    tileMatrix[3] = glm::vec4( column * 0.5f, caps->originBottomLeft ? 0.5f - row * 0.5f : row * 0.5f, 0.f, 1.f );
    return tileMatrix * crop * viewProjection;
}


void BGFXRenderer::SubmitShadowCasters( bgfx::ViewId view, const Frustum& frustum )
{
    OPTICK_EVENT( "Renderer::SubmitShadowCasters" );
    for( size_t b = 0; b < m_activeShadowBatchCount; ++b )
    {
        m_shadowBatches[b].transforms.clear();
    }
    m_activeShadowBatchCount = 0;
    m_skinnedShadowCasters.clear();

    for( size_t i = 0; i < m_meshCache.Commands.size(); ++i )
    {
        const Moonlight::MeshCommand& mesh = m_meshCache.Commands[i];
        if( !mesh.MeshMaterial || !mesh.SingleMesh || !mesh.CastShadows || mesh.IsTransparent || mesh.VertexBufferIdx == UINT16_MAX )
        {
            continue;
        }
        if( !mesh.WorldBounds.IsValid() || !frustum.Intersects( mesh.WorldBounds ) )
        {
            continue;
        }
        if( mesh.SkinBoneCount > 0 )
        {
            m_skinnedShadowCasters.push_back( i );   // each has its own pose: drawn one by one below
            continue;
        }
        // Opaque casters only differ by geometry; alpha-tested ones also by their textures.
        const uint64_t key = mesh.AlphaCutoff > 0.f ? mesh.BatchKey : 0u;
        InstanceBatch* batch = nullptr;
        for( size_t b = 0; b < m_activeShadowBatchCount; ++b )
        {
            InstanceBatch& candidate = m_shadowBatches[b];
            if( candidate.vertexBuffer == mesh.VertexBufferIdx && candidate.indexBuffer == mesh.IndexBufferIdx && candidate.materialKey == key )
            {
                batch = &candidate;
                break;
            }
        }
        if( !batch )
        {
            if( m_activeShadowBatchCount == m_shadowBatches.size() )
            {
                m_shadowBatches.emplace_back();
            }
            batch = &m_shadowBatches[m_activeShadowBatchCount++];
            batch->vertexBuffer = mesh.VertexBufferIdx;
            batch->indexBuffer = mesh.IndexBufferIdx;
            batch->materialKey = key;
            batch->representativeIndex = i;
        }
        batch->transforms.push_back( mesh.Transform );
    }

    constexpr uint16_t kInstanceStride = sizeof( glm::mat4 );
    const uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    for( size_t b = 0; b < m_activeShadowBatchCount; ++b )
    {
        const InstanceBatch& batch = m_shadowBatches[b];
        const Moonlight::MeshCommand& mesh = m_meshCache.Commands[batch.representativeIndex];
        const float alpha[4] = { mesh.AlphaCutoff, mesh.MeshMaterial->Tiling.x, mesh.MeshMaterial->Tiling.y, 0.f };
        const uint32_t count = static_cast<uint32_t>( batch.transforms.size() );
        uint32_t offset = 0;
        while( offset < count )
        {
            const uint32_t available = bgfx::getAvailInstanceDataBuffer( count - offset, kInstanceStride );
            if( available == 0 )
            {
                break;
            }
            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer( &idb, available, kInstanceStride );
            std::memcpy( idb.data, batch.transforms.data() + offset, (size_t)available * kInstanceStride );

            bgfx::setVertexBuffer( 0, mesh.SingleMesh->GetVertexBuffer() );
            bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexuffer() );
            bgfx::setInstanceDataBuffer( &idb );
            bgfx::setUniform( u_shadowAlpha, alpha );
            if( mesh.AlphaCutoff > 0.f )
            {
                const Moonlight::Texture* diffuse = mesh.MeshMaterial->GetTexture( Moonlight::TextureType::Diffuse );
                const Moonlight::Texture* opacity = mesh.MeshMaterial->GetTexture( Moonlight::TextureType::Opacity );
                bgfx::setTexture( 0, s_texDiffuse, diffuse && bgfx::isValid( diffuse->TexHandle ) ? diffuse->TexHandle : m_whiteTexture );
                bgfx::setTexture( 2, s_texAlpha, opacity && bgfx::isValid( opacity->TexHandle ) ? opacity->TexHandle : m_whiteTexture );
            }
            bgfx::setState( state );
            bgfx::submit( view, m_shadowProgram.GetProgram() );
            offset += available;
        }
    }

    const bgfx::ProgramHandle skinnedProgram = m_shadowSkinnedProgram.GetProgram();
    for( size_t index : m_skinnedShadowCasters )
    {
        const Moonlight::MeshCommand& mesh = m_meshCache.Commands[index];
        if( !bgfx::isValid( skinnedProgram ) || !bgfx::isValid( mesh.SingleMesh->GetSkinBuffer() ) || bgfx::getAvailInstanceDataBuffer( 1, kInstanceStride ) == 0 )
        {
            continue;
        }
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer( &idb, 1, kInstanceStride );
        std::memcpy( idb.data, &mesh.Transform, kInstanceStride );
        const float alpha[4] = { mesh.AlphaCutoff, mesh.MeshMaterial->Tiling.x, mesh.MeshMaterial->Tiling.y, 0.f };
        bgfx::setVertexBuffer( 0, mesh.SingleMesh->GetVertexBuffer() );
        bgfx::setVertexBuffer( 1, mesh.SingleMesh->GetSkinBuffer() );
        bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexuffer() );
        bgfx::setInstanceDataBuffer( &idb );
        bgfx::setUniform( u_shadowAlpha, alpha );
        bgfx::setUniform( u_bones, mesh.SkinPalette, mesh.SkinBoneCount );
        if( mesh.AlphaCutoff > 0.f )
        {
            const Moonlight::Texture* diffuse = mesh.MeshMaterial->GetTexture( Moonlight::TextureType::Diffuse );
            const Moonlight::Texture* opacity = mesh.MeshMaterial->GetTexture( Moonlight::TextureType::Opacity );
            bgfx::setTexture( 0, s_texDiffuse, diffuse && bgfx::isValid( diffuse->TexHandle ) ? diffuse->TexHandle : m_whiteTexture );
            bgfx::setTexture( 2, s_texAlpha, opacity && bgfx::isValid( opacity->TexHandle ) ? opacity->TexHandle : m_whiteTexture );
        }
        bgfx::setState( state );
        bgfx::submit( view, skinnedProgram );
    }
}


void BGFXRenderer::RenderLocalShadows()
{
    OPTICK_EVENT( "Renderer::RenderLocalShadows" );
    for( size_t i = 0; i < m_spotShadows.size(); ++i )
    {
        char name[32];
        bx::snprintf( name, sizeof( name ), "Local Shadow %u", static_cast<uint32_t>( i ) );
        const bgfx::ViewId view = m_views.Allocate( name );
        if( view == UINT16_MAX )
        {
            return;
        }
        const SpotShadow& shadow = m_spotShadows[i];
        bgfx::setViewFrameBuffer( view, m_spotShadowAtlas.Buffer );
        bgfx::setViewRect( view, shadow.X, shadow.Y, shadow.Size, shadow.Size );
        bgfx::setViewClear( view, BGFX_CLEAR_DEPTH, 0, 1.f, 0 );
        bgfx::setViewTransform( view, &shadow.View[0][0], &shadow.Projection[0][0] );
        bgfx::touch( view );
        Frustum frustum;
        frustum.Update( shadow.Projection * shadow.View );
        SubmitShadowCasters( view, frustum );
    }
}


void BGFXRenderer::RenderSunShadows( Moonlight::CameraData& camera )
{
    OPTICK_EVENT( "Renderer::RenderSunShadows" );
    m_lighting.ShadowParams[3] = 0.f;
    if( !m_hasSunShadow )
    {
        return;
    }

    Moonlight::CascadeInput input;
    input.CameraPosition = camera.Position;
    input.CameraFront = camera.Front;
    input.CameraUp = camera.Up;
    input.FovDegrees = camera.FOV;
    input.Aspect = camera.OutputSize.y > 0.f ? camera.OutputSize.x / camera.OutputSize.y : 1.f;
    input.Orthographic = camera.Projection == Moonlight::ProjectionType::Orthographic;
    input.OrthographicSize = camera.OrthographicSize;
    input.Near = camera.Near;
    input.ShadowDistance = std::min( m_sunShadowLight.ShadowDistance, camera.Far );
    input.LightDirection = m_sunShadowLight.Direction;
    input.Resolution = m_sunShadowAtlas.TileSize;
    input.HomogeneousDepth = bgfx::getCaps()->homogeneousDepth;
    input.CasterBounds = m_casterBounds;
    Moonlight::CascadeSetup cascades[Moonlight::kMaxCascades];
    Moonlight::ComputeCascades( input, cascades );

    const uint16_t tile = m_sunShadowAtlas.TileSize;
    for( uint32_t i = 0; i < Moonlight::kMaxCascades; ++i )
    {
        char name[32];
        bx::snprintf( name, sizeof( name ), "Shadow Cascade %u", i );
        const bgfx::ViewId view = m_views.Allocate( name );
        if( view == UINT16_MAX )
        {
            return;
        }
        const Moonlight::CascadeSetup& cascade = cascades[i];
        bgfx::setViewFrameBuffer( view, m_sunShadowAtlas.Buffer );
        bgfx::setViewRect( view, static_cast<uint16_t>( ( i % 2 ) * tile ), static_cast<uint16_t>( ( i / 2 ) * tile ), tile, tile );
        bgfx::setViewClear( view, BGFX_CLEAR_DEPTH, 0, 1.f, 0 );
        bgfx::setViewTransform( view, &cascade.View[0][0], &cascade.Projection[0][0] );
        bgfx::touch( view );
        Frustum frustum;
        frustum.Update( cascade.ViewProjection );
        SubmitShadowCasters( view, frustum );

        m_lighting.ShadowMatrix[i] = ShadowAtlasMatrix( i, cascade.ViewProjection );
        m_lighting.CascadeSplits[i] = cascade.SplitFar;
        m_lighting.CascadeTexel[i] = cascade.TexelWorldSize;
    }
    m_lighting.ShadowParams[0] = m_sunShadowLight.ShadowBias;
    m_lighting.ShadowParams[1] = m_sunShadowLight.ShadowNormalBias;
    m_lighting.ShadowParams[2] = 1.f / ( 2.f * static_cast<float>( tile ) );
    m_lighting.ShadowParams[3] = Shadows.DebugCascades ? 2.f : 1.f;
}


void BGFXRenderer::RenderParticles( Moonlight::CameraData& camera )
{
    OPTICK_EVENT( "Renderer::RenderParticles" );
    if( !m_particleProgram.IsLoaded() || !camera.Buffer || !bgfx::isValid( camera.Buffer->ParticleBuffer ) )
    {
        return;
    }

    // Systems back to front (by the centre of their bounds), skipping ones outside the view.
    const glm::mat4& viewMatrix = camera.View.GetInternalMatrix();
    m_particleOrder.clear();
    for( uint32_t i = 0; i < m_particleBatches.size(); ++i )
    {
        const Moonlight::ParticleBatch& batch = m_particleBatches[i];
        if( batch.Instances.empty() || ( camera.ShouldCull && !camera.ViewFrustum.Intersects( batch.Bounds ) ) )
        {
            continue;
        }
        const glm::vec4 center = viewMatrix * glm::vec4( batch.Bounds.GetCenter().InternalVector, 1.f );
        m_particleOrder.emplace_back( center.z, i );
    }
    if( m_particleOrder.empty() )
    {
        return;
    }
    std::sort( m_particleOrder.begin(), m_particleOrder.end(), []( const auto& a, const auto& b ) { return a.first > b.first; } );

    const bgfx::ViewId view = m_views.Allocate( camera.IsEditorView ? "Editor Particles" : "Particles" );
    if( view == UINT16_MAX )
    {
        return;
    }
    Moonlight::FrameBuffer& buffer = *camera.Buffer;
    bgfx::setViewFrameBuffer( view, buffer.ParticleBuffer );
    bgfx::setViewRect( view, 0, 0, static_cast<uint16_t>( buffer.Width ), static_cast<uint16_t>( buffer.Height ) );
    bgfx::setViewTransform( view, &camera.View.GetInternalMatrix()[0][0], &camera.ProjectionMatrix.GetInternalMatrix()[0][0] );
    // Submission order is draw order (back to front).
    bgfx::setViewMode( view, bgfx::ViewMode::Sequential );
    bgfx::touch( view );

    const glm::mat4& projection = camera.ProjectionMatrix.GetInternalMatrix();
    const float depthParams[4] = { projection[2][2], projection[3][2], camera.Projection == Moonlight::ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
    constexpr uint16_t kInstanceStride = sizeof( Moonlight::ParticleInstance );
    static_assert( sizeof( Moonlight::ParticleInstance ) == 64, "particle instances are four vec4s" );

    for( const auto& entry : m_particleOrder )
    {
        const Moonlight::ParticleBatch& batch = m_particleBatches[entry.second];
        const Moonlight::ParticleInstance* instances = batch.Instances.data();
        if( batch.Blend == Moonlight::ParticleBlend::Alpha )
        {
            // Blended particles need back-to-front order within the system too.
            m_particleSorted = batch.Instances;
            std::sort( m_particleSorted.begin(), m_particleSorted.end(), [&viewMatrix]( const Moonlight::ParticleInstance& a, const Moonlight::ParticleInstance& b ) {
                const float za = viewMatrix[0][2] * a.PositionSize.x + viewMatrix[1][2] * a.PositionSize.y + viewMatrix[2][2] * a.PositionSize.z;
                const float zb = viewMatrix[0][2] * b.PositionSize.x + viewMatrix[1][2] * b.PositionSize.y + viewMatrix[2][2] * b.PositionSize.z;
                return za > zb;
            } );
            instances = m_particleSorted.data();
        }

        const float params[4] = { static_cast<float>( batch.FlipbookColumns ), static_cast<float>( batch.FlipbookRows ), batch.Softness, static_cast<float>( batch.Alignment ) };
        const float params2[4] = { batch.StretchFactor, batch.Blend == Moonlight::ParticleBlend::Additive ? 1.f : 0.f, 0.f, 0.f };
        const uint64_t blend = batch.Blend == Moonlight::ParticleBlend::Additive
            ? BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE )
            : BGFX_STATE_BLEND_ALPHA;
        const bgfx::TextureHandle texture = bgfx::isValid( batch.Texture ) ? batch.Texture : m_particleTexture;

        const uint32_t count = static_cast<uint32_t>( batch.Instances.size() );
        uint32_t offset = 0;
        while( offset < count )
        {
            const uint32_t available = bgfx::getAvailInstanceDataBuffer( count - offset, kInstanceStride );
            if( available == 0 )
            {
                break;
            }
            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer( &idb, available, kInstanceStride );
            std::memcpy( idb.data, instances + offset, (size_t)available * kInstanceStride );
            bgfx::setVertexBuffer( 0, m_particleQuadVB );
            bgfx::setIndexBuffer( m_particleQuadIB );
            bgfx::setInstanceDataBuffer( &idb );
            bgfx::setTexture( 0, s_texParticle, texture );
            bgfx::setTexture( 1, s_sceneDepth, buffer.DepthTexture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
            bgfx::setUniform( u_particleParams, params );
            bgfx::setUniform( u_particleParams2, params2 );
            bgfx::setUniform( u_particleDepth, depthParams );
            bgfx::setState( BGFX_STATE_WRITE_RGB | blend );
            bgfx::submit( view, m_particleProgram.GetProgram() );
            offset += available;
        }
    }
}


void BGFXRenderer::BindLighting()
{
    // Uniform state is part of each draw in bgfx, so these go with every submit.
    bgfx::setUniform( u_lightParams, m_lighting.Params );
    bgfx::setUniform( u_dirLightDirection, m_lighting.DirectionalDirection, 4 );
    bgfx::setUniform( u_dirLightColor, m_lighting.DirectionalColor, 4 );
    bgfx::setUniform( u_clusterParams, m_lighting.ClusterParams );
    bgfx::setUniform( u_clusterGrid, m_lighting.ClusterGrid );
    bgfx::setUniform( u_ambientSky, m_lighting.AmbientSky );
    bgfx::setUniform( u_ambientGround, m_lighting.AmbientGround );
    const uint32_t pointClamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::setTexture( 8, s_lightData, m_lightDataTexture, pointClamp );
    bgfx::setUniform( u_shadowMatrix, m_lighting.ShadowMatrix, 4 );
    bgfx::setUniform( u_cascadeSplits, m_lighting.CascadeSplits );
    bgfx::setUniform( u_cascadeTexel, m_lighting.CascadeTexel );
    bgfx::setUniform( u_shadowParams, m_lighting.ShadowParams );
    bgfx::setUniform( u_localShadowMatrix, m_lighting.LocalShadowMatrix, 16 );
    bgfx::setUniform( u_spotShadowParams, m_lighting.SpotShadowParams );
    bgfx::setUniform( u_pointShadowParams, m_lighting.PointShadowParams );
    if( bgfx::isValid( m_sunShadowAtlas.Texture ) )
    {
        bgfx::setTexture( 11, s_shadowMap, m_sunShadowAtlas.Texture );
        bgfx::setTexture( 12, s_spotShadowMap, m_spotShadowAtlas.Texture );
    }
    bgfx::setUniform( u_envParams, m_lighting.EnvParams );
    if( bgfx::isValid( m_lighting.EnvSpecular ) )
    {
        bgfx::setTexture( 13, s_envSpecular, m_lighting.EnvSpecular );
        bgfx::setTexture( 14, s_envIrradiance, m_lighting.EnvIrradiance );
        bgfx::setTexture( 15, s_brdfLut, m_environment->GetBrdfLut() );
    }
    if( bgfx::isValid( m_lighting.ClusterGridTexture ) )
    {
        bgfx::setTexture( 9, s_clusterGrid, m_lighting.ClusterGridTexture, pointClamp );
        bgfx::setTexture( 10, s_clusterIndices, m_lighting.ClusterIndexTexture, pointClamp );
    }
}


bgfx::ProgramHandle BGFXRenderer::BindMeshDrawState( const Moonlight::MeshCommand& mesh, uint64_t state )
{
    // Set vertex and index buffer.
    bgfx::setVertexBuffer( 0, mesh.SingleMesh->GetVertexBuffer() );
    bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexuffer() );

    // Every sampler gets a texture: a missing map falls back to a neutral 1x1 so the material's
    // colour shows through instead of whatever the backend leaves bound.
    auto textureOr = [&mesh]( Moonlight::TextureType type, bgfx::TextureHandle fallback ) {
        const Moonlight::Texture* texture = mesh.MeshMaterial->GetTexture( type );
        return ( texture && bgfx::isValid( texture->TexHandle ) ) ? texture->TexHandle : fallback;
    };
    bgfx::setTexture( 0, s_texDiffuse, textureOr( Moonlight::TextureType::Diffuse, m_whiteTexture ) );
    bgfx::setTexture( 1, s_texNormal, textureOr( Moonlight::TextureType::Normal, m_flatNormalTexture ) );
    bgfx::setTexture( 2, s_texAlpha, textureOr( Moonlight::TextureType::Opacity, m_whiteTexture ) );
    bgfx::setTexture( 3, s_texMetallicRoughness, textureOr( Moonlight::TextureType::MetallicRoughness, m_whiteTexture ) );
    bgfx::setTexture( 4, s_texEmissive, textureOr( Moonlight::TextureType::Emissive, m_whiteTexture ) );
    bgfx::setTexture( 5, s_texOcclusion, textureOr( Moonlight::TextureType::Occlusion, m_whiteTexture ) );
    BindLighting();

    mesh.MeshMaterial->Use();

    // Set render states.
    bgfx::setState( mesh.MeshMaterial->GetRenderState( state ) );

    return mesh.MeshMaterial->MeshShader.GetProgram();
}


void BGFXRenderer::RenderSingleMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state )
{
    OPTICK_CATEGORY( "Mesh", Optick::Category::Rendering );

    if( mesh.Type != Moonlight::MeshType::MeshCount )
    {
        if( !mesh.SingleMesh || !bgfx::isValid(mesh.SingleMesh->GetVertexBuffer() ) )
        {
            return;
        }

        // Set model matrix for rendering.
        bgfx::setTransform( &mesh.Transform );

        bgfx::ProgramHandle program = BindMeshDrawState( mesh, state );

        // Submit primitive for rendering to view 0.
        bgfx::submit( id, program );
    }
    else
    {
        ME_ASSERT_MSG( false, "Why do I need that if statement?" );
    }
}


void BGFXRenderer::RenderSkinnedMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state )
{
    constexpr uint16_t kInstanceStride = sizeof( glm::mat4 );
    if( !mesh.SingleMesh || !bgfx::isValid( mesh.SingleMesh->GetVertexBuffer() ) || bgfx::getAvailInstanceDataBuffer( 1, kInstanceStride ) == 0 )
    {
        return;
    }
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer( &idb, 1, kInstanceStride );
    std::memcpy( idb.data, &mesh.Transform, kInstanceStride );

    bgfx::ProgramHandle program = BindMeshDrawState( mesh, state );
    const bgfx::ProgramHandle skinned = mesh.MeshMaterial->GetSkinnedProgram();
    if( bgfx::isValid( skinned ) && bgfx::isValid( mesh.SingleMesh->GetSkinBuffer() ) )
    {
        bgfx::setVertexBuffer( 1, mesh.SingleMesh->GetSkinBuffer() );
        bgfx::setUniform( u_bones, mesh.SkinPalette, mesh.SkinBoneCount );
        program = skinned;
    }
    // Materials without a skinned program draw the bind pose.
    bgfx::setInstanceDataBuffer( &idb );
    bgfx::submit( id, program );
}


void BGFXRenderer::RenderMeshInstanced( bgfx::ViewId id, const Moonlight::MeshCommand& representative, const glm::mat4* transforms, uint32_t count, uint64_t state )
{
    if( count == 0 || transforms == nullptr || !representative.SingleMesh || !bgfx::isValid( representative.SingleMesh->GetVertexBuffer() ) )
    {
        return;
    }

    constexpr uint16_t kInstanceStride = sizeof( glm::mat4 );

    uint32_t remaining = count;
    uint32_t offset = 0;
    while( remaining > 0 )
    {
        const uint32_t available = bgfx::getAvailInstanceDataBuffer( remaining, kInstanceStride );
        if( available == 0 )
        {
            break;
        }

        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer( &idb, available, kInstanceStride );
        std::memcpy( idb.data, transforms + offset, (size_t)available * kInstanceStride );

        bgfx::ProgramHandle program = BindMeshDrawState( representative, state );
        bgfx::setInstanceDataBuffer( &idb );

        bgfx::submit( id, program );

        offset += available;
        remaining -= available;
    }
}

void BGFXRenderer::WindowResized( const Vector2& newSize )
{
    if( CurrentSize == newSize )
    {
        return;
    }
    CurrentSize = newSize;

#if USING( ME_EDITOR )
    if( EditorCameraBuffer )
    {
        EditorCameraBuffer->Width = newSize.x;
        EditorCameraBuffer->Height = newSize.y;
        EditorCameraBuffer->ReCreate( m_resetFlags );
    }
#endif
    for( auto cam : m_cameraCache.Commands )
    {
        if( cam.Buffer && ( cam.IsMain || cam.Buffer->MatchMainBufferSize ) )
        {
            cam.Buffer->Width = newSize.x;
            cam.Buffer->Height = newSize.y;
            cam.Buffer->ReCreate( m_resetFlags );
        }
    }
}


uint32_t BGFXRenderer::GetResetFlags() const
{
    return m_resetFlags;
}

CommandCache<Moonlight::CameraData>& BGFXRenderer::GetCameraCache()
{
    return m_cameraCache;
}

CommandCache<Moonlight::MeshCommand>& BGFXRenderer::GetMeshCache()
{
    return m_meshCache;
}

CommandCache<Moonlight::DebugColliderCommand>& BGFXRenderer::GetDebugDrawCache()
{
    return m_debugDrawCache;
}

void BGFXRenderer::UpdateMeshMatrix( unsigned int Id, const glm::mat4& matrix )
{
    if( Id >= m_meshCache.Commands.size() )
    {
        return;
    }

    OPTICK_CATEGORY( "UpdateMeshMatrix", Optick::Category::Rendering );

    m_meshCache.Commands[Id].Transform = matrix;
}

void BGFXRenderer::ClearMeshes()
{
    m_meshCache.Commands.clear();
    while( !m_meshCache.FreeIndicies.empty() )
    {
        m_meshCache.FreeIndicies.pop();
    }
}

SharedPtr<Moonlight::DynamicSky> BGFXRenderer::GetSky()
{
    return m_dynamicSky;
}

void BGFXRenderer::RecreateFrameBuffer( uint32_t index )
{
    m_cameraCache.Get( index )->Buffer->ReCreate( m_resetFlags );
}

void BGFXRenderer::SetMSAALevel( MSAALevel level )
{
    m_resetFlags &= ~( m_resetFlags & BGFX_RESET_MSAA_MASK );

    switch( level )
    {
    case BGFXRenderer::X2:
        m_resetFlags |= BGFX_RESET_MSAA_X2;
        break;
    case BGFXRenderer::X4:
        m_resetFlags |= BGFX_RESET_MSAA_X4;
        break;
    case BGFXRenderer::X8:
        m_resetFlags |= BGFX_RESET_MSAA_X8;
        break;
    case BGFXRenderer::X16:
        m_resetFlags |= BGFX_RESET_MSAA_X16;
        break;
    case BGFXRenderer::None:
    default:
        break;
    }

    NeedsReset = true;
}


void BGFXRenderer::RequestScreenshot( const std::string& filePath )
{
    bgfx::requestScreenShot( BGFX_INVALID_HANDLE, filePath.c_str() );
}


uint32_t BGFXRenderer::GetScreenshotCount() const
{
    return m_bgfxCallback.GetScreenshotCount();
}

#if USING( ME_IMGUI )
ImGuiRenderer* BGFXRenderer::GetImGuiRenderer() const
{
    return ImGuiRender;
}
#endif

void BGFXRenderer::SetDebugDrawEnabled( bool inEnabled )
{
    EnableDebugDraw = inEnabled;
}
