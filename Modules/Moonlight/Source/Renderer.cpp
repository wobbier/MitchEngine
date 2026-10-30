#include <Renderer.h>

#include <bgfx/bgfx.h>
#include <ImGui/ImGuiRenderer.h>
#include "optick.h"
#include "Jobs/JobSystem.h"
#include "imgui.h"
#include "Utils/BGFXUtils.h"
#include "bx/timer.h"
#include <bx/string.h>
#include "Graphics/Material.h"
#include "Graphics/ShaderStructures.h"
#include "Primitives/Cube.h"
#include "Graphics/MeshData.h"
#include "Graphics/ShaderCommand.h"
#include "Graphics/DefaultTextures.h"
#include "Materials/ShaderGraphAsset.h"
#include "Materials/PortalMaterial.h"
#include <algorithm>
#include <cstring>
#include "Resource/ResourceCache.h"
#include <Graphics/SkyBox.h>
#include <Math/Matrix4.h>
#include <Graphics/DynamicSky.h>
#include <Graphics/ModelResource.h>
#include <Debug/DebugDrawer.h>
#include <map>
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
    init.swapChain.nwh  = settings.WindowPtr;
    init.swapChain.ndt  = settings.DisplayPtr;
    init.platformData.type = settings.WindowType;
    init.callback = &m_bgfxCallback;
    init.swapChain.width  = static_cast<uint32_t>( PreviousSize.x );
    init.swapChain.height = static_cast<uint32_t>( PreviousSize.y );
#if USING( ME_PLATFORM_MACOS )
    init.reset = BGFX_RESET_VSYNC;
#else
    init.reset = BGFX_RESET_NONE;// | BGFX_RESET_MSAA_X16;
#endif

#if USING( ME_PLATFORM_UWP )
    // Something is up with using DX12 and my shaders, so this is the fix for now.
    init.type = bgfx::RendererType::Direct3D11;
#elif USING( ME_PLATFORM_LINUX )
    init.type = bgfx::RendererType::Vulkan;
#endif
    if( settings.Type != bgfx::RendererType::Count )
    {
        init.type = settings.Type;
    }
    m_resetFlags = init.reset;
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
        Moonlight::SetGpuAlive( true );
    }

    // Set view 0 clear state.
    bgfx::setViewClear( kClearView
        , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        , 0x303030ff
        , 1.0f
        , 0
    );

    m_hasAssets = settings.InitAssets;
    if( settings.InitAssets )
    {
        EditorCameraBuffer = new Moonlight::FrameBuffer( init.swapChain.width, init.swapChain.height );
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
        Moonlight::WindVertex::Init();
        Moonlight::PosTexCoordVertex::Init();

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
        // Every engine shader at once: first-time cooks and binary reads run on the loader threads
        // in parallel, then the passes below (and later materials) find them loaded.
        m_engineShaders = Moonlight::PrefetchShaders( Moonlight::FindEngineShaderStages() );
        UIProgram = Moonlight::LoadProgram( "Assets/Shaders/UI.vert", "Assets/Shaders/UI.frag" );
        s_texDiffuse = bgfx::createUniform( "s_texDiffuse", bgfx::UniformType::Sampler );
        s_texNormal = bgfx::createUniform( "s_texNormal", bgfx::UniformType::Sampler );
        s_texAlpha = bgfx::createUniform( "s_texAlpha", bgfx::UniformType::Sampler );
        s_texUI = bgfx::createUniform( "s_texUI", bgfx::UniformType::Sampler );
        u_screenView = bgfx::createUniform( "u_screenView", bgfx::UniformType::Vec4 );
        // Per-frame uniforms apply to every draw in the frame regardless of view/sort order.

        m_timeOffset = bx::getHPCounter();

        m_dynamicSky = MakeShared<Moonlight::DynamicSky>();
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
        m_grayTexture = makeSolidTexture( 0xFF808080u, "Default Gray" );
        Moonlight::SetDefaultTexture( Moonlight::DefaultTexture::White, m_whiteTexture );
        Moonlight::SetDefaultTexture( Moonlight::DefaultTexture::Black, m_blackTexture );
        Moonlight::SetDefaultTexture( Moonlight::DefaultTexture::Gray, m_grayTexture );
        Moonlight::SetDefaultTexture( Moonlight::DefaultTexture::FlatNormal, m_flatNormalTexture );

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
        u_viewShading = bgfx::createUniform( "u_viewShading", bgfx::UniformType::Vec4 );
        u_eraParams = bgfx::createUniform( "u_eraParams", bgfx::UniformType::Vec4 );
        u_dirLightDirection = bgfx::createUniform( "u_dirLightDirection", bgfx::UniformType::Vec4, 4 );
        u_dirLightColor = bgfx::createUniform( "u_dirLightColor", bgfx::UniformType::Vec4, 4 );
        u_eclipseSun = bgfx::createUniform( "u_eclipseSun", bgfx::UniformType::Vec4 );
        u_eclipseMoon = bgfx::createUniform( "u_eclipseMoon", bgfx::UniformType::Vec4 );
        u_eclipseParams = bgfx::createUniform( "u_eclipseParams", bgfx::UniformType::Vec4 );
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
        s_texMetallic = bgfx::createUniform( "s_texMetallic", bgfx::UniformType::Sampler );
        s_texRoughness = bgfx::createUniform( "s_texRoughness", bgfx::UniformType::Sampler );

        // Shadows (see RenderSunShadows / RenderLocalShadows).
        u_shadowMatrix = bgfx::createUniform( "u_shadowMatrix", bgfx::UniformType::Mat4, 4 );
        u_cascadeSplits = bgfx::createUniform( "u_cascadeSplits", bgfx::UniformType::Vec4 );
        u_cascadeTexel = bgfx::createUniform( "u_cascadeTexel", bgfx::UniformType::Vec4 );
        u_shadowParams = bgfx::createUniform( "u_shadowParams", bgfx::UniformType::Vec4 );
        u_localShadowMatrix = bgfx::createUniform( "u_localShadowMatrix", bgfx::UniformType::Mat4, 16 );
        u_spotShadowParams = bgfx::createUniform( "u_spotShadowParams", bgfx::UniformType::Vec4 );
        u_pointShadowParams = bgfx::createUniform( "u_pointShadowParams", bgfx::UniformType::Vec4 );
        u_shadowAlpha = bgfx::createUniform( "u_shadowAlpha", bgfx::UniformType::Vec4 );
        u_drawParams = bgfx::createUniform( "u_drawParams", bgfx::UniformType::Vec4 );
        u_shadowUvOffset = bgfx::createUniform( "u_shadowUvOffset", bgfx::UniformType::Vec4 );
        s_shadowMap = bgfx::createUniform( "s_shadowMap", bgfx::UniformType::Sampler );
        s_spotShadowMap = bgfx::createUniform( "s_spotShadowMap", bgfx::UniformType::Sampler );
        m_shadowProgram = Moonlight::ShaderCommand( "Assets/Shaders/ShadowDepth" );
        m_shadowSkinnedProgram = Moonlight::ShaderCommand( "Assets/Shaders/ShadowDepthSkinned", "Assets/Shaders/ShadowDepth" );
        m_overdrawProgram = Moonlight::ShaderCommand( "Assets/Shaders/ShadowDepth", "Assets/Shaders/Overdraw" );
        m_overdrawSkinnedProgram = Moonlight::ShaderCommand( "Assets/Shaders/ShadowDepthSkinned", "Assets/Shaders/Overdraw" );
        m_outlineHullProgram = Moonlight::ShaderCommand( "Assets/Shaders/OutlineHull", "Assets/Shaders/OutlineHull" );
        m_outlineHullSkinnedProgram = Moonlight::ShaderCommand( "Assets/Shaders/OutlineHullSkinned", "Assets/Shaders/OutlineHull" );
        u_outlineHull = bgfx::createUniform( "u_outlineHull", bgfx::UniformType::Vec4 );
        u_bones = bgfx::createUniform( "u_bones", bgfx::UniformType::Mat4, Moonlight::MeshData::kMaxBones );
        m_shadowsSupported = m_shadowProgram.IsLoaded();
        if( !m_shadowsSupported )
        {
            YIKES( "Shadows disabled: the renderer has no depth compare samplers or the shadow shader failed to load." );
        }
        s_envSpecular = bgfx::createUniform( "s_envSpecular", bgfx::UniformType::Sampler );
        s_envIrradiance = bgfx::createUniform( "s_envIrradiance", bgfx::UniformType::Sampler );
        s_brdfLut = bgfx::createUniform( "s_brdfLut", bgfx::UniformType::Sampler );
        u_envParams = bgfx::createUniform( "u_envParams", bgfx::UniformType::Vec4 );
        u_fogParams = bgfx::createUniform( "u_fogParams", bgfx::UniformType::Vec4 );
        u_fogColor = bgfx::createUniform( "u_fogColor", bgfx::UniformType::Vec4 );
        u_fogVolume = bgfx::createUniform( "u_fogVolume", bgfx::UniformType::Vec4 );
        u_fogForward = bgfx::createUniform( "u_fogForward", bgfx::UniformType::Vec4 );
        m_environment = MakeUnique<Moonlight::EnvironmentLighting>();
        m_environment->Init();
        Moonlight::SetDefaultTexture( Moonlight::DefaultTexture::BlackCube, m_environment->GetFallbackCube() );

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

            const uint32_t white = 0xffffffff;
            m_spriteWhite = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy( &white, sizeof( white ) ) );
            bgfx::setName( m_spriteWhite, "Sprite White" );
            s_texSprite = bgfx::createUniform( "s_texSprite", bgfx::UniformType::Sampler );
            s_texSpriteNormal = bgfx::createUniform( "s_texSpriteNormal", bgfx::UniformType::Sampler );
            u_spriteParams = bgfx::createUniform( "u_spriteParams", bgfx::UniformType::Vec4 );
            m_spriteProgram = Moonlight::ShaderCommand( "Assets/Shaders/Sprite" );
        }
        // Decals: a unit box (its outward faces wind clockwise seen from outside) and the decal shader.
        {
            Moonlight::PosTexCoordVertex box[8];
            for( int i = 0; i < 8; ++i )
            {
                box[i] = { Vector3( ( i == 1 || i == 2 || i == 5 || i == 6 ) ? 0.5f : -0.5f, ( i == 2 || i == 3 || i == 6 || i == 7 ) ? 0.5f : -0.5f, i >= 4 ? 0.5f : -0.5f ), Vector2( 0.f, 0.f ) };
            }
            const uint16_t indices[36] = { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 7, 3, 0, 4, 7, 1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 6, 2, 3, 7, 6 };
            m_decalBoxVB = bgfx::createVertexBuffer( bgfx::copy( box, sizeof( box ) ), Moonlight::PosTexCoordVertex::ms_layout );
            m_decalBoxIB = bgfx::createIndexBuffer( bgfx::copy( indices, sizeof( indices ) ) );
            s_decalAlbedo = bgfx::createUniform( "s_decalAlbedo", bgfx::UniformType::Sampler );
            s_decalNormal = bgfx::createUniform( "s_decalNormal", bgfx::UniformType::Sampler );
            u_decalColor = bgfx::createUniform( "u_decalColor", bgfx::UniformType::Vec4 );
            u_decalEmissive = bgfx::createUniform( "u_decalEmissive", bgfx::UniformType::Vec4 );
            u_decalSurface = bgfx::createUniform( "u_decalSurface", bgfx::UniformType::Vec4 );
            u_decalDepth = bgfx::createUniform( "u_decalDepth", bgfx::UniformType::Vec4 );
            u_decalTexSize = bgfx::createUniform( "u_decalTexSize", bgfx::UniformType::Vec4 );
            u_decalWorldToLocal = bgfx::createUniform( "u_decalWorldToLocal", bgfx::UniformType::Mat4 );
            u_decalAxes = bgfx::createUniform( "u_decalAxes", bgfx::UniformType::Mat4 );
            m_decalProgram = Moonlight::ShaderCommand( "Assets/Shaders/Decal" );
        }
        // The lighting shader always declares the shadow samplers, so the atlases always exist.
        EnsureShadowAtlas( m_sunShadowAtlas, CapShadowResolution( Shadows.CascadeResolution ), "Sun Shadow Atlas" );
        EnsureLocalShadowAtlas( CapShadowResolution( Shadows.SpotResolution ), CapShadowResolution( Shadows.PointResolution ) );
    }
    s_time = bgfx::createUniform( "u_time", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4 );
    u_wind = bgfx::createUniform( "u_wind", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4 );
    u_windParams = bgfx::createUniform( "u_windParams", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4 );
    TransparentIndicies.reserve( kMeshTransparencyTempSize );

