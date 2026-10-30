#include "SplineGizmo.h"

#if USING( ME_EDITOR )

#include "Components/Gameplay/Spline.h"
#include "Components/Transform.h"
#include "Editor/EditorOperations.h"
#include "Editor/SceneTools.h"
#include "Editor/Selection.h"
#include "ECS/Entity.h"
#include "Math/Bounds.h"
#include "Widgets/SceneViewWidget.h"
#include <algorithm>
#include <vector>
#include <imgui.h>

namespace
{
    struct Drag
    {
        bool Active = false;
        int Point = -1;
        Vector3 PlanePoint;
        Vector3 PlaneNormal;
        json Before;
        EntityHandle Entity;
    } s_drag;


    Spline* SelectedSpline( EntityHandle& OutEntity )
    {
        OutEntity = Selection::Get().GetActive();
        if( !OutEntity || Selection::Get().Count() != 1 || !OutEntity->HasComponent<Transform>() )
        {
            return nullptr;
        }
        Spline* spline = OutEntity->TryGetComponent<Spline>();
        return spline && spline->IsEnabled() ? spline : nullptr;
    }


    // A world position on the World view's screen (ImGui's coordinates); false when behind the camera.
    bool Project( SceneViewWidget& InView, const Vector3& InWorld, const Vector2& InScreenMin, const Vector2& InScreenSize, ImVec2& OutScreen )
    {
        const Matrix4 viewProjection( InView.MainCamera->ProjectionMatrix.GetInternalMatrix() * InView.MainCamera->View.GetInternalMatrix() );
        Vector2 pixel;
        const bool visible = SceneTools::WorldToScreen( InWorld, InView.SceneViewRenderSize, viewProjection, pixel );
        OutScreen = ImVec2( InScreenMin.x + pixel.x * InScreenSize.x / std::max( InView.SceneViewRenderSize.x, 1.f ),
            InScreenMin.y + pixel.y * InScreenSize.y / std::max( InView.SceneViewRenderSize.y, 1.f ) );
        return visible;
    }


    Ray MouseRay( SceneViewWidget& InView, const ImVec2& InMouse, const Vector2& InScreenMin, const Vector2& InScreenSize )
    {
        const Vector2 pixel( ( InMouse.x - InScreenMin.x ) * InView.SceneViewRenderSize.x / std::max( InScreenSize.x, 1.f ),
            ( InMouse.y - InScreenMin.y ) * InView.SceneViewRenderSize.y / std::max( InScreenSize.y, 1.f ) );
        return SceneTools::ScreenRay( pixel, InView.SceneViewRenderSize, InView.MainCamera->View, InView.MainCamera->ProjectionMatrix );
    }
}


