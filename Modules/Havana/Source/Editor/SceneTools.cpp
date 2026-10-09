#include "SceneTools.h"

#if USING( ME_EDITOR )

#include "EditorOperations.h"
#include "Selection.h"
#include "Components/Camera.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Lighting/Light.h"
#include "Components/Effects/ParticleSystem.h"
#include "Components/Transform.h"
#include "Debug/DebugDraw.h"
#include "Engine/World.h"
#include <algorithm>
#include <cmath>

namespace SceneTools
{
    namespace
    {
        void AccumulateBounds( Transform& InTransform, bool InRecursive, AABB& OutBounds )
        {
            Entity* entity = InTransform.Parent.Get();
            if( entity )
            {
                Mesh* mesh = entity->TryGetComponent<Mesh>();
                if( mesh && mesh->MeshReferece && mesh->MeshReferece->Bounds.IsValid() )
                {
                    OutBounds.Encapsulate( mesh->MeshReferece->Bounds.Transformed( InTransform.GetLocalToWorldMatrix() ) );
                }
            }
            if( InRecursive )
            {
                for( Transform* child : InTransform.GetChildren() )
                {
                    AccumulateBounds( *child, true, OutBounds );
                }
            }
        }


        Vector3 Transform3( const glm::mat4& InMatrix, const Vector3& InPoint, float InW )
        {
            const glm::vec4 result = InMatrix * glm::vec4( InPoint.InternalVector, InW );
            return Vector3( result.x, result.y, result.z );
        }


        void DrawGrid( const Vector3& InCameraPosition )
        {
            // Spacing follows the camera height in powers of ten; lines fade out with distance.
            const float height = std::max( std::fabs( InCameraPosition.y ), 0.5f );
            const float spacing = std::pow( 10.f, std::floor( std::log10( height ) ) );
            const int halfCount = 50;
            const float extent = halfCount * spacing;
            const float snap = spacing * 10.f;
            const float centerX = std::round( InCameraPosition.x / snap ) * snap;
            const float centerZ = std::round( InCameraPosition.z / snap ) * snap;
            const int segments = 12;
            const float segmentLength = ( extent * 2.f ) / segments;

            auto drawLine = [&]( bool alongX, float offset, Vector4 color ) {
                for( int s = 0; s < segments; ++s )
                {
                    const float a = -extent + s * segmentLength;
                    const float b = a + segmentLength;
                    const Vector3 from = alongX ? Vector3( centerX + a, 0.f, offset ) : Vector3( offset, 0.f, centerZ + a );
                    const Vector3 to = alongX ? Vector3( centerX + b, 0.f, offset ) : Vector3( offset, 0.f, centerZ + b );
                    const Vector3 middle = ( from + to ) * 0.5f;
                    const float distance = Vector3( middle.x - InCameraPosition.x, 0.f, middle.z - InCameraPosition.z ).Length();
                    Vector4 faded = color;
                    faded.w *= std::clamp( 1.f - distance / extent, 0.f, 1.f );
                    if( faded.w > 0.01f )
                    {
                        DebugDraw::Line( from, to, faded, 0.f, DebugDraw::EditorOnly );
                    }
                }
            };

            for( int i = -halfCount; i <= halfCount; ++i )
            {
                const float offsetZ = centerZ + i * spacing;
                const float offsetX = centerX + i * spacing;
                const bool majorZ = std::fabs( std::fmod( offsetZ, snap ) ) < spacing * 0.01f;
                const bool majorX = std::fabs( std::fmod( offsetX, snap ) ) < spacing * 0.01f;
                const bool axisZ = std::fabs( offsetZ ) < spacing * 0.01f;
                const bool axisX = std::fabs( offsetX ) < spacing * 0.01f;
                drawLine( true, offsetZ, axisZ ? Vector4( 0.9f, 0.25f, 0.25f, 0.8f ) : Vector4( 0.6f, 0.6f, 0.6f, majorZ ? 0.45f : 0.18f ) );
                drawLine( false, offsetX, axisX ? Vector4( 0.25f, 0.45f, 0.95f, 0.8f ) : Vector4( 0.6f, 0.6f, 0.6f, majorX ? 0.45f : 0.18f ) );
            }
        }


        void DrawSelection()
        {
            const EntityHandle active = Selection::Get().GetActive();
            for( const EntityHandle& handle : Selection::Get().GetEntities() )
            {
                Transform* transform = handle->TryGetComponent<Transform>();
                if( !transform )
                {
                    continue;
                }
                const Vector4 color = handle == active ? DebugDraw::Orange : Vector4( 1.f, 0.75f, 0.35f, 1.f );
                AABB bounds = ComputeWorldBounds( *handle.Get(), true );
                if( bounds.IsValid() )
                {
                    // Slightly inflated so the box doesn't z-fight with the faces it wraps.
                    const Vector3 size = bounds.GetSize();
                    const float pad = std::max( std::max( size.x, std::max( size.y, size.z ) ) * 0.01f, 0.01f );
                    bounds = AABB( bounds.Min - Vector3( pad, pad, pad ), bounds.Max + Vector3( pad, pad, pad ) );
                    // Bright where visible, faint where hidden behind geometry.
                    DebugDraw::Box( bounds, color, 0.f, DebugDraw::EditorOnly );
                    DebugDraw::Box( bounds, Vector4( color.x, color.y, color.z, 0.25f ), 0.f, DebugDraw::EditorOnly | DebugDraw::NoDepthTest );
                }
                else
                {
                    DebugDraw::Axes( transform->GetLocalToWorldMatrix(), 0.5f, 0.f, DebugDraw::EditorOnly | DebugDraw::NoDepthTest );
                }
            }
        }


