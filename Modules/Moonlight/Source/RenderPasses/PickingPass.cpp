#include "PickingPass.h"
#include "Renderer.h"
#include "Camera/CameraData.h"
#include "Utils/BGFXUtils.h"
#include "Utils/ImGuiUtils.h"
#include "bx/timer.h"
#include "optick.h"
#include "RenderCommands.h"
#include "Graphics/MeshData.h"

using namespace Moonlight;

PickingPass::PickingPass()
{
        // Set up screen clears
    bgfx::setViewClear( RENDER_PASS_SHADING
        , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        , 0x303030ff
        , 1.0f
        , 0
    );

// ID buffer clears to black, which represents clicking on nothing (background)
    bgfx::setViewClear( RENDER_PASS_ID
        , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        , 0x000000ff
        , 1.0f
        , 0
    );

// Create uniforms
    u_tint = bgfx::createUniform( "u_tint", bgfx::UniformType::Vec4 ); // Tint for when you click on items
    u_id = bgfx::createUniform( "u_id", bgfx::UniformType::Vec4 ); // ID for drawing into ID buffer

    // Create program from shaders.
    m_shadingProgram = Moonlight::LoadProgram( "Assets/Shaders/Picking/picking_shaded.vert", "Assets/Shaders/Picking/picking_shaded.frag" ); // Blinn shading
    m_idProgram = Moonlight::LoadProgram( "Assets/Shaders/Picking/picking_shaded.vert", "Assets/Shaders/Picking/picking_id.frag" );     // Shader for drawing into ID buffer


    m_highlighted = UINT32_MAX;
    m_reading = 0;
    m_currFrame = UINT32_MAX;
    m_fov = 3.0f;

    
    //bx::RngMwc mwc;  // Random number generator
    for( uint32_t ii = 0; ii < 1000; ++ii )
    {
        //m_meshes[ii] = meshLoad( meshPaths[ii % BX_COUNTOF( meshPaths )] );
        //m_meshScale[ii] = meshScale[ii % BX_COUNTOF( meshPaths )];
        // For the sake of this example, we'll give each mesh a random color,  so the debug output looks colorful.
        // In an actual app, you'd probably just want to count starting from 1
        
        uint32_t rr = m_random( 0, 200000 ) % 256;
        uint32_t gg = m_random( 0, 200000 ) % 256;
        uint32_t bb = m_random( 0, 200000 ) % 256;
        m_idsF[ii][0] = rr / 255.0f;
        m_idsF[ii][1] = gg / 255.0f;
        m_idsF[ii][2] = bb / 255.0f;
        m_idsF[ii][3] = 1.0f;
        m_idsU[ii] = rr + ( gg << 8 ) + ( bb << 16 ) + ( 255u << 24 );
    }
    //m_timeOffset = bx::getHPCounter();

            // Set up ID buffer, which has a color target and depth buffer
    m_pickingRT = bgfx::createTexture2D( ID_DIM, ID_DIM, false, 1, bgfx::TextureFormat::RGBA8, 0
        | BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP
    );
    m_pickingRTDepth = bgfx::createTexture2D( ID_DIM, ID_DIM, false, 1, bgfx::TextureFormat::D32F, 0
        | BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP
    );

// CPU texture for blitting to and reading ID buffer so we can see what was clicked on.
// Impossible to read directly from a render target, you *must* blit to a CPU texture
// first. Algorithm Overview: Render on GPU -> Blit to CPU texture -> Read from CPU
// texture.
    m_blitTex = bgfx::createTexture2D( ID_DIM, ID_DIM, false, 1, bgfx::TextureFormat::RGBA8, 0
        | BGFX_TEXTURE_BLIT_DST
        | BGFX_TEXTURE_READ_BACK
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP
    );

    bgfx::TextureHandle rt[2] =
    {
        m_pickingRT,
        m_pickingRTDepth
    };
    m_pickingFB = bgfx::createFrameBuffer( BX_COUNTOF( rt ), rt, true );
}


