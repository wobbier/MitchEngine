#include "ColorPicking.h"
#if USING( ME_EDITOR )
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "Widgets/SceneViewWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ECS/Entity.h"
#include "ECS/Component.h"
#include "Camera/CameraData.h"
#include "Device/FrameBuffer.h"
#include "RenderViews.h"
#include "Reflection/Reflection.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Utils/ImGuiUtils.h"
#include <Widgets/ColorPopover.h>
#include <Widgets/Juice.h>
#include <ImGuiTheme.h>
#include <bgfx/bgfx.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace ColorPicking
{
    namespace
    {
        using Rgb = std::array<float, 3>;

        enum class Purpose : uint8_t
        {
            Preview,        // the loupe's reading while armed
            Sample,         // a click: the colour goes to the armed swatch
            Automation,     // assert-viewport-pixel
        };

        // One 1x1 GPU copy at a time (the RenderTextureAutomation pattern: blit on the capture
        // view, which runs after every camera's views, then read back once bgfx says it's ready).
        struct Readback
        {
            bgfx::TextureHandle Texture = BGFX_INVALID_HANDLE;
            uint8_t Pixel[4] = {};
            uint32_t ReadyFrame = 0;
            bool Pending = false;
            Purpose For = Purpose::Preview;
        };

        struct State
        {
            Readback Copy;
            bool Armed = false;
            ImGuiID Target = 0;
            uint64_t Entity = 0;
            // A click waiting for the copy slot, and the swatch it is for.
            bool SampleQueued = false;
            uint16_t SampleX = 0;
            uint16_t SampleY = 0;
            ImGuiID SampleTarget = 0;
            uint64_t SampleEntity = 0;
            // A sample that has landed, until its swatch takes it.
            bool HasResult = false;
            Rgb Result {};
            ImGuiID ResultTarget = 0;
            uint64_t ResultEntity = 0;
            bool HasPreview = false;
            Rgb Preview {};
            // assert-viewport-pixel's copy.
            bool HasAutomation = false;
            Rgb Automation {};
            int UpdatedFrame = -1;
        };

        State& Get()
        {
            static State state;
            return state;
        }

        uint32_t CurrentFrame()
        {
            return GetEngine().m_frameRenderSettings.m_currentFrame;
        }

        uint64_t ActiveEntity()
        {
            EntityHandle active = Selection::Get().GetActive();
            return active ? active->GetGUID() : 0;
        }

        Rgb FromPixel( const uint8_t* InBgra )
        {
            return { InBgra[2] / 255.f, InBgra[1] / 255.f, InBgra[0] / 255.f };
        }

        // The World View's displayed image and its size, or false when it has none.
        bool ViewImage( SceneViewWidget& InView, Moonlight::FrameBuffer*& OutBuffer, uint32_t& OutWidth, uint32_t& OutHeight )
        {
            Moonlight::FrameBuffer* buffer = InView.MainCamera ? InView.MainCamera->Buffer : nullptr;
            if( !buffer || !bgfx::isValid( buffer->Texture ) || !bgfx::isValid( buffer->Buffer ) )
            {
                return false;
            }
            OutBuffer = buffer;
            OutWidth = buffer->GetOutputWidth();
            OutHeight = buffer->GetOutputHeight();
            return OutWidth > 0 && OutHeight > 0;
        }

        // The texel under a point given in the view's render pixels (SceneViewRenderSize units,
        // which is what the shown part of the texture spans).
        bool Texel( SceneViewWidget& InView, Vector2 InRenderPixel, uint16_t& OutX, uint16_t& OutY )
        {
            Moonlight::FrameBuffer* buffer = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
            if( !ViewImage( InView, buffer, width, height ) )
            {
                return false;
            }
            const int x = static_cast<int>( std::floor( InRenderPixel.x ) );
            const int y = static_cast<int>( std::floor( InRenderPixel.y ) );
            OutX = static_cast<uint16_t>( std::clamp( x, 0, static_cast<int>( width ) - 1 ) );
            OutY = static_cast<uint16_t>( std::clamp( y, 0, static_cast<int>( height ) - 1 ) );
            return true;
        }

        bool StartCopy( SceneViewWidget& InView, uint16_t InX, uint16_t InY, Purpose InFor )
        {
            State& state = Get();
            if( state.Copy.Pending )
            {
                return false;
            }
            Moonlight::FrameBuffer* buffer = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
            if( !ViewImage( InView, buffer, width, height ) )
            {
                return false;
            }
            if( !bgfx::isValid( state.Copy.Texture ) )
            {
                // The view's final image is BGRA8 (FrameBuffer::ReCreate).
                state.Copy.Texture = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK );
            }
            bgfx::TextureRegion destination;
            bgfx::TextureRegion source;
            destination.init( state.Copy.Texture );
            source.init( buffer->Texture, InX, InY, 1, 1 );
            bgfx::blit( Moonlight::RenderView::EditorCapture, destination, source );
            state.Copy.ReadyFrame = bgfx::read( destination, state.Copy.Pixel );
            state.Copy.Pending = true;
            state.Copy.For = InFor;
            return true;
        }

        // Once a frame: lands finished copies and cancels on Esc.
        void Update()
        {
            State& state = Get();
            const int frame = ImGui::GetFrameCount();
            if( state.UpdatedFrame == frame )
            {
                return;
            }
            state.UpdatedFrame = frame;
            if( state.Armed && ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
            {
                Cancel();
                MeWidgets::Juice::Play( MeWidgets::Juice::Cue::Reset );
            }
            if( state.Copy.Pending && CurrentFrame() >= state.Copy.ReadyFrame )
            {
                state.Copy.Pending = false;
                const Rgb color = FromPixel( state.Copy.Pixel );
                switch( state.Copy.For )
                {
                case Purpose::Preview:
                    state.Preview = color;
                    state.HasPreview = true;
                    break;
                case Purpose::Sample:
                    state.Result = color;
                    state.ResultTarget = state.SampleTarget;
                    state.ResultEntity = state.SampleEntity;
                    state.HasResult = true;
                    break;
                case Purpose::Automation:
                    state.Automation = color;
                    state.HasAutomation = true;
                    break;
                }
            }
        }

        void Arm( ImGuiID InId )
        {
            State& state = Get();
            state.Armed = true;
            state.Target = InId;
            state.Entity = ActiveEntity();
            state.HasPreview = false;
        }

        bool IsArmedFor( ImGuiID InId )
        {
            Update();
            const State& state = Get();
            return ( state.Armed && state.Target == InId ) || ( state.SampleQueued && state.SampleTarget == InId )
                || ( state.Copy.Pending && state.Copy.For == Purpose::Sample && state.SampleTarget == InId );
        }

        bool Take( ImGuiID InId, Rgb& OutRgb )
        {
            Update();
            State& state = Get();
            if( !state.HasResult || state.ResultTarget != InId )
            {
                return false;
            }
            state.HasResult = false;
            // The swatch's ID is per field row: a sample for an entity no longer shown is dropped.
            if( state.ResultEntity != ActiveEntity() )
            {
                return false;
            }
            OutRgb = state.Result;
            return true;
        }

        // Text with a soft halo, readable over any part of the scene.
        void HaloText( ImDrawList* InDrawList, ImVec2 InPos, const char* InText )
        {
            const float o = std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
            const ImU32 halo = ImGui::GetColorU32( ImGuiTheme::Gray( 0.f, 0.85f ) );
            InDrawList->AddText( ImVec2( InPos.x - o, InPos.y ), halo, InText );
            InDrawList->AddText( ImVec2( InPos.x + o, InPos.y ), halo, InText );
            InDrawList->AddText( ImVec2( InPos.x, InPos.y - o ), halo, InText );
            InDrawList->AddText( ImVec2( InPos.x, InPos.y + o ), halo, InText );
            InDrawList->AddText( InPos, IM_COL32_WHITE, InText );
        }
    }

    void Install()
    {
        MeWidgets::ColorPopoverHooks hooks;
        hooks.SceneColors = &GatherSceneColors;
        hooks.ArmEyedropper = &Arm;
        hooks.IsEyedropperArmed = &IsArmedFor;
        hooks.TakeEyedropped = &Take;
        MeWidgets::SetColorPopoverHooks( std::move( hooks ) );
    }

    bool IsArmed()
    {
        return Get().Armed;
    }

    void Cancel()
    {
        State& state = Get();
        state.Armed = false;
        state.Target = 0;
        state.HasPreview = false;
    }

    bool DrawViewport( SceneViewWidget& InView, const Vector2& InImageMin, const Vector2& InImageSize )
    {
        Update();
        State& state = Get();
        if( state.SampleQueued && StartCopy( InView, state.SampleX, state.SampleY, Purpose::Sample ) )
        {
            state.SampleQueued = false;
        }
        if( !state.Armed )
        {
            return state.SampleQueued;
        }

        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float pad = ImGuiTheme::Em( 0.4f );
        const float thin = std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        const ImVec2 imageMin( InImageMin.x, InImageMin.y );
        const ImVec2 imageMax( InImageMin.x + InImageSize.x, InImageMin.y + InImageSize.y );

        // What's going on, at the bottom of the view (the stats sit at the top).
        {
            const char* hint = "Eyedropper: click to take the colour on screen. Esc cancels.";
            const ImVec2 size = ImGui::CalcTextSize( hint );
            const float height = size.y + pad * 1.5f;
            const ImVec2 min( std::round( imageMin.x + ( InImageSize.x - size.x ) * 0.5f - pad ), std::round( imageMax.y - ImGuiTheme::Em( 1.f ) - height ) );
            const ImVec2 max( min.x + size.x + pad * 2.f, min.y + height );
            drawList->AddRectFilled( min, max, ImGui::GetColorU32( ImGuiTheme::WithAlpha( palette.Popup, 0.92f ) ), ImGui::GetStyle().FrameRounding );
            drawList->AddRect( min, max, ImGui::GetColorU32( palette.AccentText ), ImGui::GetStyle().FrameRounding, 0, thin );
            drawList->AddText( ImVec2( min.x + pad, min.y + pad * 0.75f ), ImGui::GetColorU32( palette.Text ), hint );
        }

        if( !InView.IsViewportHovered )
        {
            return true;
        }

        uint16_t texelX = 0;
        uint16_t texelY = 0;
        if( !Texel( InView, InView.ViewportMousePosition, texelX, texelY ) )
        {
            return true;
        }
        if( !state.Copy.Pending && !state.SampleQueued )
        {
            StartCopy( InView, texelX, texelY, Purpose::Preview );
        }

        if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
        {
            state.SampleQueued = true;
            state.SampleX = texelX;
            state.SampleY = texelY;
            state.SampleTarget = state.Target;
            state.SampleEntity = state.Entity;
            state.Armed = false;
            if( StartCopy( InView, texelX, texelY, Purpose::Sample ) )
            {
                state.SampleQueued = false;
            }
            return true;
        }

        // A crosshair at the pointer and a loupe of the pixels around it.
        ImGui::SetMouseCursor( ImGuiMouseCursor_None );
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        drawList->PushClipRect( imageMin, imageMax, true );
        const float arm = ImGuiTheme::Em( 0.8f );
        const float hole = ImGuiTheme::Em( 0.25f );
        const ImU32 dark = ImGui::GetColorU32( ImGuiTheme::Gray( 0.f, 0.8f ) );
        const ImU32 light = IM_COL32_WHITE;
        for( int pass = 0; pass < 2; ++pass )
        {
            const ImU32 color = pass == 0 ? dark : light;
            const float width = pass == 0 ? thin * 3.f : thin;
            drawList->AddLine( ImVec2( mouse.x - arm, mouse.y ), ImVec2( mouse.x - hole, mouse.y ), color, width );
            drawList->AddLine( ImVec2( mouse.x + hole, mouse.y ), ImVec2( mouse.x + arm, mouse.y ), color, width );
            drawList->AddLine( ImVec2( mouse.x, mouse.y - arm ), ImVec2( mouse.x, mouse.y - hole ), color, width );
            drawList->AddLine( ImVec2( mouse.x, mouse.y + hole ), ImVec2( mouse.x, mouse.y + arm ), color, width );
        }

        Moonlight::FrameBuffer* buffer = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        if( ViewImage( InView, buffer, width, height ) )
        {
            const float radius = ImGuiTheme::Em( 3.f );
            constexpr float kZoom = 8.f;
            const float halfTexels = radius / kZoom;
            ImVec2 center( mouse.x + arm + radius, mouse.y - arm - radius );
            if( center.x + radius > imageMax.x ) center.x = mouse.x - arm - radius;
            if( center.y - radius < imageMin.y ) center.y = mouse.y + arm + radius;
            const ImVec2 uvMin( ( texelX + 0.5f - halfTexels ) / width, ( texelY + 0.5f - halfTexels ) / height );
            const ImVec2 uvMax( ( texelX + 0.5f + halfTexels ) / width, ( texelY + 0.5f + halfTexels ) / height );
            const ImVec2 min( center.x - radius, center.y - radius );
            const ImVec2 max( center.x + radius, center.y + radius );
            drawList->AddCircleFilled( center, radius + thin * 2.f, dark, 48 );
            drawList->AddImageRounded( ImGui::toId( buffer->Texture, IMGUI_FLAGS_NONE, 0 ), min, max, uvMin, uvMax, IM_COL32_WHITE, radius );
            drawList->AddCircle( center, radius + thin, light, 48, thin * 1.5f );
            // The sampled pixel, outlined.
            const float half = kZoom * 0.5f;
            drawList->AddRect( ImVec2( center.x - half - thin, center.y - half - thin ), ImVec2( center.x + half + thin, center.y + half + thin ), dark, 0.f, 0, thin * 2.f );
            drawList->AddRect( ImVec2( center.x - half, center.y - half ), ImVec2( center.x + half, center.y + half ), light, 0.f, 0, thin );
            if( state.HasPreview )
            {
                const std::string hex = MeWidgets::ColorMath::FormatHex( state.Preview );
                const ImVec2 size = ImGui::CalcTextSize( hex.c_str() );
                HaloText( drawList, ImVec2( std::round( center.x - size.x * 0.5f ), center.y + radius + thin * 3.f ), hex.c_str() );
            }
        }
        drawList->PopClipRect();
        ImGui::SetTooltip( "The colour on screen (after lighting, exposure and tonemapping)" );
        return true;
    }

    bool ReadViewportPixel( SceneViewWidget& InView, float InX, float InY, std::array<float, 3>& OutRgb, std::string& OutError, bool& OutRetry )
    {
        Update();
        State& state = Get();
        if( state.HasAutomation )
        {
            state.HasAutomation = false;
            OutRgb = state.Automation;
            return true;
        }
        if( state.Copy.Pending )
        {
            // Our copy (or someone else's) is in flight.
            OutRetry = true;
            return true;
        }
        uint16_t x = 0;
        uint16_t y = 0;
        if( !InView.IsImageShown() || !Texel( InView, Vector2( InX * InView.SceneViewRenderSize.x, InY * InView.SceneViewRenderSize.y ), x, y ) )
        {
            OutError = "the World View shows no image";
            return false;
        }
        if( !StartCopy( InView, x, y, Purpose::Automation ) )
        {
            OutError = "couldn't copy the World View's image";
            return false;
        }
        OutRetry = true;
        return true;
    }

    std::vector<std::array<float, 3>> GatherSceneColors()
    {
        std::vector<Rgb> colors;
        World& world = EditorOps::GetWorld();
        world.ForEachEntity( [&]( Entity& InEntity ) {
            for( BaseComponent* component : InEntity.GetAllComponents() )
            {
                const Reflection::TypeInfo* type = component ? component->GetTypeInfo() : nullptr;
                if( !type )
                {
                    continue;
                }
                type->ForEachField( static_cast<void*>( component ), [&]( const Reflection::FieldInfo& InField, void* InOwner ) {
                    if( !InField.Has( Reflection::FieldFlags::Color ) )
                    {
                        return;
                    }
                    const void* value = InField.Get( InOwner );
                    Rgb color {};
                    if( InField.GetType() == Reflection::PropertyType::Vector3 )
                    {
                        const Vector3& v = *static_cast<const Vector3*>( value );
                        color = { v.x, v.y, v.z };
                    }
                    else if( InField.GetType() == Reflection::PropertyType::Vector4 )
                    {
                        const Vector4& v = *static_cast<const Vector4*>( value );
                        color = { v.x, v.y, v.z };
                    }
                    else
                    {
                        return;
                    }
                    colors.push_back( InField.Has( Reflection::FieldFlags::HDR ) ? MeWidgets::ColorMath::SplitHdr( color ).Base : color );
                } );
            }
        } );
        return colors;
    }
}
#endif