        void DrawComponentGizmos()
        {
            World& world = EditorOps::GetWorld();
            world.ForEachEntity( []( Entity& entity ) {
                Transform* transform = entity.TryGetComponent<Transform>();
                if( !transform || !entity.IsActiveInHierarchy() )
                {
                    return;
                }
                const bool selected = Selection::Get().Contains( entity.GetHandle() );
                if( Camera* camera = entity.TryGetComponent<Camera>() )
                {
                    if( camera != Camera::EditorCamera )
                    {
                        const float aspect = camera->OutputSize.y > 0.f ? camera->OutputSize.x / camera->OutputSize.y : 16.f / 9.f;
                        const float farPlane = selected ? std::min( camera->Far, 50.f ) : 2.f;
                        const glm::mat4 projection = glm::perspectiveLH( glm::radians( camera->GetFOV() ), aspect, std::max( camera->Near, 0.01f ), farPlane );
                        const Vector3 position = transform->GetWorldPosition();
                        const glm::mat4 view = glm::lookAtLH( position.InternalVector, ( position + transform->Front() ).InternalVector, transform->Up().InternalVector );
                        const Vector4 color = selected ? DebugDraw::White : Vector4( 0.8f, 0.8f, 0.8f, 0.5f );
                        DebugDraw::Frustum( Matrix4( projection * view ), color, 0.f, DebugDraw::EditorOnly );
                    }
                }
                ParticleSystem* particles = entity.TryGetComponent<ParticleSystem>();
                if( particles && selected )
                {
                    // Emission shape (the cone and box emit along the entity's up axis).
                    const Vector4 color( 0.55f, 0.85f, 1.f, 0.8f );
                    const Vector3 position = transform->GetWorldPosition();
                    const Vector3 up = transform->Up();
                    switch( particles->Shape )
                    {
                    case ParticleShape::Sphere:
                    case ParticleShape::Hemisphere:
                        DebugDraw::Sphere( position, std::max( particles->ShapeRadius, 0.05f ), color, 0.f, DebugDraw::EditorOnly, 20 );
                        break;
                    case ParticleShape::Cone:
                        DebugDraw::Circle( position, up, std::max( particles->ShapeRadius, 0.05f ), color, 0.f, DebugDraw::EditorOnly, 24 );
                        DebugDraw::Cone( position, up, 1.f, std::max( particles->ConeAngle, 1.f ), color, 0.f, DebugDraw::EditorOnly );
                        break;
                    case ParticleShape::Box:
                        DebugDraw::Box( transform->GetLocalToWorldMatrix(), AABB::FromCenterExtents( Vector3(), particles->BoxSize * 0.5f ), color, 0.f, DebugDraw::EditorOnly );
                        break;
                    case ParticleShape::Point:
                        DebugDraw::Arrow( position, position + up * 0.75f, color, 0.15f, 0.f, DebugDraw::EditorOnly );
                        break;
                    }
                }
                if( Light* light = entity.TryGetComponent<Light>() )
                {
                    const Vector3 position = transform->GetWorldPosition();
                    const Vector4 color( light->Color.x, light->Color.y, light->Color.z, selected ? 1.f : 0.6f );
                    switch( light->LightType )
                    {
                    case Moonlight::LightType::Directional:
                        DebugDraw::Circle( position, transform->Front(), 0.35f, color, 0.f, DebugDraw::EditorOnly, 16 );
                        DebugDraw::Arrow( position, position + transform->Front() * ( selected ? 2.f : 1.f ), color, 0.25f, 0.f, DebugDraw::EditorOnly );
                        break;
                    case Moonlight::LightType::Point:
                        DebugDraw::Sphere( position, selected ? light->Range : 0.25f, color, 0.f, DebugDraw::EditorOnly, selected ? 32 : 12 );
                        break;
                    case Moonlight::LightType::Spot:
                        DebugDraw::Cone( position, transform->Front(), selected ? light->Range : 1.f, light->OuterConeAngle, color, 0.f, DebugDraw::EditorOnly );
                        if( selected )
                        {
                            DebugDraw::Cone( position, transform->Front(), light->Range, light->InnerConeAngle, Vector4( color.x, color.y, color.z, 0.35f ), 0.f, DebugDraw::EditorOnly );
                        }
                        break;
                    }
                }
            } );
        }
    }


