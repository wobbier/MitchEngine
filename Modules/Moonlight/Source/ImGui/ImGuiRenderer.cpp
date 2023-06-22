#include <ImGui/ImGuiRenderer.h>

#include <bgfx/embedded_shader.h>
#include <bx/math.h>
#include <imgui.h>
#include <ImGui/Resources/vs_ocornut_imgui.bin.h>
#include <ImGui/Resources/fs_ocornut_imgui.bin.h>
#include <ImGui/Resources/vs_imgui_image.bin.h>
#include <ImGui/Resources/fs_imgui_image.bin.h>
#include <ImGui/Resources/roboto_regular.ttf.h>
#include <ImGui/Resources/robotomono_regular.ttf.h>
#include <ImGui/Resources/icons_kenney.ttf.h>
#include <ImGui/Resources/icons_font_awesome.ttf.h>
#include "optick.h"
#include <Utils/BGFXUtils.h>
#include <Utils/ImGuiUtils.h>

#define IMGUI_MBUT_LEFT   0x01
#define IMGUI_MBUT_RIGHT  0x02
#define IMGUI_MBUT_MIDDLE 0x04

static const bgfx::EmbeddedShader s_embeddedShaders[] =
{
    BGFX_EMBEDDED_SHADER( vs_ocornut_imgui ),
    BGFX_EMBEDDED_SHADER( fs_ocornut_imgui ),
    BGFX_EMBEDDED_SHADER( vs_imgui_image ),
    BGFX_EMBEDDED_SHADER( fs_imgui_image ),

    BGFX_EMBEDDED_SHADER_END()
};

void ImGuiRenderer::Create()
{
    ViewId = 255;

    IMGUI_CHECKVERSION();
    Context = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2( 1280, 720 );
    io.DeltaTime = 1.f / 140.f;
    io.IniFilename = nullptr;
    //io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    auto renderType = bgfx::getRendererType();

    Program = bgfx::createProgram(
        bgfx::createEmbeddedShader( s_embeddedShaders, renderType, "vs_ocornut_imgui" )
        , bgfx::createEmbeddedShader( s_embeddedShaders, renderType, "fs_ocornut_imgui" )
        , true );

    ImageLODEnabled = bgfx::createUniform( "u_imageLodEnabled", bgfx::UniformType::Vec4 );
    ImageProgram = bgfx::createProgram(
        bgfx::createEmbeddedShader( s_embeddedShaders, renderType, "vs_imgui_image" )
        , bgfx::createEmbeddedShader( s_embeddedShaders, renderType, "fs_imgui_image" )
        , true );

    Layout.begin()
        .add( bgfx::Attrib::Position, 2, bgfx::AttribType::Float )
        .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
        .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
        .end();

    sTexture = bgfx::createUniform( "s_tex", bgfx::UniformType::Sampler );

    uint8_t* data;
    int32_t width;
    int32_t height;
    io.Fonts->GetTexDataAsRGBA32( &data, &width, &height );

    mTexture = bgfx::createTexture2D(
        (uint16_t)width
        , (uint16_t)height
        , false
        , 1
        , bgfx::TextureFormat::BGRA8
        , 0
        , bgfx::copy( data, width * height * 4 )
    );
}

void ImGuiRenderer::NewFrame( const Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId )
{
    ViewId = viewId;

    ImGuiIO& io = ImGui::GetIO();
    if ( inputChar >= 0 )
    {
        io.AddInputCharacter( inputChar );
    }
    io.DisplaySize = ImVec2( outputSize.x, outputSize.y );

    io.MousePos = ImVec2( mousePosition.x, mousePosition.y );
    io.MouseDown[0] = ( mouseButton & IMGUI_MBUT_LEFT ) != 0;
    io.MouseDown[1] = ( mouseButton & IMGUI_MBUT_RIGHT ) != 0;
    io.MouseDown[2] = ( mouseButton & IMGUI_MBUT_MIDDLE ) != 0;

    ImGui::NewFrame();
}

void ImGuiRenderer::EndFrame()
{
    ImGui::Render();

    if ( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

        // I need to bind this to the window with a framebuffer
        Render( ImGui::GetDrawData(), ViewId );

        ImGui::UpdatePlatformWindows();
        for ( int i = 1; i < platform_io.Viewports.Size; i++ )
        {
            // Weird issue when creating / destroying windows, need to assign proper ids
            ImGuiViewport* viewport = platform_io.Viewports[i];

            // TODO: Find out why this was removed??
            //if ( viewport->Flags & ImGuiViewportFlags_Minimized )
            //    continue;
            uint16_t platformIndex = static_cast<uint16_t>( 254 - platform_io.Viewports.Size ) + i;
            if ( platform_io.Platform_RenderWindow ) platform_io.Platform_RenderWindow( viewport, &platformIndex );
        }
    }
    else
    {
        Render( ImGui::GetDrawData(), ViewId );
    }
}

