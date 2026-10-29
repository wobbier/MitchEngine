#include "SpriteRectGizmo.h"

#if USING( ME_EDITOR )

#include "Components/Graphics/SpriteRenderer.h"
#include "Components/Transform.h"
#include "Editor/EditorOperations.h"
#include "Editor/SceneTools.h"
#include "Editor/Selection.h"
#include "ECS/Entity.h"
#include "Widgets/SceneViewWidget.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

namespace
{
    struct Quad
    {
        // Local XY of the corners (pivot at the origin) and the 8 handles on screen.
        float X0 = 0.f, X1 = 0.f, Y0 = 0.f, Y1 = 0.f;
        std::array<ImVec2, SpriteRectGizmo::Count> Handles;
        std::array<ImVec2, 4> Corners;
        // Screen pixels per local unit along the sprite's X and Y.
        ImVec2 AxisX, AxisY;
        Vector2 Size;
        Vector2 Pivot;
        Vector4 Border;
        float PixelsPerUnit = 100.f;
    };

    struct Drag
    {
        bool Active = false;
        int Handle = -1;
        ImVec2 StartMouse;
        Vector2 StartSize;
        json Before;
        EntityHandle Entity;
    } s_drag;

    SpriteRenderer* SelectedSprite( EntityHandle& OutEntity )
    {
        OutEntity = Selection::Get().GetActive();
        if( !OutEntity || Selection::Get().Count() != 1 || !OutEntity->HasComponent<Transform>() )
        {
            return nullptr;
        }
        SpriteRenderer* sprite = OutEntity->TryGetComponent<SpriteRenderer>();
        return sprite && sprite->IsEnabled() ? sprite : nullptr;
    }

    bool Build( SceneViewWidget& InView, Entity& InEntity, SpriteRenderer& InSprite, const Vector2& InScreenMin, const Vector2& InScreenSize, Quad& OutQuad )
    {
        InSprite.Resolve();
        Vector4 frame;
        InSprite.GetFrame( frame, OutQuad.Pivot, OutQuad.Border );
        OutQuad.Size = InSprite.GetWorldSize();
        OutQuad.PixelsPerUnit = std::max( InSprite.PixelsPerUnit, 0.01f );
        OutQuad.X0 = -OutQuad.Pivot.x * OutQuad.Size.x;
        OutQuad.X1 = ( 1.f - OutQuad.Pivot.x ) * OutQuad.Size.x;
        OutQuad.Y0 = -OutQuad.Pivot.y * OutQuad.Size.y;
        OutQuad.Y1 = ( 1.f - OutQuad.Pivot.y ) * OutQuad.Size.y;
        const glm::mat4& world = InEntity.GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix();
        const Matrix4 viewProjection( InView.MainCamera->ProjectionMatrix.GetInternalMatrix() * InView.MainCamera->View.GetInternalMatrix() );
        bool visible = true;
        auto project = [&]( float InX, float InY ) {
            const glm::vec4 point = world * glm::vec4( InX, InY, 0.f, 1.f );
            Vector2 pixel;
            visible &= SceneTools::WorldToScreen( Vector3( point ), InView.SceneViewRenderSize, viewProjection, pixel );
            return ImVec2( InScreenMin.x + pixel.x * InScreenSize.x / std::max( InView.SceneViewRenderSize.x, 1.f ),
                InScreenMin.y + pixel.y * InScreenSize.y / std::max( InView.SceneViewRenderSize.y, 1.f ) );
        };
        const float midX = ( OutQuad.X0 + OutQuad.X1 ) * 0.5f;
        const float midY = ( OutQuad.Y0 + OutQuad.Y1 ) * 0.5f;
        OutQuad.Corners = { project( OutQuad.X0, OutQuad.Y0 ), project( OutQuad.X1, OutQuad.Y0 ), project( OutQuad.X1, OutQuad.Y1 ), project( OutQuad.X0, OutQuad.Y1 ) };
        OutQuad.Handles[SpriteRectGizmo::Left] = project( OutQuad.X0, midY );
        OutQuad.Handles[SpriteRectGizmo::Right] = project( OutQuad.X1, midY );
        OutQuad.Handles[SpriteRectGizmo::Bottom] = project( midX, OutQuad.Y0 );
        OutQuad.Handles[SpriteRectGizmo::Top] = project( midX, OutQuad.Y1 );
        OutQuad.Handles[SpriteRectGizmo::BottomLeft] = OutQuad.Corners[0];
        OutQuad.Handles[SpriteRectGizmo::BottomRight] = OutQuad.Corners[1];
        OutQuad.Handles[SpriteRectGizmo::TopRight] = OutQuad.Corners[2];
        OutQuad.Handles[SpriteRectGizmo::TopLeft] = OutQuad.Corners[3];
        const ImVec2 origin = project( 0.f, 0.f );
        const ImVec2 unitX = project( 1.f, 0.f );
        const ImVec2 unitY = project( 0.f, 1.f );
        OutQuad.AxisX = ImVec2( unitX.x - origin.x, unitX.y - origin.y );
        OutQuad.AxisY = ImVec2( unitY.x - origin.x, unitY.y - origin.y );
        return visible;
    }

