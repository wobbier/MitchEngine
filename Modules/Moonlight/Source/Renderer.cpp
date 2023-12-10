#include <Renderer.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <ImGui/ImGuiRenderer.h>
#include "optick.h"
#include "imgui.h"
#include "Utils/BGFXUtils.h"
#include "bx/timer.h"
#include "Graphics/Material.h"
#include "Graphics/ShaderStructures.h"
#include "Primitives/Cube.h"
#include "Graphics/MeshData.h"
#include "Graphics/ShaderCommand.h"
#include <algorithm>
#include "Resource/ResourceCache.h"
#include <Graphics/SkyBox.h>
#include <Math/Matrix4.h>
#include <Graphics/DynamicSky.h>
#include <Graphics/ModelResource.h>
#include <Debug/DebugDrawer.h>
#include <stack>
#include <Mathf.h>
#include "Core/Assert.h"

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
    BX_STATIC_ASSERT( BX_COUNTOF( s_ptState ) == BX_COUNTOF( s_ptNames ) );
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
    PreviousSize = settings.InitialSize;
    // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
    // Most graphics APIs must be used on the same thread that created the window.
    bgfx::renderFrame();
    // Initialize bgfx using the native window handle and window resolution.
    bgfx::Init init;
    init.platformData.nwh = settings.WindowPtr;
    init.resolution.width = static_cast<uint32_t>( PreviousSize.x );
    init.resolution.height = static_cast<uint32_t>( PreviousSize.y );
#if USING( ME_PLATFORM_MACOS )
    init.resolution.reset = BGFX_RESET_VSYNC;
#else
    init.resolution.reset =/* BGFX_RESET_VSYNC |*/ BGFX_RESET_MSAA_X16;
#endif

#if USING( ME_PLATFORM_UWP )
    // Something is up with using DX12 and my shaders, so this is the fix for now.
    init.type = bgfx::RendererType::Direct3D11;
#endif
    m_resetFlags = init.resolution.reset;
    CurrentSize = settings.InitialSize;

    if( !bgfx::init( init ) )
    {
        CLog::Log( CLog::LogType::Error, "BGFX Failed to Init." );

        return;
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
#if USING( ME_ENABLE_RENDERDOC )
        RenderDoc = new RenderDocManager();
#endif
        EditorCameraBuffer = new Moonlight::FrameBuffer( init.resolution.width, init.resolution.height );
        EditorCameraBuffer->ReCreate( m_resetFlags );

        m_debugDraw.reset( new DebugDrawer() );
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

        UIProgram = Moonlight::LoadProgram( "Assets/Shaders/UI.vert", "Assets/Shaders/UI.frag" );
        s_texDiffuse = bgfx::createUniform( "s_texDiffuse", bgfx::UniformType::Sampler );
        s_texNormal = bgfx::createUniform( "s_texNormal", bgfx::UniformType::Sampler );
        s_texAlpha = bgfx::createUniform( "s_texAlpha", bgfx::UniformType::Sampler );
        s_texUI = bgfx::createUniform( "s_texUI", bgfx::UniformType::Sampler );
        s_ambient = bgfx::createUniform( "s_ambient", bgfx::UniformType::Vec4 );
        s_sunDirection = bgfx::createUniform( "s_sunDirection", bgfx::UniformType::Vec4 );
        s_sunDiffuse = bgfx::createUniform( "s_sunDiffuse", bgfx::UniformType::Vec4 );

        m_timeOffset = bx::getHPCounter();

        m_dynamicSky = MakeShared<Moonlight::DynamicSky>( 32, 32 );

        m_dynamicSky->m_sun.Update( 0 );
        m_defaultOpacityTexture = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Textures/DefaultAlpha.png" ) );
    }
    TransparentIndicies.reserve( kMeshTransparencyTempSize );

#if USING( ME_IMGUI )
    ImGuiRender = new ImGuiRenderer();
    ImGuiRender->Create();