#if USING( ME_IMGUI )
    {
        OPTICK_EVENT( "BGFXRenderer::ImGuiCreate" );
        ImGuiRender = new ImGuiRenderer();
        ImGuiRender->Create();
    }
#endif


    ResetMainSwapChain();
    bgfx::setViewRect( kClearView, 0, 0, bgfx::BackbufferRatio::Equal );
}


void BGFXRenderer::ResetMainSwapChain()
{
    // Headless (Noop): there's no back buffer to set up (bgfx asserts BGRA8 can't be one there).
    if( bgfx::getRendererType() == bgfx::RendererType::Noop )
    {
        return;
    }
    constexpr uint32_t swapChainFlags = BGFX_SWAP_CHAIN_MSAA_MASK | BGFX_SWAP_CHAIN_SRGB_BACKBUFFER | BGFX_SWAP_CHAIN_HDR10
        | BGFX_SWAP_CHAIN_HIDPI | BGFX_SWAP_CHAIN_TRANSPARENT_BACKBUFFER;
    bgfx::SwapChain swapChain;
    swapChain.width = static_cast<uint32_t>( CurrentSize.x );
    swapChain.height = static_cast<uint32_t>( CurrentSize.y );
    swapChain.flags = m_resetFlags & swapChainFlags;
    bgfx::reset( m_resetFlags & ~swapChainFlags, &swapChain );
}


BGFXRenderer::BGFXRenderer()
    : m_pt( 0 )
    , m_timeOffset( 0 )
{
}


BGFXRenderer::~BGFXRenderer() = default;


namespace
{
    // Samplers of the shader graph particle / decal templates (stages 6 and 7, after the graph's own).
    std::map<std::string, bgfx::UniformHandle>& GraphSamplers()
    {
        static std::map<std::string, bgfx::UniformHandle> samplers;
        return samplers;
    }


    bgfx::UniformHandle GraphUniform( const char* InName, bgfx::UniformType::Enum InType )
    {
        auto found = GraphSamplers().find( InName );
        if( found == GraphSamplers().end() )
        {
            found = GraphSamplers().emplace( InName, bgfx::createUniform( InName, InType ) ).first;
        }
        return found->second;
    }


