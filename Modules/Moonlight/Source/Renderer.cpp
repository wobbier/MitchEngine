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
        BRUH("renderFrame");
    bgfx::renderFrame();
        BRUH("renderFrame");
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
        BRUH("Renderer assets");
        OPTICK_EVENT( "BGFXRenderer::LoadShaders" );
        UIProgram = Moonlight::LoadProgram( "Assets/Shaders/UI.vert", "Assets/Shaders/UI.frag" );
        s_texDiffuse = bgfx::createUniform( "s_texDiffuse", bgfx::UniformType::Sampler );
        s_texNormal = bgfx::createUniform( "s_texNormal", bgfx::UniformType::Sampler );
        s_texAlpha = bgfx::createUniform( "s_texAlpha", bgfx::UniformType::Sampler );
        s_texUI = bgfx::createUniform( "s_texUI", bgfx::UniformType::Sampler );
        // Per-frame uniforms apply to every draw in the frame regardless of view/sort order.
        s_ambient = bgfx::createUniform( "s_ambient", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4 );
        s_sunDirection = bgfx::createUniform( "s_sunDirection", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4 );
        s_sunDiffuse = bgfx::createUniform( "s_sunDiffuse", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4 );

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
        m_lightDataTexture = bgfx::createTexture2D( 1024, 1, false, 1, bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        bgfx::setName( m_lightDataTexture, "Light Data" );
        m_lightData.assign( 1024 * 4, 0.f );
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

    const bgfx::UniformHandle uniforms[] = { s_texDiffuse, s_texNormal, s_texAlpha, s_texUI, s_ambient, s_sunDirection, s_sunDiffuse, s_time,
        u_lightParams, u_dirLightDirection, u_dirLightColor, u_clusterParams, u_clusterGrid, u_ambientSky, u_ambientGround,
        s_lightData, s_clusterGrid, s_clusterIndices, s_texMetallicRoughness, s_texEmissive, s_texOcclusion };
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

    if( m_dynamicSky )
    {
        bx::Vec3 sunLuminanceXYZ = m_dynamicSky->m_sunLuminanceXYZ.GetValue( m_dynamicSky->m_time );
        bx::Vec3 sunDiffuse = m_dynamicSky->xyzToRgb( sunLuminanceXYZ );

        // Vec4 uniforms read 16 bytes; widen the bx::Vec3s so we don't read past them.
        const bx::Vec3& sunDir = m_dynamicSky->m_sun.m_sunDir;
        const Vector4 sunDirection( sunDir.x, sunDir.y, sunDir.z, 0.f );
        const Vector4 sunDiffuse4( sunDiffuse.x / 255.f, sunDiffuse.y / 255.f, sunDiffuse.z / 255.f, 1.f );
        bgfx::setFrameUniform( s_ambient, &m_ambient.x );
        bgfx::setFrameUniform( s_sunDirection, &sunDirection.x );
        bgfx::setFrameUniform( s_sunDiffuse, &sunDiffuse4.x );
    }
    bgfx::setFrameUniform( s_time, &m_time.x );

    m_views.Reset();
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
                if( mesh.SupportsInstancing )
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
    m_localLights.clear();
    int directionalCount = 0;
    for( const Moonlight::LightCommand& light : m_lights )
    {
        if( light.Type == Moonlight::LightType::Directional )
        {
            if( directionalCount < 4 )
            {
                float* direction = m_lighting.DirectionalDirection[directionalCount];
                float* color = m_lighting.DirectionalColor[directionalCount];
                const Vector3 dir = light.Direction.Normalized();
                direction[0] = dir.x; direction[1] = dir.y; direction[2] = dir.z; direction[3] = light.CastShadows ? 1.f : 0.f;
                color[0] = light.Color.x; color[1] = light.Color.y; color[2] = light.Color.z; color[3] = 0.f;
                ++directionalCount;
            }
        }
        else if( m_localLights.size() < 256 )
        {
            m_localLights.push_back( light );
        }
    }

    // Scenes without a directional light are lit by the procedural sky's sun.
    if( directionalCount == 0 && m_dynamicSky )
    {
        const bx::Vec3& sunDir = m_dynamicSky->m_sun.m_sunDir;
        bx::Vec3 sunRgb = m_dynamicSky->xyzToRgb( m_dynamicSky->m_sunLuminanceXYZ.GetValue( m_dynamicSky->m_time ) );
        const float peak = std::max( { sunRgb.x, sunRgb.y, sunRgb.z, 1e-4f } );
        const float elevation = std::clamp( sunDir.y * 4.f, 0.f, 1.f );
        const float intensity = 3.f * elevation;
        float* direction = m_lighting.DirectionalDirection[0];
        float* color = m_lighting.DirectionalColor[0];
        direction[0] = -sunDir.x; direction[1] = -sunDir.y; direction[2] = -sunDir.z; direction[3] = 1.f;
        color[0] = sunRgb.x / peak * intensity; color[1] = sunRgb.y / peak * intensity; color[2] = sunRgb.z / peak * intensity;
        directionalCount = 1;
    }

    m_lighting.Params[0] = static_cast<float>( directionalCount );
    m_lighting.Params[1] = static_cast<float>( m_localLights.size() );
    m_lighting.Params[2] = 1.f;

    // Light data: 4 RGBA32F texels per point/spot light.
    if( !m_localLights.empty() && bgfx::isValid( m_lightDataTexture ) )
    {
        for( size_t i = 0; i < m_localLights.size(); ++i )
        {
            const Moonlight::LightCommand& light = m_localLights[i];
            float* texel = &m_lightData[i * 16];
            const Vector3 dir = light.Direction.Normalized();
            const float values[16] = {
                light.Position.x, light.Position.y, light.Position.z, light.Range,
                light.Color.x, light.Color.y, light.Color.z, light.Type == Moonlight::LightType::Spot ? 2.f : 1.f,
                dir.x, dir.y, dir.z, light.CosOuter,
                light.CosInner, 0.f, 0.f, 0.f };
            std::memcpy( texel, values, sizeof( values ) );
        }
        const uint16_t width = static_cast<uint16_t>( m_localLights.size() * 4 );
        bgfx::updateTexture2D( m_lightDataTexture, 0, 0, 0, 0, width, 1, bgfx::copy( m_lightData.data(), width * 4 * sizeof( float ) ) );
    }
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