#endif

    bgfx::reset( (uint32_t)CurrentSize.x, (uint32_t)CurrentSize.y, m_resetFlags );
    bgfx::setViewRect( kClearView, 0, 0, bgfx::BackbufferRatio::Equal );
}

void BGFXRenderer::Destroy()
{
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

void BGFXRenderer::Render( Moonlight::CameraData& EditorCamera )
{
    OPTICK_EVENT( "Renderer::Render", Optick::Category::Rendering );

    // Use debug font to print information about this example.
    bgfx::dbgTextClear();
    // Enable stats or debug text.
    bgfx::setDebug( s_showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT );
    // Advance to next frame. Process submitted rendering primitives.

    if( CurrentSize != PreviousSize || NeedsReset )
    {
        PreviousSize = CurrentSize;

        bgfx::reset( (uint32_t)CurrentSize.x, (uint32_t)CurrentSize.y, m_resetFlags );
        bgfx::setViewRect( kClearView, 0, 0, bgfx::BackbufferRatio::Equal );
        if( NeedsReset )
        {
            for( auto& cam : m_cameraCache.Commands )
            {
                if( cam.Buffer )
                {
                    cam.Buffer->ReCreate( m_resetFlags );
                }
            }
        }
        NeedsReset = false;
    }

    if( m_dynamicSky )
    {
        bx::Vec3 sunLuminanceXYZ = m_dynamicSky->m_sunLuminanceXYZ.GetValue( m_dynamicSky->m_time );
        bx::Vec3 sunDiffuse = m_dynamicSky->xyzToRgb( sunLuminanceXYZ );

        sunDiffuse.x = sunDiffuse.x / 255.f;
        sunDiffuse.y = sunDiffuse.y / 255.f;
        sunDiffuse.z = sunDiffuse.z / 255.f;
        bgfx::setUniform( s_ambient, &m_ambient.x );
        bgfx::setUniform( s_sunDirection, &m_dynamicSky->m_sun.m_sunDir.x );
        bgfx::setUniform( s_sunDiffuse, &sunDiffuse.x );
    }

#if USING( ME_EDITOR )
    bgfx::ViewId id = 1;

    EditorCamera.Buffer = EditorCameraBuffer;
    RenderCameraView( EditorCamera, id );
    ++id;

    for( auto& camData : m_cameraCache.Commands )
    {
        if( !camData.IsMain )
        {
            RenderCameraView( camData, id );
            ++id;
        }
    }

    for( auto& camData : m_cameraCache.Commands )
    {
        if( camData.IsMain )
        {
            RenderCameraView( camData, id );
            break;
        }
    }
#else
    bgfx::ViewId id = kClearView;
    for( auto& camData : m_cameraCache.Commands )
    {
        if( !camData.IsMain )
        {
            ++id;
            RenderCameraView( camData, id );
        }
    }

    for( auto& camData : m_cameraCache.Commands )
    {
        if( camData.IsMain )
        {
            RenderCameraView( camData, kClearView );
            break;
        }
    }
#endif

    {
#if USING( ME_IMGUI )
        ImGuiRender->EndFrame();
#endif
        {
            OPTICK_EVENT( "Renderer::Frame", Optick::Category::Rendering );
            bgfx::frame();
        }
    }
}


void BGFXRenderer::SetGuizmoDrawCallback( std::function<void( DebugDrawer* )> GuizmoDrawingFunc )
{
    m_guizmoCallback = GuizmoDrawingFunc;
}

Matrix4 CalculateObliqueMatrix( const Matrix4& projection, const glm::vec4& clipPlane )
{
    Matrix4 returnMat = projection.GetInternalMatrix();
    {
        glm::vec4 vCamera = {
        ( Mathf::Sign( clipPlane.x ) + returnMat.GetInternalMatrix()[2][0] ) / returnMat.GetInternalMatrix()[0][0],
        ( Mathf::Sign( clipPlane.y ) + returnMat.GetInternalMatrix()[2][1] ) / returnMat.GetInternalMatrix()[1][1],
        1.f,
        ( 1.f + returnMat.GetInternalMatrix()[2][2] ) / returnMat.GetInternalMatrix()[3][2]
        };

        float m = 2.f / glm::dot( clipPlane, vCamera );
        returnMat.GetInternalMatrix()[0][2] = clipPlane.x * m;
        returnMat.GetInternalMatrix()[1][2] = clipPlane.y * m;
        returnMat.GetInternalMatrix()[2][2] = clipPlane.z * m;
        returnMat.GetInternalMatrix()[3][2] = clipPlane.w * m;
    }
    /*{
        glm::vec4 vCamera = {
        (Mathf::Sign(clipPlane.x) - returnMat.GetInternalMatrix()[0][2]) / returnMat.GetInternalMatrix()[0][0],
        (Mathf::Sign(clipPlane.y) - returnMat.GetInternalMatrix()[1][2]) / returnMat.GetInternalMatrix()[1][1],
        1.f,
        (1.f - returnMat.GetInternalMatrix()[2][2]) / returnMat.GetInternalMatrix()[2][3]
        };

        float m = 1.f / glm::dot(clipPlane, vCamera);
        returnMat.GetInternalMatrix()[2][0] = m * clipPlane.x;
        returnMat.GetInternalMatrix()[2][1] = m * clipPlane.y;
        returnMat.GetInternalMatrix()[2][2] = m * clipPlane.z;
        returnMat.GetInternalMatrix()[2][3] = m * clipPlane.w;
    }*/
    //{
    //	glm::vec4 q = projection.Inverse().GetInternalMatrix() * glm::vec4(Mathf::Sign(clipPlane.x), Mathf::Sign(clipPlane.y), 1.f, 1.f);
    //	glm::vec4 c = clipPlane * (2.f / glm::dot(clipPlane, q));
    //	returnMat.GetInternalMatrix()[2][0] = c.x - returnMat.GetInternalMatrix()[3][0];
    //	returnMat.GetInternalMatrix()[2][1] = c.y - returnMat.GetInternalMatrix()[3][1];
    //	returnMat.GetInternalMatrix()[2][2] = c.z - returnMat.GetInternalMatrix()[3][2];
    //	returnMat.GetInternalMatrix()[2][3] = c.w - returnMat.GetInternalMatrix()[3][3];
    //}
    return returnMat;
}

void BGFXRenderer::RenderCameraView( Moonlight::CameraData& camera, bgfx::ViewId id )
{
    OPTICK_CATEGORY( "Render Camera", Optick::Category::Camera )
        if( CurrentSize.IsZero() )
        {
            return;
        }
    if( !camera.Buffer )
    {
        return;
    }

    std::string viewName = "Game " + std::to_string( id );
    bgfx::setViewName( id, viewName.c_str() );
    //float time = (float)((bx::getHPCounter() - m_timeOffset) / double(bx::getHPFrequency()));

    const bx::Vec3 eye = { camera.Position.x, camera.Position.y, camera.Position.z };
    const bx::Vec3 at = { camera.Position.x + camera.Front.x, camera.Position.y + camera.Front.y, camera.Position.z + camera.Front.z };
    const bx::Vec3 up = { camera.Up.x, camera.Up.y, camera.Up.z };

    // Set view and projection matrix for view 0.
    Matrix4 view = camera.View;
    //bx::mtxLookAt(view, eye, at, up);

    Matrix4 proj;
    if( camera.Projection == Moonlight::ProjectionType::Perspective )
    {
        bx::mtxProj( &proj.GetInternalMatrix()[0][0], camera.FOV, float( camera.OutputSize.x ) / float( camera.OutputSize.y ), std::max( camera.Near, 0.01f ), camera.Far, bgfx::getCaps()->homogeneousDepth );
    }
    else
    {
        bx::mtxOrtho( &proj.GetInternalMatrix()[0][0], -( camera.OutputSize.x / camera.OrthographicSize ), ( camera.OutputSize.x / camera.OrthographicSize ), -( camera.OutputSize.y / camera.OrthographicSize ), ( camera.OutputSize.y / camera.OrthographicSize ), camera.Near, camera.Far, 0.f, bgfx::getCaps()->homogeneousDepth );
    }

    if( camera.IsOblique )
    {
        proj = CalculateObliqueMatrix( proj.GetInternalMatrix(), camera.ObliqueData );
    }

    bgfx::setViewTransform( id, &view.GetInternalMatrix()[0][0], &proj.GetInternalMatrix()[0][0] );
    if( id > 0 )
    {
        bgfx::setViewFrameBuffer( id, camera.Buffer->Buffer );
    }

    // Set view 0 default viewport.
    bgfx::setViewRect( id, 0, 0, uint16_t( camera.OutputSize.x ), uint16_t( camera.OutputSize.y ) );

    //bgfx::touch(0);
    uint32_t color = (uint32_t)( camera.ClearColor.x * 255.f ) << 24 | (uint32_t)( camera.ClearColor.y * 255.f ) << 16 | (uint32_t)( camera.ClearColor.z * 255.f ) << 8 | 255;
    bgfx::setViewClear( id
        , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        , color, 1.0f, 0
    );

    bgfx::touch( id );

    bgfx::setViewTransform( 0, &view.GetInternalMatrix()[0][0], &proj.GetInternalMatrix()[0][0] );
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

        //			XMStoreFloat4x4(&m_constantBufferSceneData.model, XMMatrixTranslation(eye[0], eye[1], eye[2]));
        //			XMStoreFloat4x4(&m_constantBufferSceneData.modelInv, XMMatrixInverse(nullptr, XMMatrixTranslation(eye[0], eye[1], eye[2])));
        // Set model matrix for rendering.
        glm::mat4 model = glm::mat4( 1.f );
        model = glm::translate( model, camera.Position.InternalVector );
        model = glm::scale( model, glm::vec3( 1000.f, 1000.f, 1000.f ) );

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
        | BGFX_STATE_CULL_CCW
        | BGFX_STATE_MSAA
        | s_ptState[m_pt]
        ;

    if( EnableDebugDraw )
    {
        m_debugDraw->Begin( id, true );
    }

    TransparentIndicies.clear();
    {
        OPTICK_CATEGORY( "Meshes", Optick::Category::GPU_Scene );

        for( size_t i = 0; i < m_meshCache.Commands.size(); ++i )
        {
            const Moonlight::MeshCommand& mesh = m_meshCache.Commands[i];
            if( !mesh.MeshMaterial )
            {
                continue;
            }

            if( mesh.MeshMaterial->IsTransparent() )
            {
                TransparentIndicies.push_back( i );
                continue;
            }

            //m_debugDraw->Push();
            //m_debugDraw->Draw(&mesh.Transform[0][0]);
            //m_debugDraw->Pop();
            RenderSingleMesh( id, mesh, state );
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

    uint64_t transparentState = 0
        | BGFX_STATE_WRITE_RGB
        //| BGFX_STATE_WRITE_Z
        | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_CULL_CCW
        | BGFX_STATE_MSAA
        | BGFX_STATE_BLEND_ALPHA
        | s_ptState[m_pt]
        ;

    {
        OPTICK_CATEGORY( "Transparent Meshes", Optick::Category::GPU_Scene )

        for( auto index : TransparentIndicies )
        {
            //m_debugDraw->Push();
            //m_debugDraw->Draw(&m_meshCache.Commands[index].Transform[0][0]);
            //m_debugDraw->Pop();
            RenderSingleMesh( id, m_meshCache.Commands[index], transparentState );
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

    float orthoProj[16];
    bx::mtxOrtho( orthoProj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, bgfx::getCaps()->homogeneousDepth );
    {
        // clear out transform stack
        float identity[16];
        bx::mtxIdentity( identity );
        bgfx::setTransform( identity );
    }

    // Get renderer capabilities info.
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    float m_texelHalf = ( bgfx::RendererType::Direct3D9 == renderer ) ? 0.5f : 0.0f;
    if( camera.IsMain && bgfx::isValid( camera.UITexture ) )
    {
        const int view = 69;
        bgfx::setViewName( view, "UI" );
        //bgfx::setViewClear(view
        //	, BGFX_CLEAR_NONE
        //	, 1
        //	, 1.0f
        //	, 0
        //);

        bgfx::setViewRect( view, 0, 0, uint16_t( camera.OutputSize.x ), uint16_t( camera.OutputSize.y ) );
        bgfx::setViewTransform( view, NULL, orthoProj );
        if( id > 0 )
        {
            bgfx::setViewFrameBuffer( view, camera.Buffer->Buffer );
        }
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

    //if (!camera.IsMain)
    {
        bgfx::blit( id, camera.Buffer->Texture, 0, 0, bgfx::getTexture( camera.Buffer->Buffer ) );
    }
}

void BGFXRenderer::RenderSingleMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state )
{
    //if (mesh.Type == Moonlight::Cube)
    //{
    //	if (mesh.MeshMaterial)
    //	{
    //		// Set model matrix for rendering.
    //		bgfx::setTransform(&mesh.Transform);

    //		// Set vertex and index buffer.
    //		bgfx::setVertexBuffer(0, m_vbh);
    //		bgfx::setIndexBuffer(m_ibh);

    //		mesh.MeshMaterial->Use();

    //		// Set render states.
    //		bgfx::setState(state);

    //		// Submit primitive for rendering to view 0.
    //		bgfx::submit(id, mesh.MeshMaterial->MeshShader.GetProgram());
    //	}
    //}
    //else 
    if( mesh.Type == Moonlight::MeshType::Model || mesh.Type == Moonlight::MeshType::Plane || mesh.Type == Moonlight::Cube )
    {
        if( !mesh.SingleMesh )
        {
            return;
        }

        // Set model matrix for rendering.
        bgfx::setTransform( &mesh.Transform );

        // Set vertex and index buffer.
        bgfx::setVertexBuffer( 0, mesh.SingleMesh->GetVertexBuffer() );
        bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexuffer() );

        if( const Moonlight::Texture* diffuse = mesh.MeshMaterial->GetTexture( Moonlight::TextureType::Diffuse ) )
        {
            if( bgfx::isValid( diffuse->TexHandle ) )
            {
                bgfx::setTexture( 0, s_texDiffuse, diffuse->TexHandle );
            }
        }

        if( const Moonlight::Texture* normal = mesh.MeshMaterial->GetTexture( Moonlight::TextureType::Normal ) )
        {
            if( bgfx::isValid( normal->TexHandle ) )
            {
                bgfx::setTexture( 1, s_texNormal, normal->TexHandle );
            }
        }
        else
        {
            bgfx::setTexture( 1, s_texNormal, m_defaultOpacityTexture->TexHandle );
        }

        if( const Moonlight::Texture* opacity = mesh.MeshMaterial->GetTexture( Moonlight::TextureType::Opacity ) )
        {
            if( bgfx::isValid( opacity->TexHandle ) )
            {
                bgfx::setTexture( 2, s_texAlpha, opacity->TexHandle );
            }
        }
        else
        {
            bgfx::setTexture( 2, s_texAlpha, m_defaultOpacityTexture->TexHandle );
        }

        mesh.MeshMaterial->Use();

        // Set render states.
        bgfx::setState( mesh.MeshMaterial->GetRenderState( state ) );

        // Submit primitive for rendering to view 0.
        bgfx::submit( id, mesh.MeshMaterial->MeshShader.GetProgram() );
    }
}

void BGFXRenderer::WindowResized( const Vector2& newSize )
{
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
    if( Id > m_meshCache.Commands.size() )
    {
        return;
    }

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