void ImGuiRenderer::Render( ImDrawData* drawData, bgfx::ViewId viewId )
{
    OPTICK_EVENT( "ImGuiRenderer::Render", Optick::Category::Rendering );
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)( drawData->DisplaySize.x * drawData->FramebufferScale.x );
    int fb_height = (int)( drawData->DisplaySize.y * drawData->FramebufferScale.y );
    if ( fb_width <= 0 || fb_height <= 0 ) return;

    bgfx::setViewName( viewId, "ImGui" );
    bgfx::setViewMode( viewId, bgfx::ViewMode::Sequential );

    const bgfx::Caps* caps = bgfx::getCaps();
    {
        float ortho[16];
        float x = drawData->DisplayPos.x;
        float y = drawData->DisplayPos.y;
        float width = drawData->DisplaySize.x;
        float height = drawData->DisplaySize.y;


        bx::mtxOrtho( ortho, x, x + ( width ), y + height, y, 0.0f, 1000.0f, 0.0f, caps->homogeneousDepth );
        bgfx::setViewTransform( viewId, NULL, ortho );
        bgfx::setViewRect( viewId, 0, 0, uint16_t( width ), uint16_t( height ) );
    }

    const ImVec2 clipPos = drawData->DisplayPos;         // (0,0) unless using multi-viewports
    const ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    for ( int32_t ii = 0, num = drawData->CmdListsCount; ii < num; ++ii )
    {
        OPTICK_EVENT( "ImGuiRenderer::CommandList", Optick::Category::Rendering );
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;

        const ImDrawList* drawList = drawData->CmdLists[ii];
        uint32_t numVertices = (uint32_t)drawList->VtxBuffer.size();
        uint32_t numIndices = (uint32_t)drawList->IdxBuffer.size();

        if ( !Moonlight::CheckAvailTransientBuffers( numVertices, Layout, numIndices ) )
        {
            // not enough space in transient buffer just quit drawing the rest...
            break;
        }

        bgfx::allocTransientVertexBuffer( &tvb, numVertices, Layout );
        bgfx::allocTransientIndexBuffer( &tib, numIndices, sizeof( ImDrawIdx ) == 4 );

        ImDrawVert* verts = (ImDrawVert*)tvb.data;
        bx::memCopy( verts, drawList->VtxBuffer.begin(), numVertices * sizeof( ImDrawVert ) );

        ImDrawIdx* indices = (ImDrawIdx*)tib.data;
        bx::memCopy( indices, drawList->IdxBuffer.begin(), numIndices * sizeof( ImDrawIdx ) );

        bgfx::Encoder* encoder = bgfx::begin();

        for ( const ImDrawCmd* cmd = drawList->CmdBuffer.begin(), *cmdEnd = drawList->CmdBuffer.end(); cmd != cmdEnd; ++cmd )
        {
            OPTICK_EVENT( "ImGuiRenderer::Command", Optick::Category::Rendering );
            if ( cmd->UserCallback )
            {
                cmd->UserCallback( drawList, cmd );
            }
            else if ( cmd->ElemCount != 0 )
            {
                uint64_t state = 0
                    | BGFX_STATE_WRITE_RGB
                    | BGFX_STATE_WRITE_A
                    | BGFX_STATE_MSAA
                    ;

                bgfx::TextureHandle th = mTexture;
                bgfx::ProgramHandle program = Program;

                if ( cmd->TextureId )
                {
                    union { ImTextureID ptr; struct { bgfx::TextureHandle handle; uint8_t flags; uint8_t mip; } s; } texture = { cmd->TextureId };
                    state |= 0 != ( IMGUI_FLAGS_ALPHA_BLEND & texture.s.flags )
                        ? BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA )
                        : BGFX_STATE_NONE
                        ;
                    th = texture.s.handle;
                    if ( 0 != texture.s.mip )
                    {
                        const float lodEnabled[4] = { static_cast<float>( texture.s.mip ), 1.f, 0.f, 0.f };
                        bgfx::setUniform( ImageLODEnabled, lodEnabled );
                        program = ImageProgram;
                    }
                }
                else
                {
                    state |= BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA );
                }

                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clipRect;
                clipRect.x = ( cmd->ClipRect.x - clipPos.x ) * clipScale.x;
                clipRect.y = ( cmd->ClipRect.y - clipPos.y ) * clipScale.y;
                clipRect.z = ( cmd->ClipRect.z - clipPos.x ) * clipScale.x;
                clipRect.w = ( cmd->ClipRect.w - clipPos.y ) * clipScale.y;

                if ( clipRect.x < fb_width
                    && clipRect.y < fb_height
                    && clipRect.z >= 0.0f
                    && clipRect.w >= 0.0f )
                {
                    const uint16_t xx = uint16_t( bx::max( clipRect.x, 0.0f ) );
                    const uint16_t yy = uint16_t( bx::max( clipRect.y, 0.0f ) );
                    encoder->setScissor( xx, yy
                        , uint16_t( bx::min( clipRect.z, 65535.0f ) - xx )
                        , uint16_t( bx::min( clipRect.w, 65535.0f ) - yy )
                    );

                    encoder->setState( state );
                    encoder->setTexture( 0, sTexture, th );
                    encoder->setVertexBuffer( 0, &tvb, cmd->VtxOffset, numVertices );
                    encoder->setIndexBuffer( &tib, cmd->IdxOffset, cmd->ElemCount );
                    encoder->submit( viewId, program );
                }
            }
        }

        bgfx::end( encoder );
    }
}