    AABB ComputeWorldBounds( Entity& InEntity, bool InRecursive )
    {
        AABB bounds;
        if( Transform* transform = InEntity.TryGetComponent<Transform>() )
        {
            AccumulateBounds( *transform, InRecursive, bounds );
        }
        return bounds;
    }


    AABB ComputeSelectionBounds()
    {
        AABB bounds;
        for( Entity* entity : Selection::Get().GetRootEntities() )
        {
            AABB entityBounds = ComputeWorldBounds( *entity, true );
            if( !entityBounds.IsValid() )
            {
                if( Transform* transform = entity->TryGetComponent<Transform>() )
                {
                    entityBounds = AABB::FromCenterExtents( transform->GetWorldPosition(), Vector3( 0.5f, 0.5f, 0.5f ) );
                }
            }
            bounds.Encapsulate( entityBounds );
        }
        return bounds;
    }


    bool Raycast( const Ray& InRay, RayHit& OutHit )
    {
        bool hit = false;
        OutHit.Distance = FLT_MAX;
        EditorOps::GetWorld().Each<Transform, Mesh>( [&]( Entity& entity, Transform& transform, Mesh& mesh ) {
            if( !mesh.MeshReferece || !mesh.MeshReferece->Bounds.IsValid() )
            {
                return;
            }
            // Test in mesh space against the local bounds (an oriented box in world space).
            const glm::mat4& world = transform.GetLocalToWorldMatrix().GetInternalMatrix();
            const glm::mat4 inverse = glm::inverse( world );
            const Vector3 localOrigin = Transform3( inverse, InRay.Origin, 1.f );
            const Vector3 localDirection = Transform3( inverse, InRay.Direction, 0.f );
            if( localDirection.LengthSquared() <= 0.f )
            {
                return;
            }
            const Ray localRay( localOrigin, localDirection );
            const float localDistance = localRay.Intersect( mesh.MeshReferece->Bounds );
            if( localDistance < 0.f )
            {
                return;
            }
            const Vector3 worldPoint = Transform3( world, localRay.GetPoint( localDistance ), 1.f );
            const float distance = ( worldPoint - InRay.Origin ).Length();
            if( distance < OutHit.Distance )
            {
                OutHit.Distance = distance;
                OutHit.Point = worldPoint;
                OutHit.Entity = entity.GetHandle();
                hit = true;
            }
        } );
        return hit;
    }


    Ray ScreenRay( const Vector2& InPixel, const Vector2& InViewportSize, const Matrix4& InView, const Matrix4& InProjection )
    {
        const float x = ( InPixel.x / std::max( InViewportSize.x, 1.f ) ) * 2.f - 1.f;
        const float y = 1.f - ( InPixel.y / std::max( InViewportSize.y, 1.f ) ) * 2.f;
        const glm::mat4 inverse = glm::inverse( InProjection.GetInternalMatrix() * InView.GetInternalMatrix() );
        // Depth 0.5..1 avoids depending on the projection's near-plane depth convention.
        glm::vec4 nearPoint = inverse * glm::vec4( x, y, 0.5f, 1.f );
        glm::vec4 farPoint = inverse * glm::vec4( x, y, 1.f, 1.f );
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;
        const Vector3 direction( glm::vec3( farPoint - nearPoint ) );
        const bool isOrthographic = std::fabs( InProjection.GetInternalMatrix()[2][3] ) < 1e-6f;
        if( isOrthographic )
        {
            // Parallel rays: start well behind the view plane so nothing in front is missed.
            const Vector3 origin = Vector3( glm::vec3( nearPoint ) ) - direction.Normalized() * 10000.f;
            return Ray( origin, direction );
        }
        const glm::mat4 cameraWorld = glm::inverse( InView.GetInternalMatrix() );
        const Vector3 eye( cameraWorld[3].x, cameraWorld[3].y, cameraWorld[3].z );
        return Ray( eye, direction );
    }


    bool WorldToScreen( const Vector3& InPoint, const Vector2& InViewportSize, const Matrix4& InViewProjection, Vector2& OutPixel )
    {
        const glm::vec4 clip = InViewProjection.GetInternalMatrix() * glm::vec4( InPoint.InternalVector, 1.f );
        if( clip.w <= 0.0001f )
        {
            return false;
        }
        const float x = clip.x / clip.w;
        const float y = clip.y / clip.w;
        OutPixel = Vector2( ( x * 0.5f + 0.5f ) * InViewportSize.x, ( 0.5f - y * 0.5f ) * InViewportSize.y );
        return true;
    }


    void DrawOverlays( const Vector3& InCameraPosition, const OverlaySettings& InSettings )
    {
        if( InSettings.ShowGrid )
        {
            DrawGrid( InCameraPosition );
        }
        if( InSettings.ShowGizmos )
        {
            DrawComponentGizmos();
        }
        if( InSettings.ShowSelection )
        {
            DrawSelection();
        }
    }
}

#endif