    // Local units a screen movement covers along an axis (its screen direction and length).
    float AlongAxis( const ImVec2& InDelta, const ImVec2& InAxis )
    {
        const float lengthSquared = InAxis.x * InAxis.x + InAxis.y * InAxis.y;
        return lengthSquared > 1e-6f ? ( InDelta.x * InAxis.x + InDelta.y * InAxis.y ) / lengthSquared : 0.f;
    }

    // A new extent for a side moved by InUnits, the pivot staying put: the side's distance from
    // the pivot changes, so the size scales by it.
    float Resize( float InSize, float InPivot, bool InFarSide, float InUnits )
    {
        const float share = InFarSide ? 1.f - InPivot : InPivot;
        const float moved = InFarSide ? InUnits : -InUnits;
        return share > 0.05f ? std::max( InSize + moved / share, 0.01f ) : std::max( InSize + moved, 0.01f );
    }
}


bool SpriteRectGizmo::DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize )
{
    EntityHandle entity;
    SpriteRenderer* sprite = InView.MainCamera ? SelectedSprite( entity ) : nullptr;
    if( !sprite || InView.IsUsingGuizmo )
    {
        s_drag.Active = false;
        return false;
    }
    Quad quad;
    if( !Build( InView, *entity.Get(), *sprite, InScreenMin, InScreenSize, quad ) )
    {
        return s_drag.Active;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 outline = IM_COL32( 255, 170, 40, 230 );
    draw->AddPolyline( quad.Corners.data(), 4, outline, ImDrawFlags_Closed, 1.5f );
    // 9-slice borders, dashed.
    if( quad.Border.x > 0.f || quad.Border.y > 0.f || quad.Border.z > 0.f || quad.Border.w > 0.f )
    {
        const glm::mat4& world = entity->GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix();
        (void)world;
        auto lerp = []( const ImVec2& InA, const ImVec2& InB, float InT ) { return ImVec2( InA.x + ( InB.x - InA.x ) * InT, InA.y + ( InB.y - InA.y ) * InT ); };
        auto dashed = [&]( const ImVec2& InA, const ImVec2& InB ) {
            for( int i = 0; i < 16; i += 2 )
            {
                draw->AddLine( lerp( InA, InB, i / 16.f ), lerp( InA, InB, ( i + 1 ) / 16.f ), IM_COL32( 120, 200, 255, 200 ) );
            }
        };
        const float width = std::max( quad.X1 - quad.X0, 1e-4f );
        const float height = std::max( quad.Y1 - quad.Y0, 1e-4f );
        // Borders are texels (left, top, right, bottom): in units, then as fractions of the quad.
        const float left = std::clamp( quad.Border.x / quad.PixelsPerUnit / width, 0.f, 1.f );
        const float right = std::clamp( 1.f - quad.Border.z / quad.PixelsPerUnit / width, 0.f, 1.f );
        const float bottom = std::clamp( quad.Border.w / quad.PixelsPerUnit / height, 0.f, 1.f );
        const float top = std::clamp( 1.f - quad.Border.y / quad.PixelsPerUnit / height, 0.f, 1.f );
        for( float u : { left, right } )
        {
            dashed( lerp( quad.Corners[0], quad.Corners[1], u ), lerp( quad.Corners[3], quad.Corners[2], u ) );
        }
        for( float v : { bottom, top } )
        {
            dashed( lerp( quad.Corners[0], quad.Corners[3], v ), lerp( quad.Corners[1], quad.Corners[2], v ) );
        }
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovered = -1;
    for( int i = 0; i < Count; ++i )
    {
        const float dx = mouse.x - quad.Handles[i].x;
        const float dy = mouse.y - quad.Handles[i].y;
        if( dx * dx + dy * dy <= 49.f )
        {
            hovered = i;
        }
    }
    for( int i = 0; i < Count; ++i )
    {
        const bool lit = i == hovered || ( s_drag.Active && i == s_drag.Handle );
        const ImVec2& at = quad.Handles[i];
        draw->AddRectFilled( ImVec2( at.x - 4.f, at.y - 4.f ), ImVec2( at.x + 4.f, at.y + 4.f ), lit ? IM_COL32( 255, 230, 120, 255 ) : IM_COL32( 255, 170, 40, 255 ) );
    }

    if( !s_drag.Active && hovered >= 0 && InView.IsViewportHovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
    {
        s_drag.Active = true;
        s_drag.Handle = hovered;
        s_drag.StartMouse = mouse;
        s_drag.StartSize = quad.Size;
        s_drag.Before = EditorOps::CaptureComponent( *entity.Get(), "SpriteRenderer" );
        s_drag.Entity = entity;
    }
    if( s_drag.Active )
    {
        if( s_drag.Entity != entity )
        {
            s_drag.Active = false;
            return false;
        }
        const ImVec2 delta( mouse.x - s_drag.StartMouse.x, mouse.y - s_drag.StartMouse.y );
        const float unitsX = AlongAxis( delta, quad.AxisX );
        const float unitsY = AlongAxis( delta, quad.AxisY );
        Vector2 size = s_drag.StartSize;
        const int handle = s_drag.Handle;
        if( handle == Left || handle == BottomLeft || handle == TopLeft ) size.x = Resize( s_drag.StartSize.x, quad.Pivot.x, false, unitsX );
        if( handle == Right || handle == BottomRight || handle == TopRight ) size.x = Resize( s_drag.StartSize.x, quad.Pivot.x, true, unitsX );
        if( handle == Bottom || handle == BottomLeft || handle == BottomRight ) size.y = Resize( s_drag.StartSize.y, quad.Pivot.y, false, unitsY );
        if( handle == Top || handle == TopLeft || handle == TopRight ) size.y = Resize( s_drag.StartSize.y, quad.Pivot.y, true, unitsY );
        sprite->Size = size;
        if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
        {
            s_drag.Active = false;
            EditorOps::RecordComponentEdit( *entity.Get(), "SpriteRenderer", s_drag.Before, EditorOps::CaptureComponent( *entity.Get(), "SpriteRenderer" ), "Resize Sprite" );
        }
        return true;
    }
    return hovered >= 0;
}


bool SpriteRectGizmo::GetHandleScreenPosition( SceneViewWidget& InView, int InHandle, Vector2& OutPosition )
{
    EntityHandle entity;
    SpriteRenderer* sprite = InView.MainCamera ? SelectedSprite( entity ) : nullptr;
    Quad quad;
    if( !sprite || InHandle < 0 || InHandle >= Count || !Build( InView, *entity.Get(), *sprite, InView.GetViewportScreenMin(), InView.GetViewportScreenSize(), quad ) )
    {
        return false;
    }
    OutPosition = Vector2( quad.Handles[InHandle].x, quad.Handles[InHandle].y );
    return true;
}


int SpriteRectGizmo::HandleFromName( const std::string& InName )
{
    const char* names[Count] = { "left", "right", "bottom", "top", "bottom-left", "bottom-right", "top-right", "top-left" };
    for( int i = 0; i < Count; ++i )
    {
        if( InName == names[i] )
        {
            return i;
        }
    }
    return -1;
}

#endif
