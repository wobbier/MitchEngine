#include "SceneTools.h"

#if USING( ME_EDITOR )

#include "EditorOperations.h"
#include "Selection.h"
#include "Components/Audio/AudioSource.h"
#include "Components/Camera.h"
#include "Components/Graphics/Decal.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Graphics/SpriteRenderer.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"
#include "Components/Effects/ParticleSystem.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/CharacterController2D.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/Colliders2D.h"
#include "Components/Physics/PhysicsJoint.h"
#include "Components/Physics/PhysicsJoint2D.h"
#include "Components/Transform.h"
#include "Debug/DebugDraw.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Graphics/DynamicSky.h"
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
                const AABB bounds = mesh ? mesh->GetWorldBounds( InTransform.GetLocalToWorldMatrix() ) : AABB();
                if( bounds.IsValid() )
                {
                    OutBounds.Encapsulate( bounds );
                }
                if( Model* model = entity->TryGetComponent<Model>(); model && model->IsCompact() )
                {
                    const AABB modelBounds = model->GetWorldBounds();
                    if( modelBounds.IsValid() ) OutBounds.Encapsulate( modelBounds );
                }
                if( ModelNode* node = entity->TryGetComponent<ModelNode>() )
                {
                    Model* model = node->GetModel();
                    const int index = model ? model->FindNode( node->NodePath ) : -1;
                    if( index >= 0 )
                    {
                        const AABB nodeBounds = model->GetWorldBounds( index );
                        if( nodeBounds.IsValid() ) OutBounds.Encapsulate( nodeBounds );
                    }
                }
                if( auto* terrain = entity->TryGetComponent<Terrain>(); terrain && terrain->IsEnabled() )
                    if( auto data = terrain->GetData() ) OutBounds.Encapsulate( data->Bounds.Transformed( InTransform.GetLocalToWorldMatrix() ) );
                // Sprites: the quad they draw (ME-278; the selection box used to skip them).
                if( SpriteRenderer* sprite = entity->TryGetComponent<SpriteRenderer>(); sprite && sprite->IsEnabled() )
                {
                    sprite->Resolve();
                    const AABB spriteBounds = sprite->BuildCommand( InTransform ).Bounds;
                    if( spriteBounds.IsValid() ) OutBounds.Encapsulate( spriteBounds );
                }
                if( Decal* decal = entity->TryGetComponent<Decal>() )
                {
                    OutBounds.Encapsulate( AABB::FromCenterExtents( Vector3(), Vector3( 0.5f, 0.5f, 0.5f ) ).Transformed( Decal::BoxMatrix( InTransform, decal->Size ) ) );
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


        // The grid on a plane (ED-8): 0 XZ (the ground), 1 XY, 2 YZ, InOffset along its normal.
        // InSpacing > 0 fixes the cell size; 0 follows the camera's distance to the plane in powers
        // of ten. Lines fade out with distance; the axes are drawn in their colours.
        void DrawGrid( const Vector3& InCameraPosition, int InPlane, float InSpacing, float InOffset )
        {
            const Vector3 axes[3] = { Vector3( 1.f, 0.f, 0.f ), Vector3( 0.f, 1.f, 0.f ), Vector3( 0.f, 0.f, 1.f ) };
            const Vector4 axisColors[3] = { Vector4( 0.9f, 0.25f, 0.25f, 0.8f ), Vector4( 0.35f, 0.85f, 0.35f, 0.8f ), Vector4( 0.25f, 0.45f, 0.95f, 0.8f ) };
            // (u, v, normal) axis indices per plane.
            const int planes[3][3] = { { 0, 2, 1 }, { 0, 1, 2 }, { 2, 1, 0 } };
            const int* plane = planes[std::clamp( InPlane, 0, 2 )];
            const Vector3 u = axes[plane[0]];
            const Vector3 v = axes[plane[1]];
            const Vector3 normal = axes[plane[2]];
            auto along = []( const Vector3& InPoint, const Vector3& InAxis ) { return InPoint.Dot( InAxis ); };

            const float height = std::max( std::fabs( along( InCameraPosition, normal ) - InOffset ), 0.5f );
            const float spacing = InSpacing > 0.f ? InSpacing : std::pow( 10.f, std::floor( std::log10( height ) ) );
            const int halfCount = 50;
            const float extent = halfCount * spacing;
            const float snap = spacing * 10.f;
            const float centerU = std::round( along( InCameraPosition, u ) / snap ) * snap;
            const float centerV = std::round( along( InCameraPosition, v ) / snap ) * snap;
            const int segments = 12;
            const float segmentLength = ( extent * 2.f ) / segments;
            const Vector3 base = normal * InOffset;
            const Vector3 cameraOnPlane = InCameraPosition - normal * along( InCameraPosition, normal );

            // A line along U at V = InOffsetV, or along V at U = InOffsetU.
            auto drawLine = [&]( bool alongU, float InAt, Vector4 color ) {
                for( int s = 0; s < segments; ++s )
                {
                    const float a = -extent + s * segmentLength;
                    const float b = a + segmentLength;
                    const Vector3 from = base + ( alongU ? u * ( centerU + a ) + v * InAt : u * InAt + v * ( centerV + a ) );
                    const Vector3 to = base + ( alongU ? u * ( centerU + b ) + v * InAt : u * InAt + v * ( centerV + b ) );
                    const Vector3 middle = ( from + to ) * 0.5f;
                    const Vector3 offPlane = middle - normal * along( middle, normal ) - cameraOnPlane;
                    Vector4 faded = color;
                    faded.w *= std::clamp( 1.f - offPlane.Length() / extent, 0.f, 1.f );
                    if( faded.w > 0.01f )
                    {
                        DebugDraw::Line( from, to, faded, 0.f, DebugDraw::EditorOnly );
                    }
                }
            };

            for( int i = -halfCount; i <= halfCount; ++i )
            {
                const float atV = centerV + i * spacing;
                const float atU = centerU + i * spacing;
                const bool majorV = std::fabs( std::fmod( atV, snap ) ) < spacing * 0.01f;
                const bool majorU = std::fabs( std::fmod( atU, snap ) ) < spacing * 0.01f;
                const bool axisV = std::fabs( atV ) < spacing * 0.01f && std::fabs( InOffset ) < 1e-6f;
                const bool axisU = std::fabs( atU ) < spacing * 0.01f && std::fabs( InOffset ) < 1e-6f;
                drawLine( true, atV, axisV ? axisColors[plane[0]] : Vector4( 0.6f, 0.6f, 0.6f, majorV ? 0.45f : 0.18f ) );
                drawLine( false, atU, axisU ? axisColors[plane[1]] : Vector4( 0.6f, 0.6f, 0.6f, majorU ? 0.45f : 0.18f ) );
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


        // Collider shapes, character capsules and joint anchors of a selected entity.
        void DrawPhysicsGizmos( Entity& InEntity, Transform& InTransform )
        {
            const Matrix4 localToWorld = InTransform.GetLocalToWorldMatrix();
            const Vector3 scale = InTransform.GetWorldScale();
            const Vector3 absScale( std::abs( scale.x ), std::abs( scale.y ), std::abs( scale.z ) );
            const Quaternion rotation = InTransform.GetWorldRotation();
            auto colorFor = []( const ColliderSettings& InCollider ) {
                return InCollider.IsTrigger ? Vector4( 0.45f, 0.85f, 1.f, 0.9f ) : Vector4( 0.55f, 1.f, 0.45f, 0.9f );
            };
            if( BoxCollider* box = InEntity.TryGetComponent<BoxCollider>(); box && box->IsEnabled() )
            {
                DebugDraw::Box( localToWorld, AABB::FromCenterExtents( box->Center, box->Size * 0.5f ), colorFor( *box ), 0.f, DebugDraw::EditorOnly );
            }
            if( SphereCollider* sphere = InEntity.TryGetComponent<SphereCollider>(); sphere && sphere->IsEnabled() )
            {
                const float radius = sphere->Radius * std::max( { absScale.x, absScale.y, absScale.z } );
                DebugDraw::Sphere( localToWorld.TransformPoint( sphere->Center ), radius, colorFor( *sphere ), 0.f, DebugDraw::EditorOnly, 24 );
            }
            if( CapsuleCollider* capsule = InEntity.TryGetComponent<CapsuleCollider>(); capsule && capsule->IsEnabled() )
            {
                const int axis = static_cast<int>( capsule->Direction );
                const float axisScale = axis == 0 ? absScale.x : ( axis == 1 ? absScale.y : absScale.z );
                const float radialScale = axis == 0 ? std::max( absScale.y, absScale.z ) : ( axis == 1 ? std::max( absScale.x, absScale.z ) : std::max( absScale.x, absScale.y ) );
                const float radius = capsule->Radius * radialScale;
                const float half = std::max( capsule->Height * axisScale * 0.5f - radius, 0.f );
                const Vector3 direction = rotation * Vector3( axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f );
                const Vector3 center = localToWorld.TransformPoint( capsule->Center );
                DebugDraw::Capsule( center - direction * half, center + direction * half, radius, colorFor( *capsule ), 0.f, DebugDraw::EditorOnly );
            }
            if( MeshCollider* meshCollider = InEntity.TryGetComponent<MeshCollider>(); meshCollider && meshCollider->IsEnabled() )
            {
                AABB bounds;
                if( ColliderUtils::GetMeshBounds( InEntity, bounds ) )
                {
                    DebugDraw::Box( localToWorld, bounds, colorFor( *meshCollider ), 0.f, DebugDraw::EditorOnly );
                }
            }
            if( CharacterController* character = InEntity.TryGetComponent<CharacterController>(); character && character->IsEnabled() )
            {
                const float half = std::max( character->Height * 0.5f - character->Radius, 0.f );
                const Vector3 center = InTransform.GetWorldPosition() - character->Center;
                DebugDraw::Capsule( center - Vector3( 0.f, half, 0.f ), center + Vector3( 0.f, half, 0.f ), character->Radius, Vector4( 0.55f, 1.f, 0.45f, 0.9f ), 0.f, DebugDraw::EditorOnly );
            }
            if( PhysicsJoint* joint = InEntity.TryGetComponent<PhysicsJoint>(); joint && joint->IsEnabled() )
            {
                const Vector4 color( 1.f, 0.85f, 0.3f, 1.f );
                const Vector3 anchor = localToWorld.TransformPoint( joint->Anchor );
                DebugDraw::Sphere( anchor, 0.08f, color, 0.f, DebugDraw::EditorOnly | DebugDraw::NoDepthTest, 12 );
                if( joint->Type == JointType::Hinge || joint->Type == JointType::Slider )
                {
                    Vector3 axis = rotation * joint->Axis;
                    axis = axis.LengthSquared() > 1e-8f ? axis.Normalized() : Vector3( 0.f, 1.f, 0.f );
                    DebugDraw::Arrow( anchor - axis * 0.5f, anchor + axis * 0.5f, color, 0.1f, 0.f, DebugDraw::EditorOnly | DebugDraw::NoDepthTest );
                }
                if( joint->Type == JointType::Distance )
                {
                    const Vector3 otherEnd = joint->ConnectedBody && joint->ConnectedBody->HasComponent<Transform>()
                        ? joint->ConnectedBody->GetComponent<Transform>().GetLocalToWorldMatrix().TransformPoint( joint->ConnectedAnchor )
                        : joint->ConnectedAnchor;
                    DebugDraw::Line( anchor, otherEnd, color, 0.f, DebugDraw::EditorOnly );
                }
                else if( joint->ConnectedBody && joint->ConnectedBody->HasComponent<Transform>() )
                {
                    DebugDraw::Line( anchor, joint->ConnectedBody->GetComponent<Transform>().GetWorldPosition(), Vector4( color.x, color.y, color.z, 0.4f ), 0.f, DebugDraw::EditorOnly );
                }
            }
        }


        // 2D colliders live in the entity's local XY plane.
        void DrawPhysics2DGizmos( Entity& InEntity, Transform& InTransform )
        {
            const Matrix4 localToWorld = InTransform.GetLocalToWorldMatrix();
            const Vector3 scale = InTransform.GetWorldScale();
            const Vector2 absScale( std::abs( scale.x ), std::abs( scale.y ) );
            const Vector3 normal = InTransform.GetWorldRotation() * Vector3( 0.f, 0.f, 1.f );
            auto point = [&localToWorld]( const Vector2& InLocal ) { return localToWorld.TransformPoint( Vector3( InLocal.x, InLocal.y, 0.f ) ); };
            auto colorFor = []( const Collider2DSettings& InCollider ) {
                return InCollider.IsTrigger ? Vector4( 0.45f, 0.85f, 1.f, 0.9f ) : Vector4( 0.55f, 1.f, 0.45f, 0.9f );
            };
            auto loop = [&point]( const std::vector<Vector2>& InPoints, bool InClosed, const Vector4& InColor ) {
                for( size_t i = 0; i + 1 < InPoints.size() || ( InClosed && i < InPoints.size() && InPoints.size() > 2 ); ++i )
                {
                    DebugDraw::Line( point( InPoints[i] ), point( InPoints[( i + 1 ) % InPoints.size()] ), InColor, 0.f, DebugDraw::EditorOnly );
                }
            };
            // A capsule from world-space cap centres, flat in the plane with the given normal.
            auto capsule = []( const Vector3& InA, const Vector3& InB, float InRadius, const Vector3& InNormal, const Vector4& InColor ) {
                DebugDraw::Circle( InA, InNormal, InRadius, InColor, 0.f, DebugDraw::EditorOnly, 24 );
                DebugDraw::Circle( InB, InNormal, InRadius, InColor, 0.f, DebugDraw::EditorOnly, 24 );
                Vector3 along = InB - InA;
                if( along.LengthSquared() > 1e-8f )
                {
                    const Vector3 side = along.Cross( InNormal ).Normalized() * InRadius;
                    DebugDraw::Line( InA + side, InB + side, InColor, 0.f, DebugDraw::EditorOnly );
                    DebugDraw::Line( InA - side, InB - side, InColor, 0.f, DebugDraw::EditorOnly );
                }
            };
            if( BoxCollider2D* box = InEntity.TryGetComponent<BoxCollider2D>(); box && box->IsEnabled() )
            {
                const Vector2 h = box->Size * 0.5f;
                const Vector2 o = box->Offset;
                loop( { o + Vector2( -h.x, -h.y ), o + Vector2( h.x, -h.y ), o + Vector2( h.x, h.y ), o + Vector2( -h.x, h.y ) }, true, colorFor( *box ) );
            }
            if( CircleCollider2D* circle = InEntity.TryGetComponent<CircleCollider2D>(); circle && circle->IsEnabled() )
            {
                DebugDraw::Circle( point( circle->Offset ), normal, circle->Radius * std::max( absScale.x, absScale.y ), colorFor( *circle ), 0.f, DebugDraw::EditorOnly, 32 );
            }
            if( CapsuleCollider2D* shape = InEntity.TryGetComponent<CapsuleCollider2D>(); shape && shape->IsEnabled() )
            {
                const bool vertical = shape->Direction == CapsuleDirection2D::Vertical;
                const float radius = shape->Radius * ( vertical ? absScale.x : absScale.y );
                const float axisScale = vertical ? absScale.y : absScale.x;
                const float half = axisScale > 1e-6f ? std::max( shape->Height * axisScale * 0.5f - radius, 0.f ) / axisScale : 0.f;
                const Vector2 along = vertical ? Vector2( 0.f, half ) : Vector2( half, 0.f );
                capsule( point( shape->Offset - along ), point( shape->Offset + along ), radius, normal, colorFor( *shape ) );
            }
            if( PolygonCollider2D* polygon = InEntity.TryGetComponent<PolygonCollider2D>(); polygon && polygon->IsEnabled() )
            {
                loop( Collider2DUtils::ConvexHull( polygon->Points, 8 ), true, colorFor( *polygon ) );
            }
            if( EdgeCollider2D* edge = InEntity.TryGetComponent<EdgeCollider2D>(); edge && edge->IsEnabled() )
            {
                loop( edge->Points, edge->Loop, colorFor( *edge ) );
            }
            if( CharacterController2D* character = InEntity.TryGetComponent<CharacterController2D>(); character && character->IsEnabled() )
            {
                const float half = std::max( character->Height * 0.5f - character->Radius, 0.f );
                const Vector3 center = InTransform.GetWorldPosition() - Vector3( character->Offset.x, character->Offset.y, 0.f );
                capsule( center - Vector3( 0.f, half, 0.f ), center + Vector3( 0.f, half, 0.f ), character->Radius, Vector3( 0.f, 0.f, 1.f ), Vector4( 0.55f, 1.f, 0.45f, 0.9f ) );
            }
            if( PhysicsJoint2D* joint = InEntity.TryGetComponent<PhysicsJoint2D>(); joint && joint->IsEnabled() )
            {
                const Vector4 color( 1.f, 0.85f, 0.3f, 1.f );
                const Vector3 anchor = point( joint->Anchor );
                DebugDraw::Circle( anchor, normal, 0.08f, color, 0.f, DebugDraw::EditorOnly | DebugDraw::NoDepthTest, 12 );
                if( joint->Type == JointType2D::Slider || joint->Type == JointType2D::Wheel )
                {
                    Vector3 axis = InTransform.GetWorldRotation() * Vector3( joint->Axis.x, joint->Axis.y, 0.f );
                    axis = axis.LengthSquared() > 1e-8f ? axis.Normalized() : Vector3( 0.f, 1.f, 0.f );
                    DebugDraw::Arrow( anchor - axis * 0.5f, anchor + axis * 0.5f, color, 0.1f, 0.f, DebugDraw::EditorOnly | DebugDraw::NoDepthTest );
                }
                Transform* other = joint->ConnectedBody ? joint->ConnectedBody->TryGetComponent<Transform>() : nullptr;
                if( joint->Type == JointType2D::Distance )
                {
                    const Vector3 otherEnd = other ? other->GetLocalToWorldMatrix().TransformPoint( Vector3( joint->ConnectedAnchor.x, joint->ConnectedAnchor.y, 0.f ) )
                                                   : Vector3( joint->ConnectedAnchor.x, joint->ConnectedAnchor.y, anchor.z );
                    DebugDraw::Line( anchor, otherEnd, color, 0.f, DebugDraw::EditorOnly );
                }
                else if( other )
                {
                    DebugDraw::Line( anchor, other->GetWorldPosition(), Vector4( color.x, color.y, color.z, 0.4f ), 0.f, DebugDraw::EditorOnly );
                }
            }
        }


        void DrawComponentGizmos()
        {
            World& world = EditorOps::GetWorld();
            const auto& camera = GetEngine().EditorCamera;
            const bool procedural = camera.ClearType == Moonlight::ClearColorType::Procedural;
            const auto bodies = Moonlight::ComputeSkyCelestial( camera.ProceduralSky );
            world.ForEachEntity( [&]( Entity& entity ) {
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
                if( selected )
                {
                    DrawPhysicsGizmos( entity, *transform );
                    DrawPhysics2DGizmos( entity, *transform );
                }
                AudioSource* audio = entity.TryGetComponent<AudioSource>();
                if( audio && selected && audio->SpatialBlend > 0.f )
                {
                    // Full volume inside the inner sphere; the outer one is where rolloff ends.
                    const Vector3 position = transform->GetWorldPosition();
                    DebugDraw::Sphere( position, std::max( audio->MinDistance, 0.01f ), Vector4( 0.45f, 0.8f, 1.f, 0.9f ), 0.f, DebugDraw::EditorOnly, 24 );
                    DebugDraw::Sphere( position, std::max( audio->MaxDistance, audio->MinDistance ), Vector4( 0.45f, 0.8f, 1.f, 0.35f ), 0.f, DebugDraw::EditorOnly, 32 );
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
                    {
                        auto drawDirection = [&]( const Vector3& direction ) {
                            DebugDraw::Circle( position, direction, 0.35f, color, 0.f, DebugDraw::EditorOnly, 16 );
                            DebugDraw::Arrow( position, position + direction * ( selected ? 2.f : 1.f ), color, 0.25f, 0.f, DebugDraw::EditorOnly );
                        };
                        if( procedural )
                        {
                            // Automatic represents both celestial sources, never the entity rotation.
                            if( light->DirectionalSource != Moonlight::DirectionalSource::Moon ) drawDirection( Vector3( -bodies.Sun.x, -bodies.Sun.y, -bodies.Sun.z ) );
                            if( light->DirectionalSource != Moonlight::DirectionalSource::Sun ) drawDirection( Vector3( -bodies.Moon.x, -bodies.Moon.y, -bodies.Moon.z ) );
                        }
                        else drawDirection( transform->Front() );
                        break;
                    }
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
                    case Moonlight::LightType::Bar:
                    {
                        const Moonlight::LightCommand command = light->BuildCommand( *transform );
                        const Vector3 offset = command.AxisX * command.HalfSize.x;
                        DebugDraw::Capsule( position - offset, position + offset, command.HalfSize.y, color, 0.f, DebugDraw::EditorOnly );
                        if( selected )
                        {
                            DebugDraw::Capsule( position - offset, position + offset, command.HalfSize.y + command.Range, Vector4( color.x, color.y, color.z, 0.35f ), 0.f, DebugDraw::EditorOnly );
                        }
                        break;
                    }
                    case Moonlight::LightType::Rectangle:
                    {
                        const Moonlight::LightCommand command = light->BuildCommand( *transform );
                        const Vector3 x = command.AxisX * command.HalfSize.x;
                        const Vector3 y = command.AxisY * command.HalfSize.y;
                        const Vector3 corners[4] = { position - x - y, position + x - y, position + x + y, position - x + y };
                        for( int i = 0; i < 4; ++i )
                        {
                            DebugDraw::Line( corners[i], corners[( i + 1 ) % 4], color, 0.f, DebugDraw::EditorOnly );
                        }
                        DebugDraw::Arrow( position, position + command.Direction * std::min( command.Range, selected ? 2.f : 1.f ), color, 0.2f, 0.f, DebugDraw::EditorOnly );
                        if( selected )
                        {
                            // The beam: each corner's edge leaves the face tilted outwards by the outer
                            // cone angle (the cone is measured from the closest point on the face), with
                            // the inner angle's full-intensity core drawn fainter inside it.
                            auto drawBeam = [&]( float angleDegrees, const Vector4& beamColor ) {
                                const float tangent = std::tan( glm::radians( std::clamp( angleDegrees, 0.f, 89.f ) ) );
                                const float length = std::min( command.Range, 40.f );
                                Vector3 far[4];
                                for( int i = 0; i < 4; ++i )
                                {
                                    const Vector3 outward = ( corners[i] - position ).Normalized();
                                    far[i] = corners[i] + ( command.Direction + outward * tangent ) * length;
                                    DebugDraw::Line( corners[i], far[i], beamColor, 0.f, DebugDraw::EditorOnly );
                                }
                                for( int i = 0; i < 4; ++i )
                                {
                                    DebugDraw::Line( far[i], far[( i + 1 ) % 4], beamColor, 0.f, DebugDraw::EditorOnly );
                                }
                            };
                            drawBeam( light->OuterConeAngle, color );
                            drawBeam( light->InnerConeAngle, Vector4( color.x, color.y, color.z, 0.35f ) );
                            glm::mat4 axes( 1.f );
                            axes[0] = glm::vec4( command.AxisX.InternalVector, 0.f );
                            axes[1] = glm::vec4( command.AxisY.InternalVector, 0.f );
                            axes[2] = glm::vec4( command.AxisZ.InternalVector, 0.f );
                            axes[3] = glm::vec4( ( position + command.Direction * ( command.Range * 0.5f ) ).InternalVector, 1.f );
                            // Encloses the rounded fade volume, entirely in front of the face.
                            const Vector3 extents( command.HalfSize.x + command.Range, command.HalfSize.y + command.Range, command.Range * 0.5f );
                            DebugDraw::Box( Matrix4( axes ), AABB::FromCenterExtents( Vector3(), extents ), Vector4( color.x, color.y, color.z, 0.35f ), 0.f, DebugDraw::EditorOnly );
                        }
                        break;
                    }
                    case Moonlight::LightType::Box:
                    {
                        const Moonlight::LightCommand command = light->BuildCommand( *transform );
                        glm::mat4 axes( 1.f );
                        axes[0] = glm::vec4( command.AxisX.InternalVector, 0.f );
                        axes[1] = glm::vec4( command.AxisY.InternalVector, 0.f );
                        axes[2] = glm::vec4( command.AxisZ.InternalVector, 0.f );
                        axes[3] = glm::vec4( position.InternalVector, 1.f );
                        const Matrix4 world( axes );
                        DebugDraw::Box( world, AABB::FromCenterExtents( Vector3(), command.HalfSize ), color, 0.f, DebugDraw::EditorOnly );
                        if( selected )
                        {
                            // Encloses the rounded fade volume; the inner box is the emitter itself.
                            const Vector3 range( command.Range, command.Range, command.Range );
                            DebugDraw::Box( world, AABB::FromCenterExtents( Vector3(), command.HalfSize + range ), Vector4( color.x, color.y, color.z, 0.35f ), 0.f, DebugDraw::EditorOnly );
                        }
                        break;
                    }
                    }
                }
                if( Decal* decal = entity.TryGetComponent<Decal>() )
                {
                    // The projection box, the direction it projects in and (selected) the texture's up.
                    const Matrix4 box = Decal::BoxMatrix( *transform, decal->Size );
                    const glm::mat4& matrix = box.GetInternalMatrix();
                    const Vector4 color = selected ? Vector4( 0.95f, 0.7f, 1.f, 1.f ) : Vector4( 0.85f, 0.6f, 1.f, 0.45f );
                    DebugDraw::Box( box, AABB::FromCenterExtents( Vector3(), Vector3( 0.5f, 0.5f, 0.5f ) ), color, 0.f, DebugDraw::EditorOnly );
                    const Vector3 back = Transform3( matrix, Vector3( 0.f, 0.f, -0.5f ), 1.f );
                    const Vector3 front = Transform3( matrix, Vector3( 0.f, 0.f, 0.5f ), 1.f );
                    DebugDraw::Arrow( back, front, color, std::min( ( front - back ).Length() * 0.25f, 0.25f ), 0.f, DebugDraw::EditorOnly );
                    if( selected )
                    {
                        const Vector3 center = Transform3( matrix, Vector3(), 1.f );
                        DebugDraw::Line( center, Transform3( matrix, Vector3( 0.f, 0.5f, 0.f ), 1.f ), Vector4( 0.45f, 1.f, 0.45f, 1.f ), 0.f, DebugDraw::EditorOnly | DebugDraw::NoDepthTest );
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


    bool Raycast( const Ray& InRay, RayHit& OutHit, const std::function<bool( Entity& )>& InIgnore )
    {
        bool hit = false;
        OutHit.Distance = FLT_MAX;
        EditorOps::GetWorld().Each<Transform, Mesh>( [&]( Entity& entity, Transform& transform, Mesh& mesh ) {
            if( !mesh.MeshReferece || !mesh.MeshReferece->Bounds.IsValid() || ( InIgnore && InIgnore( entity ) ) )
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
        EditorOps::GetWorld().Each<Model>( [&]( Entity& modelEntity, Model& model ) {
            if( !model.IsCompact() || ( InIgnore && InIgnore( modelEntity ) ) ) return;
            for( const auto& part : model.GetRenderParts() )
            {
                if( model.IsPartExternal( part ) ) continue;
                const AABB bounds = part.Renderer->GetWorldBounds( model.GetPartWorld( part ) );
                if( !bounds.IsValid() ) continue;
                const float distance = InRay.Intersect( bounds );
                EntityHandle owner = model.GetNodeEntity( part.NodeIndex );
                if( distance >= 0.f && distance < OutHit.Distance && owner->IsActiveInHierarchy() )
                {
                    OutHit.Distance = distance; OutHit.Point = InRay.GetPoint( distance ); OutHit.Entity = owner; hit = true;
                }
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
            DrawGrid( InCameraPosition, InSettings.GridPlane, InSettings.GridSpacing, InSettings.GridOffset );
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