    // Post Process shader graphs injected at InInjection, in order: each reads the previous image
    // (InInput) and writes the next. The last writes InFinal when given (after tonemapping),
    // otherwise the passes ping-pong in the HDR targets and the last image is returned.
    bgfx::TextureHandle RenderGraphPostPasses( Moonlight::PostProcess& InPostProcess, Moonlight::ViewAllocator& InViews, const Moonlight::CameraData& InCamera, Moonlight::FrameBuffer& InTargets, const char* InInjection, bgfx::TextureHandle InInput, bgfx::FrameBufferHandle InFinal, uint16_t InFinalWidth, uint16_t InFinalHeight, uint16_t InFinalX = 0, uint16_t InFinalY = 0 )
    {
        std::vector<const Moonlight::ShaderGraphAsset*> passes;
        std::vector<const Moonlight::ShaderGraphBinding*> bindings;
        for( size_t i = 0; i < InCamera.Post.Graphs.size(); ++i )
        {
            const SharedPtr<Moonlight::ShaderGraphAsset>& graph = InCamera.Post.Graphs[i];
            if( graph && graph->GetManifest().value( "Injection", std::string( "BeforeTonemap" ) ) == InInjection && bgfx::isValid( graph->GetProgramHandle( "Forward" ) ) )
            {
                passes.push_back( graph.get() );
                bindings.push_back( i < InCamera.Post.GraphBindings.size() ? &InCamera.Post.GraphBindings[i] : nullptr );
            }
        }
        if( passes.empty() )
        {
            return InInput;
        }
        const bool hdr = !bgfx::isValid( InFinal ) && InFinalWidth == 0;
        InTargets.EnsureGraphTargets( hdr, !hdr );
        Moonlight::FrameBuffer::Mip* pingPong = hdr ? InTargets.GraphHdr : InTargets.GraphLdr;

        const glm::mat4& projection = InCamera.ProjectionMatrix.GetInternalMatrix();
        const float projection1[4] = { projection[2][2], projection[3][2], InCamera.Projection == Moonlight::ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
        const float projection2[4] = { projection[0][0], projection[1][1], InCamera.Far, 0.f };
        const glm::mat4 invView = glm::inverse( InCamera.View.GetInternalMatrix() );
        float ortho[16];
        bx::mtxOrtho( ortho, 0.f, 1.f, 1.f, 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        // The final pass into a render texture on a bottom-up backend (PostProcess::FlippedTarget).
        float flippedOrtho[16];
        bx::mtxOrtho( flippedOrtho, 0.f, 1.f, 0.f, 1.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        const bool flipFinal = bgfx::isValid( InFinal ) && InFinal.idx == InPostProcess.FlippedTarget.idx;

        bgfx::TextureHandle input = InInput;
        // After tonemapping the first pass reads InInput (the tonemapped image in GraphLdr[0]).
        int write = hdr ? 0 : 1;
        for( size_t i = 0; i < passes.size(); ++i )
        {
            bgfx::TextureHandle sceneMips = BGFX_INVALID_HANDLE;
            if( passes[i]->GetManifest().value( "SceneColorMips", false ) )
            {
                float threshold = -1.f;
                const int slot = passes[i]->GetManifest().value( "SceneColorMipThresholdSlot", -1 );
                if( slot >= 0 && slot < Moonlight::ShaderGraphAsset::kMaxParameters )
                {
                    Vector4 defaults[Moonlight::ShaderGraphAsset::kMaxParameters];
                    if( !bindings[i] ) passes[i]->PackParameters( {}, defaults );
                    threshold = std::max( ( bindings[i] ? bindings[i]->Block[slot] : defaults[slot] ).x, 0.f );
                }
                sceneMips = InPostProcess.RenderGraphSceneMips( InViews, InTargets, input, threshold );
            }
            const bool last = i + 1 == passes.size();
            const bgfx::ViewId view = InViews.Allocate( hdr ? "Shader Graph Post (HDR)" : "Shader Graph Post" );
            if( view == UINT16_MAX )
            {
                break;
            }
            Moonlight::FrameBuffer::Mip& target = pingPong[write];
            const bool toFinal = last && !hdr;
            bgfx::setViewFrameBuffer( view, toFinal ? InFinal : Moonlight::FrameBuffer::GuardTarget( target.Buffer ) );
            bgfx::setViewRect( view, toFinal ? InFinalX : 0, toFinal ? InFinalY : 0, toFinal ? InFinalWidth : target.Width, toFinal ? InFinalHeight : target.Height );
            bgfx::setViewTransform( view, nullptr, toFinal && flipFinal ? flippedOrtho : ortho );
            bgfx::touch( view );

            if( bindings[i] )
            {
                passes[i]->Bind( *bindings[i] );
            }
            else
            {
                Vector4 parameters[Moonlight::ShaderGraphAsset::kMaxParameters];
                passes[i]->PackParameters( {}, parameters );
                passes[i]->Bind( parameters, {} );
            }
            bgfx::setTexture( 6, GraphUniform( "s_sgSceneColor", bgfx::UniformType::Sampler ), input, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
            if( bgfx::isValid( sceneMips ) )
            {
                bgfx::setTexture( 8, GraphUniform( "s_sgSceneColorMips", bgfx::UniformType::Sampler ), sceneMips, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
                const float mipSize[4] = { static_cast<float>( InTargets.GraphSceneMips.Width ), static_cast<float>( InTargets.GraphSceneMips.Height ), 0.f, 0.f };
                bgfx::setUniform( GraphUniform( "u_sgSceneColorMipSize", bgfx::UniformType::Vec4 ), mipSize );
            }
            bgfx::setTexture( 7, GraphUniform( "s_sgSceneDepth", bgfx::UniformType::Sampler ), InTargets.DepthTexture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
            bgfx::setUniform( GraphUniform( "u_sgPostProjection", bgfx::UniformType::Vec4 ), projection1 );
            bgfx::setUniform( GraphUniform( "u_sgPostProjection2", bgfx::UniformType::Vec4 ), projection2 );
            bgfx::setUniform( GraphUniform( "u_sgPostInvView", bgfx::UniformType::Mat4 ), &invView[0][0] );
            bgfx::setState( BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A );
            Moonlight::screenSpaceQuad( 1.f, 1.f, 0.f, bgfx::getCaps()->originBottomLeft );
            bgfx::submit( view, passes[i]->GetProgramHandle( "Forward" ) );

            input = target.Color;
            write = 1 - write;
        }
        return input;
    }


    bgfx::UniformHandle GraphSampler( const char* InName )
    {
        // Not operator[]: a value-initialised handle is index 0, a valid (someone else's) uniform.
        auto found = GraphSamplers().find( InName );
        if( found == GraphSamplers().end() )
        {
            found = GraphSamplers().emplace( InName, bgfx::createUniform( InName, bgfx::UniformType::Sampler ) ).first;
        }
        return found->second;
    }
}


void BGFXRenderer::Destroy()
{
    // Release everything the renderer owns before shutting bgfx down.
#if USING( ME_IMGUI )
    if( ImGuiRender )
    {
        ImGuiRender->Destroy();
        delete ImGuiRender;
        ImGuiRender = nullptr;
    }
#endif
#if USING( ME_EDITOR )
    m_pickingPass.reset();
#endif
    m_postProcess.reset();
    // Commands can hold shader graph assets (and so programs): drop them while bgfx is alive.
    m_particleBatches.clear();
    m_decals.clear();
    for( auto& sampler : GraphSamplers() )
    {
        if( bgfx::isValid( sampler.second ) )
        {
            bgfx::destroy( sampler.second );
        }
    }
    GraphSamplers().clear();
    Moonlight::ShaderGraphAsset::DestroyUniforms();
    Moonlight::Primitives::Shutdown();
    m_dynamicSky.reset();
    m_debugDraw.reset();
    m_defaultOpacityTexture.reset();
    m_engineShaders.clear();
    delete EditorCameraBuffer;
    EditorCameraBuffer = nullptr;
    delete m_splitMainBuffer;
    m_splitMainBuffer = nullptr;

    const bgfx::UniformHandle uniforms[] = { s_texDiffuse, s_texNormal, s_texAlpha, s_texUI, s_time, u_wind, u_windParams, u_screenView,
        u_lightParams, u_viewShading, u_eraParams, u_dirLightDirection, u_dirLightColor, u_eclipseSun, u_eclipseMoon, u_eclipseParams, u_clusterParams, u_clusterGrid, u_ambientSky, u_ambientGround,
        s_lightData, s_clusterGrid, s_clusterIndices, s_texMetallicRoughness, s_texEmissive, s_texOcclusion, s_texMetallic, s_texRoughness,
        u_shadowMatrix, u_cascadeSplits, u_cascadeTexel, u_shadowParams, u_localShadowMatrix, u_spotShadowParams, u_pointShadowParams, u_shadowAlpha, u_drawParams, u_shadowUvOffset, s_shadowMap, s_spotShadowMap,
        s_envSpecular, s_envIrradiance, s_brdfLut, u_envParams, u_fogParams, u_fogColor, u_fogVolume, u_fogForward,
        u_particleParams, u_particleParams2, u_particleDepth, s_texParticle, s_sceneDepth, s_texSprite, s_texSpriteNormal, u_spriteParams, u_bones, u_outlineHull,
        s_decalAlbedo, s_decalNormal, u_decalColor, u_decalEmissive, u_decalSurface, u_decalDepth, u_decalTexSize, u_decalWorldToLocal, u_decalAxes };
    m_particleProgram = Moonlight::ShaderCommand();
    m_spriteProgram = Moonlight::ShaderCommand();
    if( bgfx::isValid( m_spriteWhite ) )
    {
        bgfx::destroy( m_spriteWhite );
        m_spriteWhite = BGFX_INVALID_HANDLE;
    }
    m_decalProgram = Moonlight::ShaderCommand();
    if( bgfx::isValid( m_decalBoxVB ) )
    {
        bgfx::destroy( m_decalBoxVB );
    }
    if( bgfx::isValid( m_decalBoxIB ) )
    {
        bgfx::destroy( m_decalBoxIB );
    }
    for( bgfx::TextureHandle* texture : { &m_particleTexture, &m_flatNormalTexture, &m_grayTexture } )
    {
        if( bgfx::isValid( *texture ) )
        {
            bgfx::destroy( *texture );
            *texture = BGFX_INVALID_HANDLE;
        }
    }
    for( unsigned int i = 0; i < static_cast<unsigned int>( Moonlight::DefaultTexture::Count ); ++i )
    {
        Moonlight::SetDefaultTexture( static_cast<Moonlight::DefaultTexture>( i ), BGFX_INVALID_HANDLE );
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
    m_overdrawProgram = Moonlight::ShaderCommand();
    m_overdrawSkinnedProgram = Moonlight::ShaderCommand();
    m_outlineHullProgram = Moonlight::ShaderCommand();
    m_outlineHullSkinnedProgram = Moonlight::ShaderCommand();
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

    Moonlight::SetGpuAlive( false );
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

    if( !m_hasAssets )
    {
        // A tool window (Tool, the Project Hub): nothing but ImGui to draw.
        if( CurrentSize != PreviousSize || NeedsReset )
        {
            PreviousSize = CurrentSize;
            NeedsReset = false;
            ResetMainSwapChain();
        }
#if USING( ME_IMGUI )
        ImGuiRender->EndFrame();
#endif
        m_currentFrame = bgfx::frame();
        inFrameData.m_currentFrame = m_currentFrame;
        return;
    }

    if( CurrentSize != PreviousSize || NeedsReset )
    {
        PreviousSize = CurrentSize;

        ResetMainSwapChain();
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
            if( m_splitMainBuffer )
            {
                m_splitMainBuffer->ReCreate( m_resetFlags );
            }
        }
        NeedsReset = false;
    }

    bgfx::setFrameUniform( s_time, &m_time.x );
    {
        const float wind[4] = { m_wind.Direction.x, m_wind.Direction.y, m_wind.Direction.z, m_wind.Strength };
        const float windParams[4] = { m_wind.Gusts, m_wind.GustFrequency, m_wind.Turbulence, 0.f };
        bgfx::setFrameUniform( u_wind, wind );
        bgfx::setFrameUniform( u_windParams, windParams );
    }

    m_views.Reset();
    m_viewTriangles.assign( bgfx::getCaps()->limits.maxViews, 0 );
    m_gameCounts = m_editorCounts = m_frameCounts = m_textureCounts = SceneCounts();
    m_counting = &m_frameCounts;
    ++m_frameIndex;
    DebugDraw::CollectFrame( m_debugLines );
    PrepareFrameLighting();

    // Render texture cameras first, so every view after them (the editor's scene view, other
    // cameras, the main camera) samples this frame's image. Between themselves they go by GUID: one
    // sampling a texture rendered later in the frame sees last frame's image.
    m_textureCameras.clear();
    for( auto& camData : m_cameraCache.Commands )
    {
        if( camData.ShouldRender && RendersToTexture( camData ) )
        {
            m_textureCameras.push_back( &camData );
        }
    }
    std::sort( m_textureCameras.begin(), m_textureCameras.end(), []( const Moonlight::CameraData* InA, const Moonlight::CameraData* InB ) {
        return InA->TargetOrder < InB->TargetOrder;
    } );
    for( Moonlight::CameraData* camData : m_textureCameras )
    {
        RenderCameraView( *camData, false );
    }

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

    auto isSplit = []( const Moonlight::CameraData& camData ) {
        return camData.Viewport != glm::vec4( 0.f, 0.f, 1.f, 1.f );
    };
    Moonlight::CameraData* mainCamera = nullptr;
    for( auto& camData : m_cameraCache.Commands )
    {
        if( camData.IsMain && camData.ShouldRender && camData.Buffer )
        {
            mainCamera = &camData;
            break;
        }
    }

    // Secondary cameras first so the main camera can sample their output this frame. Split screen
    // cameras draw into the main camera's output, so they wait until it has rendered.
    for( auto& camData : m_cameraCache.Commands )
    {
        if( camData.IsMain || !camData.ShouldRender || RendersToTexture( camData ) || ( mainCamera && isSplit( camData ) ) )
        {
            continue;
        }
        RenderCameraView( camData, false );
    }

    if( mainCamera )
    {
        if( isSplit( *mainCamera ) )
        {
            // Keep the main buffer at the full display size; the scene renders at the rect's size.
            if( !m_splitMainBuffer )
            {
                m_splitMainBuffer = new Moonlight::FrameBuffer( static_cast<uint32_t>( mainCamera->OutputSize.x ), static_cast<uint32_t>( mainCamera->OutputSize.y ) );
            }
            Moonlight::FrameBuffer* display = mainCamera->Buffer;
            mainCamera->Buffer = m_splitMainBuffer;
            RenderCameraView( *mainCamera, kMainCameraToBackbuffer, display );
            mainCamera->Buffer = display;
        }
        else
        {
            RenderCameraView( *mainCamera, kMainCameraToBackbuffer );
        }

        for( auto& camData : m_cameraCache.Commands )
        {
            if( !camData.IsMain && camData.ShouldRender && isSplit( camData ) )
            {
                RenderCameraView( camData, kMainCameraToBackbuffer, mainCamera->Buffer );
            }
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
    m_counting = nullptr;
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
    out.TextureMemoryUsed = stats->textureMemoryUsed;
    out.RenderTargetMemoryUsed = stats->rtMemoryUsed;
    out.MeshMemoryUsed = Moonlight::MeshData::GetTotalGpuBytes();
    // The game's cameras when any rendered, else the editor's views; local shadows serve both.
    // Render texture feeds render for both.
    const SceneCounts& scene = m_gameCounts.Rendered ? m_gameCounts : m_editorCounts;
    out.SceneDrawCalls = scene.Draws + m_frameCounts.Draws + m_textureCounts.Draws;
    out.SpriteBatches = m_spriteBatches;
    m_spriteBatches = 0;
    out.SceneTriangles = scene.Triangles + m_frameCounts.Triangles + m_textureCounts.Triangles;
    out.SceneWidth = scene.Width;
    out.SceneHeight = scene.Height;
    out.Lights = static_cast<uint32_t>( m_lights.size() );
    out.Views.clear();
    for( uint16_t i = 0; i < stats->numViews; ++i )
    {
        const bgfx::ViewStats& view = stats->viewStats[i];
        const uint64_t triangles = view.view < m_viewTriangles.size() ? m_viewTriangles[view.view] : 0;
        out.Views.push_back( { view.name, double( view.cpuTimeEnd - view.cpuTimeBegin ) * 1000.0 / cpuFrequency, double( view.gpuTimeEnd - view.gpuTimeBegin ) * 1000.0 / gpuFrequency, triangles } );
    }
    if( stats->numViews == 0 )
    {
        // No per-view timings from this backend/build: still report where the triangles went.
        for( size_t id = 0; id < m_viewTriangles.size(); ++id )
        {
            if( m_viewTriangles[id] > 0 )
            {
                const std::string& name = m_views.GetName( static_cast<bgfx::ViewId>( id ) );
                out.Views.push_back( { name.empty() ? "View " + std::to_string( id ) : name, 0.0, 0.0, m_viewTriangles[id] } );
            }
        }
    }
}


void BGFXRenderer::SetGuizmoDrawCallback( std::function<void( DebugDrawer* )> GuizmoDrawingFunc )
{
    m_guizmoCallback = GuizmoDrawingFunc;
}


void BGFXRenderer::RenderCameraView( Moonlight::CameraData& camera, bool toBackbuffer, Moonlight::FrameBuffer* displayBuffer )
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
    m_drawingBuffer = camera.Buffer;

    // The output size, and the internal size the scene and post passes render at (render scaling,
    // LOOK-S1). Camera targets match the internal size exactly: post passes process whole textures.
    const uint16_t outputWidth = static_cast<uint16_t>( std::max( 1.f, camera.OutputSize.x ) );
    const uint16_t outputHeight = static_cast<uint16_t>( std::max( 1.f, camera.OutputSize.y ) );
    // The player's graphics settings scale every game camera (editor views and pixel-perfect
    // cameras keep theirs), restored when the camera's done: the cached data persists.
    struct PlayerGraphicsScope
    {
        Moonlight::CameraData& Camera;
        float RenderScale;
        bool DynamicResolution;
        Moonlight::UpscaleFilter Upscale;
        ~PlayerGraphicsScope()
        {
            Camera.RenderScale = RenderScale;
            Camera.DynamicResolution = DynamicResolution;
            Camera.Upscale = Upscale;
        }
    } playerGraphics{ camera, camera.RenderScale, camera.DynamicResolution, camera.Upscale };
    if( !camera.IsEditorView && camera.RenderGroup == 0 && !camera.PixelPerfect )
    {
        camera.RenderScale *= std::clamp( FeatureCaps.PlayerRenderScale, 0.1f, 1.f );
        camera.DynamicResolution = camera.DynamicResolution || FeatureCaps.PlayerDynamicResolution;
        if( FeatureCaps.PlayerUpscale >= 0 && FeatureCaps.PlayerUpscale <= static_cast<int>( Moonlight::UpscaleFilter::Sharp ) )
        {
            camera.Upscale = static_cast<Moonlight::UpscaleFilter>( FeatureCaps.PlayerUpscale );
        }
    }
    float renderScale = camera.RenderScale;
    if( camera.DynamicResolution )
    {
        // The scale itself (RenderScale is the ceiling), retuned every half second or so from the
        // last frame's GPU time (0 when unmeasured).
        Moonlight::FrameBuffer& state = *camera.Buffer;
        const double gpuMs = FrameStats::Get().GetRenderStats().GpuMilliseconds;
        if( ++state.DynamicFrames >= 30 && gpuMs > 0.0 )
        {
            state.DynamicFrames = 0;
            state.DynamicScale = Moonlight::StepDynamicScale( std::min( state.DynamicScale, camera.RenderScale ), static_cast<float>( gpuMs ), camera.DynamicTargetMs, camera.DynamicMinScale, camera.RenderScale );
        }
        renderScale = std::min( state.DynamicScale, camera.RenderScale );
    }
    uint32_t internalWidth = outputWidth;
    uint32_t internalHeight = outputHeight;
    uint32_t pixelFactor = 1;
    // Pixel-perfect cameras keep their whole-texel grid: no caps on top.
    Moonlight::ComputeRenderSize( outputWidth, outputHeight, renderScale, camera.Upscale, camera.PixelPerfect ? 0.f : FeatureCaps.MaxRenderScale, camera.PixelPerfect ? 0 : FeatureCaps.MaxRenderPixels, internalWidth, internalHeight, pixelFactor );
    const uint16_t width = static_cast<uint16_t>( internalWidth );
    const uint16_t height = static_cast<uint16_t>( internalHeight );
    const bool scaled = width != outputWidth || height != outputHeight || pixelFactor > 1;

    // A render texture camera writes its final image into its target texture, which it must never
    // sample itself (materials showing it get black in this camera's views).
    Moonlight::Texture* renderTexture = !toBackbuffer && !displayBuffer && RendersToTexture( camera ) ? camera.Target.get() : nullptr;
    if( renderTexture && ( renderTexture->mWidth != outputWidth || renderTexture->mHeight != outputHeight ) )
    {
        // Resized since the camera update; it renders at the new size next frame.
        return;
    }
    struct DrawingTargetScope
    {
        explicit DrawingTargetScope( bgfx::TextureHandle InHandle ) { Moonlight::Texture::SetDrawingTarget( InHandle ); }
        ~DrawingTargetScope() { Moonlight::Texture::SetDrawingTarget( BGFX_INVALID_HANDLE ); }
    } drawingTarget( renderTexture ? renderTexture->TexHandle : bgfx::TextureHandle( BGFX_INVALID_HANDLE ) );
    // Editor previews (other render groups) aren't part of the scene's statistics; render texture
    // feeds add to it without being the scene's view.
    m_counting = camera.RenderGroup != 0 ? &m_previewCounts : renderTexture ? &m_textureCounts : camera.IsEditorView ? &m_editorCounts : &m_gameCounts;
    if( !m_counting->Rendered || camera.IsMain )
    {
        m_counting->Width = width;
        m_counting->Height = height;
    }
    m_counting->Rendered = true;
    // A render texture camera's final image goes into the texture: no output target of its own.
    const bool ownsOutput = renderTexture == nullptr;
    if( camera.Buffer->Width != width || camera.Buffer->Height != height || camera.Buffer->GetOutputWidth() != outputWidth || camera.Buffer->GetOutputHeight() != outputHeight
        || camera.Buffer->OwnsOutput != ownsOutput || !bgfx::isValid( camera.Buffer->SceneBuffer ) )
    {
        camera.Buffer->Width = width;
        camera.Buffer->Height = height;
        camera.Buffer->OutputWidth = outputWidth;
        camera.Buffer->OutputHeight = outputHeight;
        camera.Buffer->OwnsOutput = ownsOutput;
        camera.Buffer->ReCreate( m_resetFlags );
    }

    // Out of framebuffers (bgfx's fixed pool, ME-274): skip the camera rather than let passes with
    // no target draw into the backbuffer. Retried every couple of seconds, in case some came free.
    if( camera.Buffer->IsIncomplete() )
    {
        if( ++camera.Buffer->IncompleteFrames % 120 == 0 )
        {
            camera.Buffer->ReCreate( m_resetFlags );
        }
        if( camera.Buffer->IsIncomplete() )
        {
            if( !camera.Buffer->LoggedIncomplete )
            {
                camera.Buffer->LoggedIncomplete = true;
                YIKES( "Camera skipped: it's missing framebuffers (bgfx's pool is full). Turn off its post effects or use fewer cameras." );
            }
            return;
        }
    }
    camera.Buffer->IncompleteFrames = 0;
    struct CurrentBufferScope
    {
        explicit CurrentBufferScope( Moonlight::FrameBuffer* InBuffer ) { Moonlight::FrameBuffer::SetCurrent( InBuffer ); }
        ~CurrentBufferScope() { Moonlight::FrameBuffer::SetCurrent( nullptr ); }
    } currentBuffer( camera.Buffer );

    // Temporal AA: this frame's unjittered view-projection (for reprojection), then a sub-pixel
    // jitter on the projection the scene draws with (restored when the view is done).
    Moonlight::FrameBuffer& targets = *camera.Buffer;
    struct CameraRestore
    {
        Moonlight::CameraData& Camera;
        Matrix4 Projection;
        Moonlight::PostProcessSettings Post;
        ~CameraRestore()
        {
            Camera.ProjectionMatrix = Projection;
            Camera.Post = Post;
        }
    } restoreCamera{ camera, camera.ProjectionMatrix, camera.Post };
    const auto& options = camera.ViewOptions;
    const bool lit = camera.Shading == Moonlight::ViewShadingMode::Lit || camera.Shading == Moonlight::ViewShadingMode::LightingOnly;
    const bool sceneEffects = camera.Shading == Moonlight::ViewShadingMode::Lit;
    const bool wireframe = camera.Shading == Moonlight::ViewShadingMode::Wireframe;
    const bool depthView = camera.Shading == Moonlight::ViewShadingMode::Depth;
    const bool overdraw = camera.Shading == Moonlight::ViewShadingMode::Overdraw && m_overdrawProgram.IsLoaded();
    const bool background = options.Background && !wireframe && !depthView && !overdraw;
    if( !lit || !options.PostProcessing )
    {
        // Inspection modes need a neutral display transform, independent of scene settings.
        // Keep the camera's authored post settings intact when this render finishes.
        camera.Post = Moonlight::PostProcessSettings();
        camera.Post.Operator = Moonlight::Tonemapper::None;
        camera.Post.Bloom = false;
        camera.Post.AmbientOcclusion = false;
        camera.Post.VignetteIntensity = 0.f;
    }
    if( lit ) camera.Post.ExposureCompensation += options.Exposure;
    camera.Post.Fog = camera.Post.Fog && options.Fog && !overdraw;
    // Effects the target hardware lacks (restored with the rest of the settings afterwards).
    camera.Post.AmbientOcclusion = camera.Post.AmbientOcclusion && FeatureCaps.AmbientOcclusion;
    camera.Post.Bloom = camera.Post.Bloom && FeatureCaps.Bloom;
    camera.Post.DepthOfField = camera.Post.DepthOfField && FeatureCaps.DepthOfField;
    camera.Post.VolumetricFog = camera.Post.VolumetricFog && FeatureCaps.VolumetricFog;
    if( FeatureCaps.MaxVolumetricSteps > 0 )
    {
        camera.Post.VolumetricSteps = std::min( camera.Post.VolumetricSteps, FeatureCaps.MaxVolumetricSteps );
    }
    // A pixel-perfect image doesn't jitter.
    camera.Post.TemporalAA = camera.Post.TemporalAA && FeatureCaps.TemporalAA && !camera.PixelPerfect;
    // The camera's own pass switches (cheaper render texture feeds).
    camera.Post.AmbientOcclusion = camera.Post.AmbientOcclusion && camera.AmbientOcclusion;
    camera.Post.VolumetricFog = camera.Post.VolumetricFog && camera.VolumetricFog;
    {
        const glm::mat4 viewProjection = camera.ProjectionMatrix.GetInternalMatrix() * camera.View.GetInternalMatrix();
        std::copy( &viewProjection[0][0], &viewProjection[0][0] + 16, targets.ViewProjection );
        targets.TemporalJitter[0] = targets.TemporalJitter[1] = 0.f;
        if( camera.Post.TemporalAA )
        {
            float jitter[2];
            Moonlight::PostProcess::TemporalJitter( targets.TemporalFrame, jitter );
            targets.TemporalJitter[0] = 2.f * jitter[0] / static_cast<float>( width );
            targets.TemporalJitter[1] = 2.f * jitter[1] / static_cast<float>( height );
            // Shift NDC by exactly the jitter: through w for perspective (whatever its handedness),
            // the translation for orthographic.
            glm::mat4& projection = camera.ProjectionMatrix.GetInternalMatrix();
            if( camera.Projection == Moonlight::ProjectionType::Orthographic )
            {
                projection[3][0] += targets.TemporalJitter[0];
                projection[3][1] += targets.TemporalJitter[1];
            }
            else
            {
                projection[2][0] += targets.TemporalJitter[0] * projection[2][3];
                projection[2][1] += targets.TemporalJitter[1] * projection[2][3];
            }
        }
    }

    PrepareCameraLighting( camera );
    m_lighting.EraParams[0] = camera.Post.EraVertexSnap;
    m_lighting.EraParams[1] = camera.Post.EraAffineTextures ? 1.f : 0.f;
    m_lighting.EraParams[2] = camera.Post.EraThreePointFilter ? 1.f : 0.f;
    m_lighting.EraParams[3] = 0.f;

    // Ambient: the camera's environment probe (captured and filtered on demand).
    const Moonlight::EnvironmentLighting::Probe* probe = lit && EnableEnvironmentLighting && FeatureCaps.EnvironmentLighting ? m_environment->Prepare( m_views, camera, m_dynamicSky.get(), m_frameIndex ) : nullptr;
    m_lighting.EnvSpecular = probe ? probe->Specular : m_environment->GetFallbackCube();
    m_lighting.EnvIrradiance = probe ? probe->Irradiance : m_environment->GetFallbackCube();
    m_lighting.EnvParams[0] = EnvironmentIntensity;
    m_lighting.EnvParams[1] = static_cast<float>( Moonlight::EnvironmentLighting::kSpecularMips - 1 );
    m_lighting.EnvParams[2] = probe ? 1.f : 0.f;

    if( lit && AreShadowsActive() && camera.SunShadows )
    {
        RenderSunShadows( camera );
    }
    else
    {
        m_lighting.ShadowParams[3] = 0.f;
    }

    const char* label = camera.IsEditorView ? "Editor" : ( camera.IsMain ? "Main" : ( renderTexture ? "Texture" : "Camera" ) );
    char viewName[64];
    const bool hasSkybox = background && camera.ClearType == Moonlight::ClearColorType::Skybox && camera.Skybox
        && camera.Skybox->SkyModel && !camera.Skybox->SkyModel->GetAllMeshes().empty()
        && camera.Skybox->SkyMaterial && camera.Skybox->SkyMaterial->MeshShader.IsLoaded()
        && ( !camera.Skybox->SkyMap || !camera.Skybox->SkyMap->IsLoading() );
    bx::snprintf( viewName, sizeof( viewName ), "%s Skybox", label );
    const bgfx::ViewId skyView = hasSkybox ? m_views.Allocate( viewName ) : UINT16_MAX;
    bx::snprintf( viewName, sizeof( viewName ), "%s Opaque", label );
    const bgfx::ViewId id = m_views.Allocate( viewName );
    if( id == UINT16_MAX )
    {
        return;
    }

    // The scene renders in linear HDR; post-processing writes the final image here.
    Moonlight::FrameBuffer* display = displayBuffer ? displayBuffer : camera.Buffer;
    const bgfx::FrameBufferHandle target = renderTexture ? renderTexture->GetRenderTarget() : ( toBackbuffer ? bgfx::FrameBufferHandle( BGFX_INVALID_HANDLE ) : display->Buffer );
    const uint16_t displayWidth = toBackbuffer ? static_cast<uint16_t>( CurrentSize.x ) : static_cast<uint16_t>( display->GetOutputWidth() );
    const uint16_t displayHeight = toBackbuffer ? static_cast<uint16_t>( CurrentSize.y ) : static_cast<uint16_t>( display->GetOutputHeight() );
    // Split screen: the final image goes into the camera's rect of the display.
    const bool split = displayBuffer || camera.Viewport != glm::vec4( 0.f, 0.f, 1.f, 1.f );
    const uint16_t targetX = split ? static_cast<uint16_t>( std::min<float>( std::round( camera.Viewport.x * displayWidth ), displayWidth - 1 ) ) : 0;
    const uint16_t targetY = split ? static_cast<uint16_t>( std::min<float>( std::round( camera.Viewport.y * displayHeight ), displayHeight - 1 ) ) : 0;
    const uint16_t targetWidth = split ? std::min<uint16_t>( outputWidth, displayWidth - targetX ) : ( toBackbuffer ? displayWidth : outputWidth );
    const uint16_t targetHeight = split ? std::min<uint16_t>( outputHeight, displayHeight - targetY ) : ( toBackbuffer ? displayHeight : outputHeight );
    // Scaled, the post chain ends at the internal size and the upscale pass writes the target.
    if( scaled )
    {
        camera.Buffer->EnsureUpscaleTarget();
    }
    // Era image effects (LOOK-S11) run on the finished image at the render size: the chain then
    // ends in EraInput, and the era pass writes where the chain would have.
    const bool era = camera.Post.HasEraImageEffects();
    if( era )
    {
        camera.Buffer->EnsureEraTarget();
    }
    const bgfx::FrameBufferHandle afterEra = scaled ? camera.Buffer->UpscaleInput.Buffer : target;
    const uint16_t afterEraWidth = scaled ? width : targetWidth;
    const uint16_t afterEraHeight = scaled ? height : targetHeight;
    const uint16_t afterEraX = scaled ? 0 : targetX;
    const uint16_t afterEraY = scaled ? 0 : targetY;
    const bgfx::FrameBufferHandle chainTarget = era ? camera.Buffer->EraInput.Buffer : afterEra;
    const uint16_t chainWidth = era ? width : afterEraWidth;
    const uint16_t chainHeight = era ? height : afterEraHeight;
    const uint16_t chainX = era ? 0 : afterEraX;
    const uint16_t chainY = era ? 0 : afterEraY;

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
    const uint32_t color = depthView || overdraw ? 0x000000ff : ( !background ? 0x050508ff : Moonlight::PackClearColor( toLinear( camera.ClearColor.x ), toLinear( camera.ClearColor.y ), toLinear( camera.ClearColor.z ) ) );
    bgfx::setViewClear( id, hasSkybox ? BGFX_CLEAR_NONE : BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, color, 1.0f, 0 );

    if( hasSkybox )
    {
        // The environment is infinitely distant. Give it a rotation-only view and its own clip
        // range, so neither metre conversion nor scene clipping/orthographic zoom cuts it off.
        glm::mat4 skyViewMatrix = glm::mat4( glm::mat3( camera.View.GetInternalMatrix() ) );
        float skyProjection[16];
        bx::mtxProj( skyProjection, camera.FOV, static_cast<float>( width ) / height, 0.01f, 10.f, bgfx::getCaps()->homogeneousDepth );
        bgfx::setViewTransform( skyView, &skyViewMatrix[0][0], skyProjection );
        bgfx::setViewFrameBuffer( skyView, camera.Buffer->SceneBuffer );
        bgfx::setViewRect( skyView, 0, 0, width, height );
        bgfx::setViewClear( skyView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, color, 1.0f, 0 );
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            //| BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_CULL_CW
            | BGFX_STATE_MSAA
            | s_ptState[m_pt]
            ;

        // Normalize the sky mesh to a unit cube, independent of its source/import units.
        const auto* skyMesh = camera.Skybox->SkyModel->GetAllMeshes()[0];
        const glm::vec3 halfExtents = glm::max( skyMesh->Bounds.GetExtents().InternalVector, glm::vec3( 1e-6f ) );
        glm::mat4 model = glm::scale( glm::mat4( 1.f ), glm::vec3( 1.f ) / halfExtents );
        model = glm::translate( model, -skyMesh->Bounds.GetCenter().InternalVector );

        bgfx::setTransform( &model[0] );

        // Set vertex and index buffer.
        bgfx::setVertexBuffer( 0, skyMesh->GetVertexBuffer() );
        bgfx::setIndexBuffer( skyMesh->GetIndexuffer() );

        // The sky shader always samples its panorama: a missing or failed one samples black (an
        // unbound sampler isn't safe on every backend).
        const Moonlight::Texture* diffuse = camera.Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse );
        bgfx::setTexture( 0, s_texDiffuse, diffuse && bgfx::isValid( diffuse->TexHandle ) ? Moonlight::Texture::Sampleable( diffuse->TexHandle ) : m_blackTexture );

        //mesh.MeshMaterial->Use();

        // Set render states.
        bgfx::setState( state );

        // Submit primitive for rendering to view 0.
        bgfx::submit( skyView, camera.Skybox->SkyMaterial->MeshShader.GetProgram() );
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
            // Resident foliage can retain a hidden command whose old visibility index
            // exceeds this frame's smaller draw list. Ignore it before reading that list.
            if( !mesh.Visible || !mesh.MeshMaterial || mesh.RenderGroup != camera.RenderGroup ) continue;
            if( hasCullingInfo && ( mesh.VisibilityIndex >= camera.VisibleFlags.size() || !camera.VisibleFlags[mesh.VisibilityIndex] ) )
            {
                continue;
            }

            {
                if( mesh.IsTransparent )
                {
                    if( options.Transparencies ) TransparentIndicies.push_back( i );
                    continue;
                }

                if( overdraw )
                {
                    RenderOverdrawMesh( id, mesh );
                    continue;
                }

                // A LOD crossfade: the outgoing level too, through the complementary mask.
                if( mesh.CrossfadeLod != 0xFF )
                {
                    Moonlight::MeshCommand outgoing = mesh;
                    outgoing.Lod = mesh.CrossfadeLod;
                    outgoing.DitherFade = -mesh.DitherFade;
                    if( mesh.SkinBoneCount > 0 )
                    {
                        RenderSkinnedMesh( id, outgoing, state );
                    }
                    else if( mesh.MeshMaterial->SupportsInstancing )
                    {
                        // Instancing-capable shaders read the transform from instance data only.
                        RenderMeshInstanced( id, outgoing, &outgoing.Transform, 1, state );
                    }
                    else
                    {
                        RenderSingleMesh( id, outgoing, state );
                    }
                }
                if( mesh.SkinBoneCount > 0 )
                {
                    RenderSkinnedMesh( id, mesh, state );
                    continue;
                }
                if( bgfx::isValid( mesh.PosedVertices ) && mesh.MeshMaterial->SupportsInstancing )
                {
                    RenderMeshInstanced( id, mesh, &mesh.Transform, 1, state );
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

    // The procedural sky goes behind the opaque scene: drawn last, it only shades the pixels the
    // scene left at the far plane.
    if( background && camera.ClearType == Moonlight::ClearColorType::Procedural && m_dynamicSky )
    {
        m_dynamicSky->Draw( id, camera );
    }

    // Per-object outlines: outlined meshes re-drawn as hulls after the sky so the ring also reads
    // against the background; no depth write, so the scene's depth hides occluded sections.
    if( sceneEffects && !overdraw )
    {
        OPTICK_CATEGORY( "Outline Hulls", Optick::Category::GPU_Scene );
        const bool hasCullingInfo = camera.ShouldCull && !camera.VisibleFlags.empty();
        for( size_t i = 0; i < m_meshCache.Commands.size(); ++i )
        {
            const Moonlight::MeshCommand& mesh = m_meshCache.Commands[i];
            if( !mesh.Outlined || mesh.OutlineWidth <= 0.f || !mesh.Visible || !mesh.MeshMaterial || mesh.RenderGroup != camera.RenderGroup ) continue;
            if( hasCullingInfo && ( mesh.VisibilityIndex >= camera.VisibleFlags.size() || !camera.VisibleFlags[mesh.VisibilityIndex] ) ) continue;
            RenderOutlineHull( id, mesh );
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

    // Decals land on the opaque surfaces; ambient occlusion then darkens both before transparents
    // are composited on top, and fog covers it all.
    // Decals, particles and debug drawing belong to the world.
    const bool world = camera.RenderGroup == 0;
    if( sceneEffects && options.Decals && world ) RenderDecals( camera );
    m_postProcess->RenderAmbientOcclusion( m_views, camera, *camera.Buffer );
    m_postProcess->RenderOutlineEdges( m_views, camera, *camera.Buffer );
    m_postProcess->RenderFog( m_views, camera, *camera.Buffer, [this]() { BindLighting(); } );
    // Everything drawn from here on (transparents, particles) is in front of the fogged scene.
    m_lighting.FogForward[0] = camera.Post.Fog && camera.Post.FogDensity > 0.f && camera.Post.FogMaxOpacity > 0.f ? 1.f : 0.f;

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
    if( EnableDebugDraw && options.DebugDraw )
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
                if( overdraw )
                {
                    RenderOverdrawMesh( transparentView, mesh );
                }
                else if( mesh.SkinBoneCount > 0 )
                {
                    RenderSkinnedMesh( transparentView, mesh, transparentState );
                }
                else if( mesh.SupportsInstancing || ( bgfx::isValid( mesh.PosedVertices ) && mesh.MeshMaterial->SupportsInstancing ) )
                {
                    RenderMeshInstanced( transparentView, mesh, &mesh.Transform, 1, transparentState );
                }
                else
                {
                    RenderSingleMesh( transparentView, mesh, transparentState );
                }
            }
    }

    if( EnableDebugDraw && options.DebugDraw )
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
    if( options.DebugDraw && world ) SubmitDebugLines( camera, transparentView );
    if( world ) RenderSprites( camera );
    if( sceneEffects && options.Particles && world ) RenderParticles( camera );

    // A render texture holds its top row first, like an image file. Backends whose framebuffers
    // start at the bottom (OpenGL) get the passes writing it flipped.
    m_postProcess->FlippedTarget = renderTexture && bgfx::getCaps()->originBottomLeft ? target : bgfx::FrameBufferHandle( BGFX_INVALID_HANDLE );

    // HDR -> final image (bloom, exposure, tonemapping, grading, FXAA), with any Post Process shader
    // graphs on the HDR scene (they replace the image bloom and tonemapping read) and on the display
    // image (the stock chain then renders into a ping-pong target, and the last graph writes target).
    m_postProcess->RenderTemporalAA( m_views, camera, *camera.Buffer );
    camera.Buffer->PostInput = RenderGraphPostPasses( *m_postProcess, m_views, camera, *camera.Buffer, "BeforeTonemap", camera.Buffer->PostInput, BGFX_INVALID_HANDLE, 0, 0 );
    // Eye adaptation follows the time since this camera last rendered (render textures can skip frames).
    m_postProcess->DeltaSeconds = std::max( camera.RenderDeltaSeconds > 0.f ? camera.RenderDeltaSeconds : m_time.x, 0.0001f );
    const bool displayGraphs = std::any_of( camera.Post.Graphs.begin(), camera.Post.Graphs.end(), []( const SharedPtr<Moonlight::ShaderGraphAsset>& InGraph ) {
        return InGraph && InGraph->GetManifest().value( "Injection", std::string() ) == "AfterTonemap" && bgfx::isValid( InGraph->GetProgramHandle( "Forward" ) );
    } );
    if( displayGraphs )
    {
        Moonlight::FrameBuffer& buffer = *camera.Buffer;
        buffer.EnsureGraphTargets( false, true );
        m_postProcess->Render( m_views, camera, buffer, buffer.GraphLdr[0].Buffer, buffer.GraphLdr[0].Width, buffer.GraphLdr[0].Height );
        RenderGraphPostPasses( *m_postProcess, m_views, camera, buffer, "AfterTonemap", buffer.GraphLdr[0].Color, chainTarget, chainWidth, chainHeight, chainX, chainY );
    }
    else
    {
        m_postProcess->Render( m_views, camera, *camera.Buffer, chainTarget, chainWidth, chainHeight, chainX, chainY );
    }
    if( era )
    {
        m_postProcess->RenderEra( m_views, camera, *camera.Buffer, afterEra, afterEraWidth, afterEraHeight, afterEraX, afterEraY );
    }
    if( scaled )
    {
        m_postProcess->RenderUpscale( m_views, camera, *camera.Buffer, pixelFactor, target, targetWidth, targetHeight, targetX, targetY );
    }
    // (Not when the view band ran out: then the final passes were skipped.)
    if( renderTexture && !m_views.IsExhausted() )
    {
        renderTexture->MarkRendered( m_frameIndex );
    }

    float orthoProj[16];
    bx::mtxOrtho( orthoProj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, bgfx::getCaps()->homogeneousDepth );
    {
        // clear out transform stack
        float identity[16];
        bx::mtxIdentity( identity );
        bgfx::setTransform( identity );
    }

    float m_texelHalf = 0.0f;
    if( EnableUIComposite && options.GameUI && camera.IsMain && bgfx::isValid( camera.RmlTexture ) )
    {
        // Runs after the UI band and all camera views, so the UI texture is from this frame.
        const bgfx::ViewId view = Moonlight::RenderView::UIComposite;
        bgfx::setViewName( view, "UI Composite" );
        bgfx::setViewMode( view, bgfx::ViewMode::Sequential );

        // The UI covers the whole display, even when the main camera only fills part of it.
        bgfx::setViewRect( view, 0, 0, split ? displayWidth : uint16_t( camera.OutputSize.x ), split ? displayHeight : uint16_t( camera.OutputSize.y ) );
        bgfx::setViewTransform( view, NULL, orthoProj );
        bgfx::setViewFrameBuffer( view, target );
        constexpr uint64_t kCompositeState = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_BLEND_FUNC_SEPARATE( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_INV_DST_ALPHA, BGFX_STATE_BLEND_ONE );
        bgfx::setState( kCompositeState );
        bgfx::setTexture( 0, s_texUI, camera.RmlTexture );
        Moonlight::screenSpaceQuad( camera.OutputSize.x, camera.OutputSize.y, m_texelHalf, bgfx::getCaps()->originBottomLeft );
        bgfx::submit( view, UIProgram );
    }
}


bool BGFXRenderer::RendersToTexture( const Moonlight::CameraData& InCamera )
{
    return !InCamera.IsMain && !InCamera.IsEditorView && InCamera.Target && InCamera.Target->IsRenderTarget() && bgfx::isValid( InCamera.Target->GetRenderTarget() );
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
    const bool shadowsOn = AreShadowsActive();
    if( shadowsOn )
    {
        EnsureShadowAtlas( m_sunShadowAtlas, CapShadowResolution( Shadows.CascadeResolution ), "Sun Shadow Atlas" );
        EnsureLocalShadowAtlas( CapShadowResolution( Shadows.SpotResolution ), CapShadowResolution( Shadows.PointResolution ) );
    }

    // Directionals are resolved per camera, since each camera can have a different sky.
    m_directionalLightCount = 0;
    m_localLights.clear();
    for( const Moonlight::LightCommand& light : m_lights )
    {
        if( light.Type == Moonlight::LightType::Directional ) ++m_directionalLightCount;
        else if( m_localLights.size() < 256 ) m_localLights.push_back( light );
    }
    m_lighting.Params[1] = static_cast<float>( m_localLights.size() );
    m_lighting.Params[2] = 1.f;

    // This frame's shadow casters, and their bounds (cascades extend towards the light to include them).
    GatherShadowCasters();

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

    // Light data: 8 RGBA32F texels per local light (256 lights fit the 2048-wide texture).
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
            const float values[32] = {
                light.Position.x, light.Position.y, light.Position.z, light.Range,
                light.Color.x, light.Color.y, light.Color.z, static_cast<float>( light.Type ),
                dir.x, dir.y, dir.z, light.CosOuter,
                light.CosInner, light.Type == Moonlight::LightType::Bar ? light.HalfSize.y : 0.f, light.GetNearFieldArea(), 0.f,
                shadowSlot, texelScale, light.ShadowBias, light.ShadowNormalBias,
                light.AxisX.x, light.AxisX.y, light.AxisX.z, light.HalfSize.x,
                light.AxisY.x, light.AxisY.y, light.AxisY.z, light.HalfSize.y,
                light.AxisZ.x, light.AxisZ.y, light.AxisZ.z, light.HalfSize.z };
            std::memcpy( &m_lightData[i * 32], values, sizeof( values ) );
        }
        const uint16_t width = static_cast<uint16_t>( m_localLights.size() * 8 );
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
    m_lighting.ViewShading[0] = static_cast<float>( camera.Shading );
    m_lighting.ViewShading[1] = std::max( camera.ViewOptions.DepthRange, 0.1f );
    m_lighting.Wireframe = camera.Shading == Moonlight::ViewShadingMode::Wireframe;
    m_lighting.BackfaceCulling = camera.ViewOptions.BackfaceCulling;
    // Set the sky (and render its tables and clouds) before lighting, shadows and environment
    // captures use it.
    if( m_dynamicSky )
    {
        m_dynamicSky->ApplySettings( camera.ProceduralSky, camera, camera.ProceduralSky.AnimationTime );
        if( camera.ClearType == Moonlight::ClearColorType::Procedural )
        {
            m_dynamicSky->Prepare( m_views, camera );
        }
    }
    // A procedural sky drives every directional (or creates sun and moon lights). Explicit Sun/Moon
    // sources share this path, including CSM, fog, decals and particles. Solid/skybox cameras
    // retain authored directionals and the legacy sky fallback when no light exists.
    // Other render groups light with their own lights (directional only), never the world's.
    const bool world = camera.RenderGroup == 0;
    const std::vector<Moonlight::LightCommand>& lights = world ? m_lights : camera.GroupLights;
    const int directionalCount = world ? m_directionalLightCount : static_cast<int>( std::count_if( lights.begin(), lights.end(), []( const Moonlight::LightCommand& InLight ) {
        return InLight.Type == Moonlight::LightType::Directional;
    } ) );
    const bool useSky = m_dynamicSky && ( camera.ClearType == Moonlight::ClearColorType::Procedural || directionalCount == 0 );
    const Moonlight::SkyDirectionalLights* skyLights = useSky ? &m_dynamicSky->GetDirectionalLights() : nullptr;
    const Moonlight::DirectionalLighting directionals = Moonlight::ResolveDirectionalLighting( lights, skyLights );
    m_lighting.EclipseParams[1] = 0.f;
    if( useSky ) m_dynamicSky->GetEclipse().Uniforms( m_lighting.EclipseSun, m_lighting.EclipseMoon, m_lighting.EclipseParams );
    m_hasSunShadow = false;
    const bool shadowsOn = AreShadowsActive();
    for( size_t i = 0; i < directionals.Count; ++i )
    {
        const Moonlight::LightCommand& light = directionals.Lights[i];
        const Vector3 dir = light.Direction.Normalized();
        const bool visible = std::max( { light.Color.x, light.Color.y, light.Color.z } ) > 1e-6f;
        const bool shadowed = i == 0 && light.CastShadows && shadowsOn && visible;
        float* direction = m_lighting.DirectionalDirection[i];
        float* color = m_lighting.DirectionalColor[i];
        direction[0] = dir.x; direction[1] = dir.y; direction[2] = dir.z; direction[3] = shadowed ? 1.f : 0.f;
        color[0] = light.Color.x; color[1] = light.Color.y; color[2] = light.Color.z; color[3] = light.SkySun ? 1.f : 0.f;
        if( shadowed ) { m_hasSunShadow = true; m_sunShadowLight = light; }
    }
    m_lighting.Params[0] = static_cast<float>( directionals.Count );

    Moonlight::FrameBuffer& buffer = *camera.Buffer;
    m_lighting.ClusterGridTexture = buffer.ClusterGrid;
    m_lighting.ClusterIndexTexture = buffer.ClusterIndices;
    Moonlight::FogUniforms( camera.Post, m_lighting.FogParams, m_lighting.FogColor, m_lighting.FogVolume );
    m_lighting.FogForward[0] = 0.f;

    const bool localLights = world && !m_localLights.empty();
    m_lighting.Params[1] = localLights ? static_cast<float>( m_localLights.size() ) : 0.f;
    if( localLights )
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
        const bx::Vec3& skyAmbient = m_dynamicSky->GetAmbientSky();
        sky = Vector3( skyAmbient.x, skyAmbient.y, skyAmbient.z );
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


void BGFXRenderer::GatherShadowCasters()
{
    OPTICK_EVENT( "Renderer::GatherShadowCasters" );
    for( size_t b = 0; b < m_activeShadowBatchCount; ++b )
    {
        m_shadowBatches[b].commands.clear();
    }
    m_activeShadowBatchCount = 0;
    m_shadowBatchLookup.clear();
    m_casterBounds.fill( AABB() );

    // Scan the (large) mesh commands in parallel chunks, keeping the casters compactly in order.
    constexpr uint32_t kChunk = 2048;
    const uint32_t commandCount = static_cast<uint32_t>( m_meshCache.Commands.size() );
    const uint32_t chunkCount = ( commandCount + kChunk - 1 ) / kChunk;
    if( m_shadowCasterChunks.size() < chunkCount )
    {
        m_shadowCasterChunks.resize( chunkCount );
    }
    m_shadowCasterChunkCount = chunkCount;
    m_shadowCasterChunkOffsets.resize( chunkCount );
    Jobs::JobSystem::Get().ParallelFor( chunkCount, 1, [this, commandCount]( uint32_t InBegin, uint32_t InEnd ) {
        for( uint32_t chunk = InBegin; chunk < InEnd; ++chunk )
        {
            std::vector<ShadowCaster>& casters = m_shadowCasterChunks[chunk];
            casters.clear();
            const uint32_t last = std::min( commandCount, ( chunk + 1 ) * kChunk );
            for( uint32_t i = chunk * kChunk; i < last; ++i )
            {
                const Moonlight::MeshCommand& mesh = m_meshCache.Commands[i];
                if( !mesh.MeshMaterial || !mesh.SingleMesh || !mesh.CastShadows || mesh.IsTransparent || mesh.VertexBufferIdx == UINT16_MAX || !mesh.WorldBounds.IsValid() )
                {
                    continue;
                }
                ShadowCaster caster;
                caster.Command = i;
                caster.Group = std::min<uint8_t>( mesh.RenderGroup, Moonlight::kMaxRenderGroups - 1 );
                // Opaque casters only differ by geometry; alpha-tested ones and materials with their
                // own caster program also by their material.
                caster.MaterialKey = ( mesh.AlphaCutoff > 0.f || mesh.CustomShadow ) ? mesh.BatchKey : 0u;
                // A fading mesh's caster dithers by its own amount (ME-279): its own batch.
                if( mesh.DitherFade != 0.f && mesh.CrossfadeLod == 0xFF )
                {
                    uint32_t fadeBits = 0;
                    std::memcpy( &fadeBits, &mesh.DitherFade, sizeof( fadeBits ) );
                    caster.MaterialKey = ( caster.MaterialKey ? caster.MaterialKey : mesh.BatchKey ) ^ ( ( uint64_t( fadeBits ) + 1 ) * 0xD6E8FEB86659FD93ull );
                }
                caster.VertexBuffer = mesh.VertexBufferIdx;
                caster.IndexBuffer = mesh.IndexBufferIdx;
                caster.Skinned = mesh.SkinBoneCount > 0 || bgfx::isValid( mesh.PosedVertices );
                caster.Bounds = mesh.WorldBounds;
                casters.push_back( caster );
            }
        }
    } );

    // Group into instance batches in place (serial, so batch order is stable).
    uint32_t previousBatch = UINT32_MAX;
    m_shadowCasterCount = 0;
    for( uint32_t chunk = 0; chunk < chunkCount; ++chunk )
    {
        m_shadowCasterChunkOffsets[chunk] = m_shadowCasterCount;
        m_shadowCasterCount += static_cast<uint32_t>( m_shadowCasterChunks[chunk].size() );
        for( ShadowCaster& caster : m_shadowCasterChunks[chunk] )
        {
            m_casterBounds[caster.Group].Encapsulate( caster.Bounds );
            if( !caster.Skinned )
            {
                auto matches = [&caster]( const InstanceBatch& InBatch ) {
                    return InBatch.vertexBuffer == caster.VertexBuffer && InBatch.indexBuffer == caster.IndexBuffer && InBatch.materialKey == caster.MaterialKey && InBatch.group == caster.Group;
                };
                const uint64_t lookup = caster.MaterialKey * 0x9E3779B97F4A7C15ull ^ ( static_cast<uint64_t>( caster.Group ) << 32 | static_cast<uint64_t>( caster.VertexBuffer ) << 16 | caster.IndexBuffer );
                // Runs of the same mesh are common (scattered copies of a prop): skip the lookup for them.
                if( previousBatch != UINT32_MAX && matches( m_shadowBatches[previousBatch] ) )
                {
                    caster.Batch = previousBatch;
                }
                else if( auto found = m_shadowBatchLookup.find( lookup ); found != m_shadowBatchLookup.end() && matches( m_shadowBatches[found->second] ) )
                {
                    caster.Batch = found->second;
                }
                else
                {
                    if( m_activeShadowBatchCount == m_shadowBatches.size() )
                    {
                        m_shadowBatches.emplace_back();
                    }
                    caster.Batch = static_cast<uint32_t>( m_activeShadowBatchCount++ );
                    InstanceBatch& batch = m_shadowBatches[caster.Batch];
                    batch.vertexBuffer = caster.VertexBuffer;
                    batch.indexBuffer = caster.IndexBuffer;
                    batch.materialKey = caster.MaterialKey;
                    batch.group = caster.Group;
                    batch.representativeIndex = caster.Command;
                    m_shadowBatchLookup.emplace( lookup, caster.Batch );
                }
                previousBatch = caster.Batch;
            }
        }
    }
}


void BGFXRenderer::CullShadowCasters( const Frustum* InFrustums, uint32_t InCount )
{
    OPTICK_EVENT( "Renderer::CullShadowCasters" );
    m_shadowCasterViews.assign( m_shadowCasterCount, 0u );
    const uint32_t count = std::min<uint32_t>( InCount, 32u );
    Jobs::JobSystem::Get().ParallelFor( m_shadowCasterChunkCount, 1, [this, InFrustums, count]( uint32_t InBegin, uint32_t InEnd ) {
        for( uint32_t chunk = InBegin; chunk < InEnd; ++chunk )
        {
            const std::vector<ShadowCaster>& casters = m_shadowCasterChunks[chunk];
            uint32_t* views = m_shadowCasterViews.data() + m_shadowCasterChunkOffsets[chunk];
            for( size_t i = 0; i < casters.size(); ++i )
            {
                uint32_t bits = 0;
                for( uint32_t v = 0; v < count; ++v )
                {
                    if( InFrustums[v].Intersects( casters[i].Bounds ) )
                    {
                        bits |= 1u << v;
                    }
                }
                views[i] = bits;
            }
        }
    } );
}


void BGFXRenderer::BindStockShadowCaster( const Moonlight::MeshCommand& InMesh, const float InAlpha[4] )
{
    bgfx::setUniform( u_shadowAlpha, InAlpha );
    // Holes are cut where the surface shows them: uv * Tiling + Offset.
    const float uvOffset[4] = { InMesh.MeshMaterial->Offset.x, InMesh.MeshMaterial->Offset.y, 0.f, 0.f };
    bgfx::setUniform( u_shadowUvOffset, uvOffset );
    if( InMesh.AlphaCutoff > 0.f )
    {
        const Moonlight::Texture* diffuse = InMesh.MeshMaterial->GetTexture( Moonlight::TextureType::Diffuse );
        const Moonlight::Texture* opacity = InMesh.MeshMaterial->GetTexture( Moonlight::TextureType::Opacity );
        // Only their alpha cuts holes: a camera's own target counts as no map (white, no holes).
        bgfx::setTexture( 0, s_texDiffuse, diffuse && bgfx::isValid( diffuse->TexHandle ) ? Moonlight::Texture::Sampleable( diffuse->TexHandle, m_whiteTexture ) : m_whiteTexture );
        bgfx::setTexture( 2, s_texAlpha, opacity && bgfx::isValid( opacity->TexHandle ) ? Moonlight::Texture::Sampleable( opacity->TexHandle, m_whiteTexture ) : m_whiteTexture );
    }
}


void BGFXRenderer::SubmitShadowCasters( bgfx::ViewId view, uint32_t InViewIndex, uint8_t InGroup )
{
    OPTICK_EVENT( "Renderer::SubmitShadowCasters" );
    for( size_t b = 0; b < m_activeShadowBatchCount; ++b )
    {
        m_shadowBatches[b].commands.clear();
    }
    m_skinnedShadowCasters.clear();
    const uint32_t bit = 1u << InViewIndex;
    const uint32_t* views = m_shadowCasterViews.data();
    for( uint32_t chunk = 0; chunk < m_shadowCasterChunkCount; ++chunk )
    {
        for( const ShadowCaster& caster : m_shadowCasterChunks[chunk] )
        {
            if( ( *views++ & bit ) && caster.Group == InGroup )
            {
                if( caster.Batch == UINT32_MAX )
                {
                    m_skinnedShadowCasters.push_back( caster.Command );   // each has its own pose: drawn one by one below
                }
                else
                {
                    m_shadowBatches[caster.Batch].commands.push_back( caster.Command );
                }
            }
        }
    }

    constexpr uint16_t kInstanceStride = sizeof( glm::mat4 );
    const uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    for( size_t b = 0; b < m_activeShadowBatchCount; ++b )
    {
        const InstanceBatch& batch = m_shadowBatches[b];
        if( batch.commands.empty() )
        {
            continue;
        }
        const Moonlight::MeshCommand& mesh = m_meshCache.Commands[batch.representativeIndex];
        // Fading meshes cast dithered shadows; a LOD crossfade casts the incoming level's, whole.
        const float alpha[4] = { mesh.AlphaCutoff, mesh.MeshMaterial->Tiling.x, mesh.MeshMaterial->Tiling.y, mesh.CrossfadeLod != 0xFF ? 0.f : mesh.DitherFade };
        const uint32_t count = static_cast<uint32_t>( batch.commands.size() );
        uint32_t offset = 0;
        while( offset < count )
        {
            const uint32_t available = std::min( bgfx::getAvailInstanceDataBuffer( count - offset, kInstanceStride ), FeatureCaps.Instancing ? count - offset : 1u );
            if( available == 0 )
            {
                break;
            }
            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer( &idb, available, kInstanceStride );
            // Straight from the mesh commands into the instance buffer.
            glm::mat4* instances = reinterpret_cast<glm::mat4*>( idb.data );
            for( uint32_t k = 0; k < available; ++k )
            {
                instances[k] = m_meshCache.Commands[batch.commands[offset + k]].Transform;
            }

            mesh.SingleMesh->BindVertexBuffer( mesh.PosedVertices );
            bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexBuffer( mesh.Lod ) );
            bgfx::setInstanceDataBuffer( &idb );
            const bgfx::ProgramHandle custom = mesh.CustomShadow ? mesh.MeshMaterial->GetShadowProgram( false ) : bgfx::ProgramHandle( BGFX_INVALID_HANDLE );
            if( bgfx::isValid( custom ) )
            {
                // Graph casters dither from the per-draw parameter (ME-283).
                const float drawParams[4] = { alpha[3], 0.f, 0.f, 0.f };
                bgfx::setUniform( u_drawParams, drawParams );
                mesh.MeshMaterial->UseShadowCaster();
            }
            else
            {
                BindStockShadowCaster( mesh, alpha );
            }
            bgfx::setState( state );
            bgfx::submit( view, bgfx::isValid( custom ) ? custom : m_shadowProgram.GetProgram() );
            CountTriangles( view, mesh, available );
            offset += available;
        }
    }

    const bgfx::ProgramHandle skinnedProgram = m_shadowSkinnedProgram.GetProgram();
    for( size_t index : m_skinnedShadowCasters )
    {
        const Moonlight::MeshCommand& mesh = m_meshCache.Commands[index];
        const bool skinned = mesh.SkinBoneCount > 0;
        const bgfx::ProgramHandle custom = mesh.CustomShadow ? mesh.MeshMaterial->GetShadowProgram( skinned ) : bgfx::ProgramHandle( BGFX_INVALID_HANDLE );
        const bgfx::ProgramHandle program = bgfx::isValid( custom ) ? custom : ( skinned ? skinnedProgram : m_shadowProgram.GetProgram() );
        if( !bgfx::isValid( program ) || ( skinned && !bgfx::isValid( mesh.SingleMesh->GetSkinBuffer() ) ) || bgfx::getAvailInstanceDataBuffer( 1, kInstanceStride ) == 0 )
        {
            continue;
        }
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer( &idb, 1, kInstanceStride );
        std::memcpy( idb.data, &mesh.Transform, kInstanceStride );
        // Fading meshes cast dithered shadows; a LOD crossfade casts the incoming level's, whole.
        const float alpha[4] = { mesh.AlphaCutoff, mesh.MeshMaterial->Tiling.x, mesh.MeshMaterial->Tiling.y, mesh.CrossfadeLod != 0xFF ? 0.f : mesh.DitherFade };
        mesh.SingleMesh->BindVertexBuffer( mesh.PosedVertices );
        if( skinned ) bgfx::setVertexBuffer( 1, mesh.SingleMesh->GetSkinBuffer() );
        bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexBuffer( mesh.Lod ) );
        bgfx::setInstanceDataBuffer( &idb );
        if( skinned ) bgfx::setUniform( u_bones, mesh.SkinPalette, mesh.SkinBoneCount );
        if( bgfx::isValid( custom ) )
        {
            const float drawParams[4] = { alpha[3], 0.f, 0.f, 0.f };
            bgfx::setUniform( u_drawParams, drawParams );
            mesh.MeshMaterial->UseShadowCaster();
        }
        else
        {
            BindStockShadowCaster( mesh, alpha );
        }
        bgfx::setState( state );
        bgfx::submit( view, program );
        CountTriangles( view, mesh );
    }
}


void BGFXRenderer::RenderLocalShadows()
{
    OPTICK_EVENT( "Renderer::RenderLocalShadows" );
    std::vector<Frustum> frustums( m_spotShadows.size() );
    for( size_t i = 0; i < m_spotShadows.size(); ++i )
    {
        frustums[i].Update( m_spotShadows[i].Projection * m_spotShadows[i].View );
    }
    if( !frustums.empty() )
    {
        CullShadowCasters( frustums.data(), static_cast<uint32_t>( frustums.size() ) );
    }
    for( size_t i = 0; i < m_spotShadows.size() && i < 32; ++i )
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
        SubmitShadowCasters( view, static_cast<uint32_t>( i ), 0 );
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
    input.CasterBounds = m_casterBounds[std::min<uint8_t>( camera.RenderGroup, Moonlight::kMaxRenderGroups - 1 )];
    Moonlight::CascadeSetup cascades[Moonlight::kMaxCascades];
    Moonlight::ComputeCascades( input, cascades );
    Frustum frustums[Moonlight::kMaxCascades];
    for( uint32_t i = 0; i < Moonlight::kMaxCascades; ++i )
    {
        frustums[i].Update( cascades[i].ViewProjection );
    }
    CullShadowCasters( frustums, Moonlight::kMaxCascades );

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
        SubmitShadowCasters( view, i, camera.RenderGroup );

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
        const float params2[4] = { batch.StretchFactor, batch.Blend == Moonlight::ParticleBlend::Additive ? 1.f : 0.f, batch.Lit ? 1.f : 0.f, 0.f };
        const uint64_t blend = batch.Blend == Moonlight::ParticleBlend::Additive
            ? BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE )
            : BGFX_STATE_BLEND_ALPHA;
        const bgfx::TextureHandle texture = bgfx::isValid( batch.Texture ) ? batch.Texture : m_particleTexture;
        // A particle shader graph replaces the fragment shader (it keeps Particle.vert).
        const bgfx::ProgramHandle graphProgram = batch.Graph ? batch.Graph->GetProgramHandle( "Forward" ) : bgfx::ProgramHandle( BGFX_INVALID_HANDLE );

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
            if( bgfx::isValid( graphProgram ) )
            {
                batch.Graph->Bind( batch.GraphBinding );
                bgfx::setTexture( 6, GraphSampler( "s_sgParticleTexture" ), Moonlight::Texture::Sampleable( texture ) );
                bgfx::setTexture( 7, GraphSampler( "s_sgSceneDepth" ), buffer.DepthTexture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
            }
            else
            {
                bgfx::setTexture( 0, s_texParticle, Moonlight::Texture::Sampleable( texture ) );
                bgfx::setTexture( 1, s_sceneDepth, buffer.DepthTexture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
            }
            bgfx::setUniform( u_particleParams, params );
            bgfx::setUniform( u_particleParams2, params2 );
            bgfx::setUniform( u_particleDepth, depthParams );
            // The particle shader includes the lighting code, so its samplers are always bound.
            BindLighting();
            bgfx::setState( BGFX_STATE_WRITE_RGB | blend );
            bgfx::submit( view, bgfx::isValid( graphProgram ) ? graphProgram : m_particleProgram.GetProgram() );
            offset += available;
        }
    }
}


void BGFXRenderer::RenderDecals( Moonlight::CameraData& camera )
{
    OPTICK_CATEGORY( "Decals", Optick::Category::GPU_Scene );
    if( m_decals.empty() || !m_decalProgram.IsLoaded() || !camera.Buffer || !bgfx::isValid( camera.Buffer->ParticleBuffer ) )
    {
        return;
    }
    // Visible decals in SortOrder (then gathering order): later ones draw over earlier ones.
    m_decalOrder.clear();
    for( const Moonlight::DecalCommand& decal : m_decals )
    {
        if( !camera.ShouldCull || camera.ViewFrustum.Intersects( decal.WorldBounds ) )
        {
            m_decalOrder.push_back( &decal );
        }
    }
    if( m_decalOrder.empty() )
    {
        return;
    }
    std::stable_sort( m_decalOrder.begin(), m_decalOrder.end(), []( const Moonlight::DecalCommand* InA, const Moonlight::DecalCommand* InB ) { return InA->SortOrder < InB->SortOrder; } );

    // The HDR colour without its depth attachment, so the scene depth can be read as a texture.
    Moonlight::FrameBuffer& buffer = *camera.Buffer;
    const bgfx::ViewId view = m_views.Allocate( camera.IsEditorView ? "Editor Decals" : "Decals" );
    if( view == UINT16_MAX )
    {
        return;
    }
    bgfx::setViewFrameBuffer( view, buffer.ParticleBuffer );
    bgfx::setViewRect( view, 0, 0, static_cast<uint16_t>( buffer.Width ), static_cast<uint16_t>( buffer.Height ) );
    bgfx::setViewTransform( view, &camera.View.GetInternalMatrix()[0][0], &camera.ProjectionMatrix.GetInternalMatrix()[0][0] );
    bgfx::setViewMode( view, bgfx::ViewMode::Sequential );
    bgfx::touch( view );

    const glm::mat4& projection = camera.ProjectionMatrix.GetInternalMatrix();
    const float depthParams[4] = { projection[2][2], projection[3][2], camera.Projection == Moonlight::ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
    const uint32_t pointClamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    // Clamped, so filtering at the box's edges never pulls in the texture's opposite side.
    const uint32_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    for( const Moonlight::DecalCommand* decal : m_decalOrder )
    {
        bgfx::setTransform( &decal->World[0][0] );
        bgfx::setVertexBuffer( 0, m_decalBoxVB );
        bgfx::setIndexBuffer( m_decalBoxIB );
        // A decal shader graph replaces the surface (Decal.vert and the projection stay).
        const bgfx::ProgramHandle graphProgram = decal->Graph ? decal->Graph->GetProgramHandle( "Forward" ) : bgfx::ProgramHandle( BGFX_INVALID_HANDLE );
        if( bgfx::isValid( graphProgram ) )
        {
            decal->Graph->Bind( decal->GraphBinding );
            bgfx::setTexture( 7, GraphSampler( "s_sgSceneDepth" ), buffer.DepthTexture, pointClamp );
        }
        else
        {
            bgfx::setTexture( 0, s_decalAlbedo, bgfx::isValid( decal->Albedo ) ? Moonlight::Texture::Sampleable( decal->Albedo ) : m_whiteTexture, clamp );
            bgfx::setTexture( 1, s_decalNormal, bgfx::isValid( decal->Normal ) ? Moonlight::Texture::Sampleable( decal->Normal, m_flatNormalTexture ) : m_flatNormalTexture, clamp );
            bgfx::setTexture( 2, s_sceneDepth, buffer.DepthTexture, pointClamp );
        }
        bgfx::setUniform( u_decalColor, decal->Color );
        bgfx::setUniform( u_decalEmissive, decal->Emissive );
        bgfx::setUniform( u_decalSurface, decal->Surface );
        bgfx::setUniform( u_decalDepth, depthParams );
        bgfx::setUniform( u_decalTexSize, decal->TextureSize );
        bgfx::setUniform( u_decalWorldToLocal, &decal->WorldToLocal[0][0] );
        bgfx::setUniform( u_decalAxes, &decal->Axes[0][0] );
        BindLighting();
        // Only the box's far faces (culling the clockwise near ones): each covered pixel shades once,
        // and the camera can stand inside the box.
        bgfx::setState( BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_CULL_CW );
        bgfx::submit( view, bgfx::isValid( graphProgram ) ? graphProgram : m_decalProgram.GetProgram() );
    }
}


void BGFXRenderer::BindLighting()
{
    // Uniform state is part of each draw in bgfx, so these go with every submit.
    bgfx::setUniform( u_lightParams, m_lighting.Params );
    bgfx::setUniform( u_viewShading, m_lighting.ViewShading );
    bgfx::setUniform( u_eraParams, m_lighting.EraParams );
    bgfx::setUniform( u_dirLightDirection, m_lighting.DirectionalDirection, 4 );
    bgfx::setUniform( u_dirLightColor, m_lighting.DirectionalColor, 4 );
    bgfx::setUniform( u_eclipseSun, m_lighting.EclipseSun );
    bgfx::setUniform( u_eclipseMoon, m_lighting.EclipseMoon );
    bgfx::setUniform( u_eclipseParams, m_lighting.EclipseParams );
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
    bgfx::setUniform( u_fogParams, m_lighting.FogParams );
    bgfx::setUniform( u_fogColor, m_lighting.FogColor );
    bgfx::setUniform( u_fogVolume, m_lighting.FogVolume );
    bgfx::setUniform( u_fogForward, m_lighting.FogForward );
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
    mesh.SingleMesh->BindVertexBuffer( mesh.PosedVertices );
    bgfx::setIndexBuffer( m_lighting.Wireframe ? mesh.SingleMesh->GetWireframeIndexBuffer( mesh.Lod ) : mesh.SingleMesh->GetIndexBuffer( mesh.Lod ) );

    // Every sampler gets a texture: a missing map falls back to a neutral 1x1 so the material's
    // colour shows through instead of whatever the backend leaves bound.
    // A render texture camera's own target never shows in its views (Texture::Sampleable): colour
    // slots get black, the rest their neutral fallback.
    auto textureOr = [this, &mesh]( Moonlight::TextureType type, bgfx::TextureHandle fallback ) {
        const Moonlight::Texture* texture = mesh.MeshMaterial->GetTexture( type );
        if( !texture || !bgfx::isValid( texture->TexHandle ) )
        {
            return fallback;
        }
        const bool colour = type == Moonlight::TextureType::Diffuse || type == Moonlight::TextureType::Emissive;
        return Moonlight::Texture::Sampleable( texture->TexHandle, colour ? m_blackTexture : fallback );
    };
    // PortalMaterial: the linked camera's HDR image in the diffuse slot, sampled in screen space by
    // the shader. A view cannot sample its own target, so there (and while that camera has no
    // target) the material's tint shows instead.
    const int screenViewCamera = mesh.MeshMaterial->ScreenViewCamera;
    if( screenViewCamera >= 0 )
    {
        const Moonlight::CameraData* source = m_cameraCache.Get( static_cast<unsigned int>( screenViewCamera ) );
        const Moonlight::FrameBuffer* buffer = source ? source->Buffer : nullptr;
        bgfx::TextureHandle view = BGFX_INVALID_HANDLE;
        if( buffer && buffer != m_drawingBuffer )
        {
            view = bgfx::isValid( buffer->PostInput ) ? buffer->PostInput : buffer->SceneColor;
        }
        const Vector3& tint = mesh.MeshMaterial->DiffuseColor;
        const float params[4] = { tint.x, tint.y, tint.z, bgfx::isValid( view ) ? 1.f : 0.f };
        bgfx::setTexture( 0, s_texDiffuse, bgfx::isValid( view ) ? view : m_blackTexture );
        bgfx::setUniform( u_screenView, params );
    }
    else
    {
        bgfx::setTexture( 0, s_texDiffuse, textureOr( Moonlight::TextureType::Diffuse, m_whiteTexture ) );
    }
    bgfx::setTexture( 1, s_texNormal, textureOr( Moonlight::TextureType::Normal, m_flatNormalTexture ) );
    bgfx::setTexture( 2, s_texAlpha, textureOr( Moonlight::TextureType::Opacity, m_whiteTexture ) );
    bgfx::setTexture( 3, s_texMetallicRoughness, textureOr( Moonlight::TextureType::MetallicRoughness, m_whiteTexture ) );
    bgfx::setTexture( 4, s_texEmissive, textureOr( Moonlight::TextureType::Emissive, m_whiteTexture ) );
    bgfx::setTexture( 5, s_texOcclusion, textureOr( Moonlight::TextureType::Occlusion, m_whiteTexture ) );
    bgfx::setTexture( 6, s_texMetallic, textureOr( Moonlight::TextureType::Metallic, m_whiteTexture ) );
    bgfx::setTexture( 7, s_texRoughness, textureOr( Moonlight::TextureType::Roughness, m_whiteTexture ) );
    BindLighting();
    const float drawParams[4] = { mesh.DitherFade, 0.f, 0.f, 0.f };
    bgfx::setUniform( u_drawParams, drawParams );

    mesh.MeshMaterial->Use();

    // Set render states.
    state = mesh.MeshMaterial->GetRenderState( state );
    if( !m_lighting.BackfaceCulling ) state &= ~BGFX_STATE_CULL_MASK;
    if( m_lighting.Wireframe )
    {
        state = ( state & ~( BGFX_STATE_PT_MASK | BGFX_STATE_CULL_MASK | BGFX_STATE_BLEND_MASK ) ) | BGFX_STATE_PT_LINES | BGFX_STATE_WRITE_Z;
    }
    bgfx::setState( state );

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
        CountTriangles( id, mesh );
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
    CountTriangles( id, mesh );
}


void BGFXRenderer::RenderOverdrawMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh )
{
    constexpr uint16_t kInstanceStride = sizeof( glm::mat4 );
    if( !mesh.SingleMesh || !bgfx::isValid( mesh.SingleMesh->GetVertexBuffer() ) || bgfx::getAvailInstanceDataBuffer( 1, kInstanceStride ) == 0 )
    {
        return;
    }
    const bool skinned = mesh.SkinBoneCount > 0 && bgfx::isValid( mesh.SingleMesh->GetSkinBuffer() ) && m_overdrawSkinnedProgram.IsLoaded();
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer( &idb, 1, kInstanceStride );
    std::memcpy( idb.data, &mesh.Transform, kInstanceStride );

    mesh.SingleMesh->BindVertexBuffer( mesh.PosedVertices );
    if( skinned )
    {
        bgfx::setVertexBuffer( 1, mesh.SingleMesh->GetSkinBuffer() );
        bgfx::setUniform( u_bones, mesh.SkinPalette, mesh.SkinBoneCount );
    }
    bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexBuffer( mesh.Lod ) );
    bgfx::setInstanceDataBuffer( &idb );
    const float alpha[4] = { mesh.AlphaCutoff, mesh.MeshMaterial->Tiling.x, mesh.MeshMaterial->Tiling.y, mesh.CrossfadeLod != 0xFF ? 0.f : mesh.DitherFade };
    BindStockShadowCaster( mesh, alpha );

    // Every layer counts, hidden or not: no depth test or write, additive colour.
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD | BGFX_STATE_MSAA;
    if( m_lighting.BackfaceCulling ) state |= mesh.MeshMaterial->GetRenderState( 0 ) & BGFX_STATE_CULL_MASK;
    bgfx::setState( state );
    bgfx::submit( id, skinned ? m_overdrawSkinnedProgram.GetProgram() : m_overdrawProgram.GetProgram() );
    CountTriangles( id, mesh );
}


void BGFXRenderer::RenderOutlineHull( bgfx::ViewId id, const Moonlight::MeshCommand& mesh )
{
    constexpr uint16_t kInstanceStride = sizeof( glm::mat4 );
    if( !mesh.SingleMesh || !bgfx::isValid( mesh.SingleMesh->GetVertexBuffer() ) || bgfx::getAvailInstanceDataBuffer( 1, kInstanceStride ) == 0 )
    {
        return;
    }
    const bool skinned = mesh.SkinBoneCount > 0 && bgfx::isValid( mesh.SingleMesh->GetSkinBuffer() ) && m_outlineHullSkinnedProgram.IsLoaded();
    if( skinned ? false : !m_outlineHullProgram.IsLoaded() )
    {
        return;
    }
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer( &idb, 1, kInstanceStride );
    std::memcpy( idb.data, &mesh.Transform, kInstanceStride );

    mesh.SingleMesh->BindVertexBuffer( mesh.PosedVertices );
    if( skinned )
    {
        bgfx::setVertexBuffer( 1, mesh.SingleMesh->GetSkinBuffer() );
        bgfx::setUniform( u_bones, mesh.SkinPalette, mesh.SkinBoneCount );
    }
    bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexBuffer( mesh.Lod ) );
    bgfx::setInstanceDataBuffer( &idb );
    const auto srgbToLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    const float hull[4] = { srgbToLinear( mesh.OutlineColor.x ), srgbToLinear( mesh.OutlineColor.y ), srgbToLinear( mesh.OutlineColor.z ), mesh.OutlineWidth };
    bgfx::setUniform( u_outlineHull, hull );
    // The opposite cull to the scene draw, no depth write: only the silhouette ring survives.
    bgfx::setState( BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA );
    bgfx::submit( id, skinned ? m_outlineHullSkinnedProgram.GetProgram() : m_outlineHullProgram.GetProgram() );
    CountTriangles( id, mesh );
}


void BGFXRenderer::CountTriangles( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint32_t instances )
{
    const uint64_t triangles = mesh.SingleMesh ? uint64_t( mesh.SingleMesh->GetIndexCount( mesh.Lod ) / 3 ) * instances : 0;
    if( id < m_viewTriangles.size() )
    {
        m_viewTriangles[id] += triangles;
    }
    if( m_counting )
    {
        ++m_counting->Draws;
        m_counting->Triangles += triangles;
    }
}


uint16_t BGFXRenderer::CapShadowResolution( uint16_t InResolution ) const
{
    return FeatureCaps.MaxShadowResolution > 0 ? std::min( InResolution, FeatureCaps.MaxShadowResolution ) : InResolution;
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
        // Without instancing every mesh is its own draw.
        const uint32_t available = std::min( bgfx::getAvailInstanceDataBuffer( remaining, kInstanceStride ), FeatureCaps.Instancing ? remaining : 1u );
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
        CountTriangles( id, representative, available );

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
        EditorCameraBuffer->Width = EditorCameraBuffer->OutputWidth = newSize.x;
        EditorCameraBuffer->Height = EditorCameraBuffer->OutputHeight = newSize.y;
        EditorCameraBuffer->ReCreate( m_resetFlags );
    }
#endif
    for( auto cam : m_cameraCache.Commands )
    {
        if( cam.Buffer && ( cam.IsMain || cam.Buffer->MatchMainBufferSize ) )
        {
            cam.Buffer->Width = cam.Buffer->OutputWidth = newSize.x;
            cam.Buffer->Height = cam.Buffer->OutputHeight = newSize.y;
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
    ++m_meshCacheGeneration;
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

BGFXRenderer::MSAALevel BGFXRenderer::GetMSAALevel() const
{
    return static_cast<MSAALevel>( ( m_resetFlags & BGFX_RESET_MSAA_MASK ) >> BGFX_RESET_MSAA_SHIFT );
}


void BGFXRenderer::SetMSAALevel( MSAALevel level )
{
    if( GetMSAALevel() == level )
    {
        return;
    }
    m_resetFlags &= ~BGFX_RESET_MSAA_MASK;

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


bool BGFXRenderer::IsVSyncEnabled() const
{
    return ( m_resetFlags & BGFX_RESET_VSYNC ) != 0;
}


void BGFXRenderer::SetVSyncEnabled( bool enabled )
{
    if( IsVSyncEnabled() == enabled )
    {
        return;
    }
    m_resetFlags = enabled ? m_resetFlags | BGFX_RESET_VSYNC : m_resetFlags & ~BGFX_RESET_VSYNC;
    NeedsReset = true;
}


void BGFXRenderer::RequestScreenshot( const std::string& filePath )
{
    bgfx::requestScreenShot( BGFX_INVALID_HANDLE, filePath.c_str() );
}


bool BGFXRenderer::IsRenderDocLoaded() const
{
#if USING( ME_ENABLE_RENDERDOC )
    return RenderDoc && RenderDoc->IsLoaded();
#else
    return false;
#endif
}


void BGFXRenderer::CaptureRenderDocFrame()
{
#if USING( ME_ENABLE_RENDERDOC )
    if( RenderDoc )
    {
        RenderDoc->Capture();
    }
#endif
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


void Moonlight::FillMeshCommand( MeshCommand& OutCommand, MeshData* InMesh, const SharedPtr<Material>& InMaterial, uint8_t InLod )
{
    OutCommand.SingleMesh = InMesh;
    OutCommand.MeshMaterial = InMaterial;
    if( InMaterial )
    {
        OutCommand.IsTransparent = InMaterial->IsTransparent();
        OutCommand.SupportsInstancing = InMaterial->SupportsInstancing;
        OutCommand.BatchKey = InMaterial->GetInstanceBatchKey();
        OutCommand.AlphaCutoff = InMaterial->GetAlphaCutoff();
        OutCommand.CustomShadow = bgfx::isValid( InMaterial->GetShadowProgram( false ) );
    }
    if( InMesh )
    {
        OutCommand.Lod = std::min<uint8_t>( InLod, InMesh->GetLodCount() );
        OutCommand.VertexBufferIdx = InMesh->GetVertexBuffer().idx;
        OutCommand.IndexBufferIdx = InMesh->GetIndexBuffer( OutCommand.Lod ).idx;
    }
}