void PickingPass::Render( BGFXRenderer* inRenderer, CameraData* inCamData, FrameRenderData& inFrameSettings )
{
    m_width = inCamData->OutputSize.x;
    m_height = inCamData->OutputSize.y;
    if( m_width <= 0 || m_height <= 0 )
    {
        return;
    }

    CameraData& camera = *inCamData;
    const bgfx::Caps* caps = bgfx::getCaps();
//ImGui::SetNextWindowPos(
    //    ImVec2( m_width - m_width / 5.0f - 10.0f, 10.0f )
    //    , ImGuiCond_FirstUseEver
    //);
    //ImGui::SetNextWindowSize(
    //    ImVec2( m_width / 5.0f, m_height / 2.0f )
    //    , ImGuiCond_FirstUseEver
    //);
    RenderDebugMenu();

    bgfx::setViewFrameBuffer( RENDER_PASS_ID, m_pickingFB );

    std::string viewName = "Picking Pass " + std::to_string( RENDER_PASS_ID );
    bgfx::setViewName( RENDER_PASS_ID, viewName.c_str() );

    // Set view and projection matrix for view 0.
    Matrix4 view = camera.View;
    //bx::mtxLookAt(view, eye, at, up);

    // #TODO: remove this custom code and rely on camera data
    Matrix4 proj;
    if( camera.Projection == Moonlight::ProjectionType::Perspective )
    {
        bx::mtxProj( &proj.GetInternalMatrix()[0][0], camera.FOV, float( camera.OutputSize.x ) / float( camera.OutputSize.y ), std::max( camera.Near, 0.01f ), camera.Far, bgfx::getCaps()->homogeneousDepth );
    }
    else
    {
        bx::mtxOrtho( &proj.GetInternalMatrix()[0][0], -( camera.OutputSize.x / camera.OrthographicSize ), ( camera.OutputSize.x / camera.OrthographicSize ), -( camera.OutputSize.y / camera.OrthographicSize ), ( camera.OutputSize.y / camera.OrthographicSize ), camera.Near, camera.Far, 0.f, bgfx::getCaps()->homogeneousDepth );
    }

    //bgfx::setViewTransform( RENDER_PASS_ID, &camera.View.GetInternalMatrix()[0][0], &proj.GetInternalMatrix()[0][0] );

    // Set view 0 default viewport.
    bgfx::setViewRect( RENDER_PASS_ID, 0, 0, ID_DIM, ID_DIM );

    //bgfx::touch(0);
    uint32_t color = (uint32_t)( camera.ClearColor.x * 255.f ) << 24 | (uint32_t)( camera.ClearColor.y * 255.f ) << 16 | (uint32_t)( camera.ClearColor.z * 255.f ) << 8 | 255;
    bgfx::setViewClear( RENDER_PASS_ID
        , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
        , color, 1.0f, 0
    );

    bgfx::touch( RENDER_PASS_ID );


    const float camSpeed = 0.25;
    float eyeDist = 2.5f;
    float time = (float)( ( bx::getHPCounter() - m_timeOffset ) / double( bx::getHPFrequency() ) );

                // Set up picking pass
    float viewProj[16];
    bx::mtxMul( viewProj, &camera.View.GetInternalMatrix()[0][0], &proj.GetInternalMatrix()[0][0] );

    float invViewProj[16];
    bx::mtxInverse( invViewProj, viewProj );

    // Mouse coord in NDC
    float mouseXNDC = ( inFrameSettings.MousePosition.x / (float)m_width ) * 2.0f - 1.0f;
    float mouseYNDC = ( ( m_height - inFrameSettings.MousePosition.y ) / (float)m_height ) * 2.0f - 1.0f;

    const bx::Vec3 pickEye = bx::mulH( { mouseXNDC, mouseYNDC, 0.0f }, invViewProj );
    const bx::Vec3 pickAt = bx::mulH( { mouseXNDC, mouseYNDC, 1.0f }, invViewProj );

    // Look at our unprojected point
    float pickView[16];
    bx::mtxLookAt( pickView, pickEye, pickAt );

    // Tight FOV is best for picking
    float pickProj[16];
    bx::mtxProj( pickProj, m_fov, 1, 0.1f, 100.0f, caps->homogeneousDepth );

    // View rect and transforms for picking pass
    //bgfx::setViewRect( RENDER_PASS_ID, 0, 0, ID_DIM, ID_DIM );
    bgfx::setViewTransform( RENDER_PASS_ID, pickView, pickProj );

    uint64_t state = 0
        | BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_Z
        | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_CULL_CCW
        | BGFX_STATE_MSAA
        //| s_ptState[m_pt]
        ;

    if( ForceDraw || ( m_reading == 0 && inFrameSettings.WasLeftPressed ) )
    {
        OPTICK_CATEGORY( "Picking", Optick::Category::GPU_Scene );

        for( size_t i = 0; i < inRenderer->GetMeshCache().Commands.size(); ++i )
        {
            const Moonlight::MeshCommand& mesh = inRenderer->GetMeshCache().Commands[i];
            if( inCamData->ShouldCull && !inCamData->VisibleFlags[mesh.VisibilityIndex] )
            {
                continue;
            }

            {
                if( !mesh.SingleMesh || !bgfx::isValid( mesh.SingleMesh->GetVertexBuffer() ) )
                {
                    continue;
                }

                uint32_t rr = ( mesh.ID & 0xFF );        // Red channel: least significant byte
                uint32_t gg = ( mesh.ID >> 8 ) & 0xFF;  // Green channel: next byte
                uint32_t bb = ( mesh.ID >> 16 ) & 0xFF; // Blue channel: next byte
                Vector4 colorVec;
                colorVec.x = rr / 255.0f;
                colorVec.y = gg / 255.0f;
                colorVec.z = bb / 255.0f;
                colorVec.w = 1.0f;
                m_idsToEnt[rr + ( gg << 8 ) + ( bb << 16 ) + ( 255u << 24 )] = mesh.ID;

                // Set model matrix for rendering.
                bgfx::setTransform( &mesh.Transform );

                // Set vertex and index buffer.
                bgfx::setVertexBuffer( 0, mesh.SingleMesh->GetVertexBuffer() );
                bgfx::setIndexBuffer( mesh.SingleMesh->GetIndexuffer() );

                // Set render states.
                bgfx::setState( state );

                    // Submit ID pass based on mesh ID
                bgfx::setUniform( u_id, &colorVec.x );
                // Submit primitive for rendering to view 0.
                bgfx::submit( RENDER_PASS_ID, m_idProgram );
            }
        }
    }
    // If the user previously clicked, and we're done reading data from GPU, look at ID buffer on CPU
                // Whatever mesh has the most pixels in the ID buffer is the one the user clicked on.
    if( m_reading == inFrameSettings.m_currentFrame )
    {
        m_reading = 0;
        std::map<uint32_t, uint32_t> ids;  // This contains all the IDs found in the buffer
        uint32_t maxAmount = 0;
        for( uint8_t* x = m_blitData; x < m_blitData + ID_DIM * ID_DIM * 4;)
        {
            uint8_t rr = *x++;
            uint8_t gg = *x++;
            uint8_t bb = *x++;
            uint8_t aa = *x++;

            if( 0 == ( rr | gg | bb ) ) // Skip background
            {
                continue;
            }

            uint32_t hashKey = rr + ( gg << 8 ) + ( bb << 16 ) + ( aa << 24 );
            std::map<uint32_t, uint32_t>::iterator mapIter = ids.find( hashKey );
            uint32_t amount = 1;
            if( mapIter != ids.end() )
            {
                amount = mapIter->second + 1;
            }

            ids[hashKey] = amount; // Amount of times this ID (color) has been clicked on in buffer
            maxAmount = maxAmount > amount
                ? maxAmount
                : amount
                ;
        }

        uint32_t idKey = 0;
        m_highlighted = UINT32_MAX;
        if( maxAmount )
        {
            for( std::map<uint32_t, uint32_t>::iterator mapIter = ids.begin(); mapIter != ids.end(); mapIter++ )
            {
                if( mapIter->second == maxAmount )
                {
                    idKey = mapIter->first;
                    break;
                }
            }

            for( uint32_t ii = 0; ii < 12; ++ii )
            {
                if( m_idsU[ii] == idKey )
                {
                    m_highlighted = ii;
                    break;
                }
            }

            inFrameSettings.RequestedEntityID = m_idsToEnt[idKey];
        }
    }

    // Start a new readback?
    if( m_reading == 0 && inFrameSettings.WasLeftPressed)
    {
        // Blit and read
        bgfx::blit( RENDER_PASS_BLIT, m_blitTex, 0, 0, m_pickingRT );
        m_reading = bgfx::readTexture( m_blitTex, m_blitData );
    }
}


void PickingPass::RenderDebugMenu()
{
    ImVec2 pickingSize = ImVec2( m_width / 5.0f - 16.0f, m_width / 5.0f - 16.0f );
    ImGui::Image( m_pickingRT, pickingSize );
    ImGui::SliderFloat( "Field of view", &m_fov, 1.0f, 60.0f );
    if( bgfx::isValid( m_blitTex ) )
    {
        //ImGui::Image( m_blitTex, pickingSize );
    }

    ImGui::Checkbox( "Force Draw", &ForceDraw );
}

bool PickingPass::IsSupported()
{
    return false;
}