bool SplineGizmo::DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize )
{
    EntityHandle entity;
    Spline* spline = InView.MainCamera ? SelectedSpline( entity ) : nullptr;
    if( !spline || InView.IsUsingGuizmo )
    {
        s_drag.Active = false;
        return false;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int segments = SplineMath::GetSegmentCount( spline->Points.size(), spline->Closed );
    if( segments > 0 )
    {
        std::vector<ImVec2> line;
        const int samples = segments * 16;
        for( int i = 0; i <= samples; ++i )
        {
            ImVec2 at;
            if( Project( InView, spline->GetPoint( static_cast<float>( i ) / samples ), InScreenMin, InScreenSize, at ) )
            {
                line.push_back( at );
            }
            else if( line.size() > 1 )
            {
                draw->AddPolyline( line.data(), static_cast<int>( line.size() ), IM_COL32( 80, 220, 255, 230 ), ImDrawFlags_None, 2.f );
                line.clear();
            }
        }
        if( line.size() > 1 )
        {
            draw->AddPolyline( line.data(), static_cast<int>( line.size() ), IM_COL32( 80, 220, 255, 230 ), ImDrawFlags_None, 2.f );
        }
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovered = -1;
    std::vector<ImVec2> handles( spline->Points.size() );
    std::vector<bool> shown( spline->Points.size(), false );
    for( size_t i = 0; i < spline->Points.size(); ++i )
    {
        shown[i] = Project( InView, spline->GetWorldControlPoint( i ), InScreenMin, InScreenSize, handles[i] );
        const float dx = mouse.x - handles[i].x;
        const float dy = mouse.y - handles[i].y;
        if( shown[i] && dx * dx + dy * dy <= 64.f )
        {
            hovered = static_cast<int>( i );
        }
    }
    for( size_t i = 0; i < handles.size(); ++i )
    {
        if( !shown[i] )
        {
            continue;
        }
        const bool lit = static_cast<int>( i ) == hovered || ( s_drag.Active && static_cast<int>( i ) == s_drag.Point );
        draw->AddCircleFilled( handles[i], 5.f, lit ? IM_COL32( 255, 230, 120, 255 ) : IM_COL32( 80, 220, 255, 255 ) );
        draw->AddCircle( handles[i], 5.f, IM_COL32( 0, 0, 0, 200 ) );
        const std::string label = std::to_string( i );
        draw->AddText( ImVec2( handles[i].x + 7.f, handles[i].y - 16.f ), IM_COL32( 220, 240, 255, 220 ), label.c_str() );
    }

    if( !s_drag.Active && hovered >= 0 && InView.IsViewportHovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
    {
        s_drag.Active = true;
        s_drag.Point = hovered;
        s_drag.PlanePoint = spline->GetWorldControlPoint( static_cast<size_t>( hovered ) );
        s_drag.PlaneNormal = -MouseRay( InView, mouse, InScreenMin, InScreenSize ).Direction;
        s_drag.Before = EditorOps::CaptureComponent( *entity.Get(), "Spline" );
        s_drag.Entity = entity;
    }
    if( s_drag.Active )
    {
        if( s_drag.Entity != entity || s_drag.Point >= static_cast<int>( spline->Points.size() ) )
        {
            s_drag.Active = false;
            return false;
        }
        // The point follows the mouse across the plane through where it started, facing the camera.
        const Ray ray = MouseRay( InView, mouse, InScreenMin, InScreenSize );
        const float facing = ray.Direction.Dot( s_drag.PlaneNormal );
        if( std::abs( facing ) > 1e-5f )
        {
            const float distance = ( s_drag.PlanePoint - ray.Origin ).Dot( s_drag.PlaneNormal ) / facing;
            if( distance > 0.f )
            {
                const Vector3 hit = ray.Origin + ray.Direction * distance;
                const glm::vec4 local = entity->GetComponent<Transform>().GetWorldToLocalMatrix().GetInternalMatrix() * glm::vec4( hit.x, hit.y, hit.z, 1.f );
                spline->Points[static_cast<size_t>( s_drag.Point )] = Vector3( local.x, local.y, local.z );
            }
        }
        if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
        {
            s_drag.Active = false;
            EditorOps::RecordComponentEdit( *entity.Get(), "Spline", s_drag.Before, EditorOps::CaptureComponent( *entity.Get(), "Spline" ), "Move Spline Point" );
        }
        return true;
    }
    return hovered >= 0;
}


bool SplineGizmo::GetHandleScreenPosition( SceneViewWidget& InView, int InIndex, Vector2& OutPosition )
{
    EntityHandle entity;
    Spline* spline = InView.MainCamera ? SelectedSpline( entity ) : nullptr;
    ImVec2 at;
    if( !spline || InIndex < 0 || InIndex >= static_cast<int>( spline->Points.size() )
        || !Project( InView, spline->GetWorldControlPoint( static_cast<size_t>( InIndex ) ), InView.GetViewportScreenMin(), InView.GetViewportScreenSize(), at ) )
    {
        return false;
    }
    OutPosition = Vector2( at.x, at.y );
    return true;
}

#endif
