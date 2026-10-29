#include "TilemapPainter.h"

#if USING( ME_EDITOR )

#include "Components/Graphics/Tilemap.h"
#include "Components/Transform.h"
#include "Editor/EditorOperations.h"
#include "Editor/SceneTools.h"
#include "Editor/Selection.h"
#include "ECS/Entity.h"
#include "Widgets/SceneViewWidget.h"
#include <imgui.h>
#include <cmath>

namespace
{
    json s_strokeBefore;
    uint64_t s_strokeEntity = 0;
    bool s_stroking = false;


    // The cell under a viewport pixel, on the tilemap's local XY plane.
    bool CellUnder( SceneViewWidget& InView, Entity& InEntity, const Vector2& InPixel, int& OutX, int& OutY )
    {
        if( !InView.MainCamera )
        {
            return false;
        }
        Transform& transform = InEntity.GetComponent<Transform>();
        const Ray ray = SceneTools::ScreenRay( InPixel, InView.SceneViewRenderSize, InView.MainCamera->View, InView.MainCamera->ProjectionMatrix );
        const glm::mat4& world = transform.GetLocalToWorldMatrix().GetInternalMatrix();
        const glm::vec3 origin = glm::vec3( world[3] );
        const glm::vec3 normal = glm::normalize( glm::vec3( world[2] ) );
        const float facing = glm::dot( normal, ray.Direction.InternalVector );
        if( std::abs( facing ) < 1e-5f )
        {
            return false;
        }
        const float distance = glm::dot( origin - ray.Origin.InternalVector, normal ) / facing;
        if( distance < 0.f )
        {
            return false;
        }
        const glm::vec3 hit = ray.Origin.InternalVector + ray.Direction.InternalVector * distance;
        const glm::vec4 local = transform.GetWorldToLocalMatrix().GetInternalMatrix() * glm::vec4( hit, 1.f );
        InEntity.GetComponent<Tilemap>().LocalToCell( Vector2( local.x, local.y ), OutX, OutY );
        return true;
    }


    void EndStroke( Entity* InEntity )
    {
        if( s_stroking && InEntity && InEntity->GetGUID() == s_strokeEntity )
        {
            EditorOps::RecordComponentEdit( *InEntity, "Tilemap", s_strokeBefore, EditorOps::CaptureComponent( *InEntity, "Tilemap" ), "Paint tiles" );
        }
        s_stroking = false;
    }
}


bool TilemapPainter::PaintAt( SceneViewWidget& InView, Entity& InEntity, const Vector2& InViewportPixel, int InPaletteIndex )
{
    Tilemap* tilemap = InEntity.TryGetComponent<Tilemap>();
    int x = 0;
    int y = 0;
    if( !tilemap || !CellUnder( InView, InEntity, InViewportPixel, x, y ) )
    {
        return false;
    }
    const json before = EditorOps::CaptureComponent( InEntity, "Tilemap" );
    tilemap->SetTile( x, y, InPaletteIndex );
    EditorOps::RecordComponentEdit( InEntity, "Tilemap", before, EditorOps::CaptureComponent( InEntity, "Tilemap" ), "Paint tiles" );
    return true;
}


bool TilemapPainter::DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize )
{
    auto entity = Selection::Get().GetActive();
    Tilemap* tilemap = entity && entity->IsActiveInHierarchy() ? entity->TryGetComponent<Tilemap>() : nullptr;
    if( !Tilemap::s_paintEnabled || !tilemap || !InView.MainCamera || InView.IsUsingGuizmo )
    {
        EndStroke( entity ? entity.Get() : nullptr );
        return false;
    }
    const bool painting = ImGui::IsMouseDown( ImGuiMouseButton_Left );
    const bool erasing = ImGui::IsMouseDown( ImGuiMouseButton_Right );
    if( ImGui::GetIO().KeyAlt || ImGui::IsMouseDown( ImGuiMouseButton_Middle ) )
    {
        EndStroke( entity.Get() );
        return false;
    }
    if( !painting && !erasing )
    {
        EndStroke( entity.Get() );
    }
    if( !InView.IsViewportHovered )
    {
        return true;
    }
    int x = 0;
    int y = 0;
    if( !CellUnder( InView, *entity.Get(), InView.ViewportMousePosition, x, y ) )
    {
        return true;
    }
    // The brush cell's outline.
    Transform& transform = entity->GetComponent<Transform>();
    const glm::mat4& world = transform.GetLocalToWorldMatrix().GetInternalMatrix();
    const Matrix4 viewProjection( InView.MainCamera->ProjectionMatrix.GetInternalMatrix() * InView.MainCamera->View.GetInternalMatrix() );
    const float size = tilemap->CellSize;
    ImVec2 corners[4];
    bool visible = true;
    for( int i = 0; i < 4; ++i )
    {
        const glm::vec3 point = glm::vec3( world * glm::vec4( ( x + ( i == 1 || i == 2 ) ) * size, ( y + ( i >= 2 ) ) * size, 0.f, 1.f ) );
        Vector2 pixel;
        visible &= SceneTools::WorldToScreen( Vector3( point ), InView.SceneViewRenderSize, viewProjection, pixel );
        corners[i] = ImVec2( InScreenMin.x + pixel.x * InScreenSize.x / InView.SceneViewRenderSize.x, InScreenMin.y + pixel.y * InScreenSize.y / InView.SceneViewRenderSize.y );
    }
    if( visible )
    {
        ImGui::GetWindowDrawList()->AddPolyline( corners, 4, Tilemap::s_paintIndex < 0 ? IM_COL32( 255, 90, 90, 255 ) : IM_COL32( 255, 215, 90, 255 ), ImDrawFlags_Closed, 2.f );
    }
    if( painting || erasing )
    {
        if( !s_stroking )
        {
            s_stroking = true;
            s_strokeEntity = entity->GetGUID();
            s_strokeBefore = EditorOps::CaptureComponent( *entity.Get(), "Tilemap" );
            ImGui::SetWindowFocus();
        }
        tilemap->SetTile( x, y, erasing ? -1 : Tilemap::s_paintIndex );
    }
    return true;
}

#endif
