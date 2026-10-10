#include "PCH.h"
#include "Physics2DCore.h"
#include "Components/Physics/CharacterController2D.h"
#include "Components/Physics/Colliders2D.h"
#include "Components/Physics/PhysicsJoint2D.h"
#include "Components/Physics/Rigidbody2D.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Debug/DebugDraw.h"
#include "ECS/ComponentFilter.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Physics/Box2DUtils.h"
#include "Physics/PhysicsHash.h"
#include "CLog.h"
#include "optick.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

using namespace Box2DUtils;
using namespace PhysicsHash;

namespace
{
    constexpr int kSubSteps = 4;
    constexpr float kPi = 3.14159265358979f;
    constexpr float kToRadians = kPi / 180.f;
    // Shape masks carry these high bits so a character's mover queries can exclude its own capsule
    // (each character's query category is one of them; its own capsule's mask lacks that bit).
    constexpr uint64_t kCharacterBits = 0xFFFFFFFF00000000ull;

    b2WorldId WorldId( uint32_t InValue )
    {
        b2WorldId id;
        std::memcpy( &id, &InValue, sizeof( id ) );
        return id;
    }

    uint32_t PackWorld( b2WorldId InId )
    {
        uint32_t value = 0;
        std::memcpy( &value, &InId, sizeof( InId ) );
        return value;
    }

    Vector2 Abs( const Vector2& v )
    {
        return Vector2( std::abs( v.x ), std::abs( v.y ) );
    }

    float LengthSquared( const Vector2& v )
    {
        return v.x * v.x + v.y * v.y;
    }

    bool SamePose( const Vector2& InPositionA, float InAngleA, const Vector2& InPositionB, float InAngleB )
    {
        constexpr float kEpsilon = 1e-5f;
        return std::abs( InPositionA.x - InPositionB.x ) < kEpsilon && std::abs( InPositionA.y - InPositionB.y ) < kEpsilon
            && std::abs( b2UnwindAngle( InAngleA - InAngleB ) ) < kEpsilon;
    }

    void* UserDataFor( Entity& InEntity )
    {
        return reinterpret_cast<void*>( static_cast<uintptr_t>( InEntity.GetId().Value() ) );
    }

    EntityHandle EntityFromUserData( World& InWorld, void* InUserData )
    {
        return InUserData ? InWorld.FindEntityByIDValue( static_cast<uint64_t>( reinterpret_cast<uintptr_t>( InUserData ) ) ) : EntityHandle();
    }

    // The collider's entity (segment shapes too), falling back to the body's owner.
    EntityHandle EntityOfShape( World& InWorld, b2ShapeId InShape )
    {
        if( !b2Shape_IsValid( InShape ) )
        {
            return EntityHandle();
        }
        if( void* data = b2Shape_GetUserData( InShape ) )
        {
            return EntityFromUserData( InWorld, data );
        }
        return EntityFromUserData( InWorld, b2Body_GetUserData( b2Shape_GetBody( InShape ) ) );
    }

    b2Filter FilterFor( Entity& InEntity )
    {
        b2Filter filter = b2DefaultFilter();
        filter.categoryBits = PhysicsLayers::Bit( InEntity.GetLayer() );
        filter.maskBits = ProjectSettings::Get().GetCollisionMask( InEntity.GetLayer() ) | kCharacterBits;
        return filter;
    }

    b2QueryFilter QueryFilterFor( uint32_t InLayerMask )
    {
        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.categoryBits = ~0ull;
        filter.maskBits = InLayerMask;
        return filter;
    }

    template<typename T>
    T* Enabled( Entity& InEntity )
    {
        T* component = InEntity.TryGetComponent<T>();
        return component && component->IsEnabled() ? component : nullptr;
    }

    bool HasAnyCollider( Entity& InEntity )
    {
        return Enabled<BoxCollider2D>( InEntity ) || Enabled<CircleCollider2D>( InEntity ) || Enabled<CapsuleCollider2D>( InEntity )
            || Enabled<PolygonCollider2D>( InEntity ) || Enabled<EdgeCollider2D>( InEntity );
    }

    // Pose of a collider entity relative to its body owner, in the owner's (unscaled) XY frame.
    void RelativePose( Transform& InCollider, Transform& InOwner, Vector2& OutPosition, float& OutAngle )
    {
        Vector3 position;
        Quaternion rotation;
        Transform* node = &InCollider;
        while( node && node != &InOwner )
        {
            const Vector3 scale = node->GetScale();
            position = node->GetPosition() + node->GetRotation() * Vector3( scale.x * position.x, scale.y * position.y, scale.z * position.z );
            rotation = node->GetRotation() * rotation;
            node = node->GetParentTransform();
        }
        const Vector3 ownerScale = InOwner.GetWorldScale();
        OutPosition = Vector2( position.x * ownerScale.x, position.y * ownerScale.y );
        OutAngle = AngleOf( rotation );
    }

    Vector4 ToColor( b2HexColor InColor )
    {
        const uint32_t c = static_cast<uint32_t>( InColor );
        return Vector4( ( ( c >> 16 ) & 0xFF ) / 255.f, ( ( c >> 8 ) & 0xFF ) / 255.f, ( c & 0xFF ) / 255.f, 1.f );
    }

    Vector3 At( b2Vec2 InPoint )
    {
        return Vector3( InPoint.x, InPoint.y, 0.f );
    }

    // Box2D debug drawing, flattened onto z = 0.
    b2DebugDraw MakeDebugDraw()
    {
        b2DebugDraw draw = b2DefaultDebugDraw();
        draw.DrawPolygonFcn = []( const b2Vec2* vertices, int count, b2HexColor color, void* ) {
            for( int i = 0; i < count; ++i )
            {
                DebugDraw::Line( At( vertices[i] ), At( vertices[( i + 1 ) % count] ), ToColor( color ) );
            }
        };
        draw.DrawSolidPolygonFcn = []( b2Transform transform, const b2Vec2* vertices, int count, float, b2HexColor color, void* ) {
            for( int i = 0; i < count; ++i )
            {
                DebugDraw::Line( At( b2TransformPoint( transform, vertices[i] ) ), At( b2TransformPoint( transform, vertices[( i + 1 ) % count] ) ), ToColor( color ) );
            }
        };
        draw.DrawCircleFcn = []( b2Vec2 center, float radius, b2HexColor color, void* ) {
            DebugDraw::Circle( At( center ), Vector3( 0.f, 0.f, 1.f ), radius, ToColor( color ), 0.f, DebugDraw::None, 24 );
        };
        draw.DrawSolidCircleFcn = []( b2Transform transform, float radius, b2HexColor color, void* ) {
            DebugDraw::Circle( At( transform.p ), Vector3( 0.f, 0.f, 1.f ), radius, ToColor( color ), 0.f, DebugDraw::None, 24 );
            DebugDraw::Line( At( transform.p ), At( b2TransformPoint( transform, b2Vec2{ radius, 0.f } ) ), ToColor( color ) );
        };
        draw.DrawSolidCapsuleFcn = []( b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void* ) {
            const Vector4 c = ToColor( color );
            DebugDraw::Circle( At( p1 ), Vector3( 0.f, 0.f, 1.f ), radius, c, 0.f, DebugDraw::None, 20 );
            DebugDraw::Circle( At( p2 ), Vector3( 0.f, 0.f, 1.f ), radius, c, 0.f, DebugDraw::None, 20 );
            const b2Vec2 axis = b2Normalize( b2Sub( p2, p1 ) );
            const b2Vec2 side{ -axis.y * radius, axis.x * radius };
            DebugDraw::Line( At( b2Add( p1, side ) ), At( b2Add( p2, side ) ), c );
            DebugDraw::Line( At( b2Sub( p1, side ) ), At( b2Sub( p2, side ) ), c );
        };
        draw.DrawSegmentFcn = []( b2Vec2 p1, b2Vec2 p2, b2HexColor color, void* ) {
            DebugDraw::Line( At( p1 ), At( p2 ), ToColor( color ) );
        };
        draw.DrawTransformFcn = []( b2Transform transform, void* ) {
            DebugDraw::Line( At( transform.p ), At( b2TransformPoint( transform, b2Vec2{ 0.3f, 0.f } ) ), DebugDraw::Red );
            DebugDraw::Line( At( transform.p ), At( b2TransformPoint( transform, b2Vec2{ 0.f, 0.3f } ) ), DebugDraw::Green );
        };
        draw.DrawPointFcn = []( b2Vec2 point, float size, b2HexColor color, void* ) {
            DebugDraw::Circle( At( point ), Vector3( 0.f, 0.f, 1.f ), std::max( size * 0.005f, 0.02f ), ToColor( color ), 0.f, DebugDraw::NoDepthTest, 8 );
        };
        draw.DrawStringFcn = []( b2Vec2, const char*, b2HexColor, void* ) {};
        draw.useDrawingBounds = false;
        draw.drawShapes = true;
        draw.drawJoints = true;
        return draw;
    }
}


Physics2DCore::Physics2DCore()
    : Base( ComponentFilter().Requires<Transform>().RequiresOneOf<Rigidbody2D>().RequiresOneOf<BoxCollider2D>().RequiresOneOf<CircleCollider2D>()
        .RequiresOneOf<CapsuleCollider2D>().RequiresOneOf<PolygonCollider2D>().RequiresOneOf<EdgeCollider2D>().RequiresOneOf<CharacterController2D>()
        .RequiresOneOf<PhysicsJoint2D>() )
{
    SetIsSerializable( false );
}


Physics2DCore::~Physics2DCore()
{
    DestroyWorld();
}


void Physics2DCore::Init()
{
    DestroyWorld();
    CreateWorld();
}


void Physics2DCore::OnRemovedFromWorld()
{
    DestroyWorld();
}


void Physics2DCore::OnStart()
{
    m_simulating = true;
    SetGravity( Vector2( ProjectSettings::Get().Gravity.x, ProjectSettings::Get().Gravity.y ) );
    SyncBodies();
    SyncJoints();
    for( auto& [id, record] : m_bodies )
    {
        record.PreviousPosition = record.CurrentPosition = record.AppliedPosition;
        record.PreviousAngle = record.CurrentAngle = record.AppliedAngle;
    }
}


void Physics2DCore::OnStop()
{
    m_simulating = false;
}


void Physics2DCore::CreateWorld()
{
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = ToB2( ProjectSettings::Get().Gravity );
    def.workerCount = static_cast<int>( std::max<uint32_t>( 1, Jobs::JobSystem::Get().GetThreadCount() ) );
    def.enqueueTask = reinterpret_cast<b2EnqueueTaskCallback*>( &Physics2DCore::EnqueueTask );
    def.finishTask = &Physics2DCore::FinishTask;
    def.userTaskContext = this;
    m_world = PackWorld( b2CreateWorld( &def ) );
}


void Physics2DCore::DestroyWorld()
{
    if( m_world != 0 && b2World_IsValid( WorldId( m_world ) ) )
    {
        b2DestroyWorld( WorldId( m_world ) );
    }
    m_world = 0;
    m_groundBody = 0;
    // Components keep stale ids, which Box2D rejects (the world generation is part of every id).
    m_bodies.clear();
}


void* Physics2DCore::EnqueueTask( void* InTask, int InItemCount, int InMinRange, void* InTaskContext, void* InUserContext )
{
    Physics2DCore* self = static_cast<Physics2DCore*>( InUserContext );
    b2TaskCallback* task = reinterpret_cast<b2TaskCallback*>( InTask );
    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    const int threads = static_cast<int>( jobs.GetThreadCount() );
    const int ranges = std::clamp( InItemCount / std::max( InMinRange, 1 ), 1, threads );
    if( ranges <= 1 || self->m_taskCount >= static_cast<int>( self->m_tasks.size() ) )
    {
        task( 0, InItemCount, Jobs::JobSystem::GetCurrentThreadIndex(), InTaskContext );
        return nullptr;
    }
    PhysicsTask& slot = self->m_tasks[self->m_taskCount++];
    slot.Task = InTask;
    slot.Context = InTaskContext;
    const int perRange = ( InItemCount + ranges - 1 ) / ranges;
    for( int begin = 0; begin < InItemCount; begin += perRange )
    {
        const int end = std::min( begin + perRange, InItemCount );
        jobs.Submit( slot.Counter, []( void* context, uint32_t rangeBegin, uint32_t rangeEnd ) {
            PhysicsTask* job = static_cast<PhysicsTask*>( context );
            // Worker indices are 0 (the stepping thread) .. thread count - 1, as Box2D expects.
            reinterpret_cast<b2TaskCallback*>( job->Task )( static_cast<int>( rangeBegin ), static_cast<int>( rangeEnd ), Jobs::JobSystem::GetCurrentThreadIndex(), job->Context );
        }, &slot, static_cast<uint32_t>( begin ), static_cast<uint32_t>( end ) );
    }
    return &slot;
}


void Physics2DCore::FinishTask( void* InUserTask, void* )
{
    if( InUserTask )
    {
        Jobs::JobSystem::Get().Wait( static_cast<PhysicsTask*>( InUserTask )->Counter );
    }
}


uint64_t Physics2DCore::ComputeShapeSignature( Entity& InOwner, const std::vector<EntityHandle>& InColliders ) const
{
    uint64_t h = 0x2d5ba9eULL;
    Transform& owner = InOwner.GetComponent<Transform>();
    h = MixVector( h, owner.GetWorldScale(), true );
    for( const EntityHandle& handle : InColliders )
    {
        Entity& entity = *handle.Get();
        Transform& transform = entity.GetComponent<Transform>();
        h = Mix( h, entity.GetId().Value() );
        h = Mix( Mix( h, entity.GetLayer() ), ProjectSettings::Get().GetCollisionMask( entity.GetLayer() ) );
        h = MixVector( h, transform.GetWorldScale(), true );
        if( &transform != &owner )
        {
            Vector2 position;
            float angle = 0.f;
            RelativePose( transform, owner, position, angle );
            h = Mix( MixVector( h, position, true ), Quantized( angle ) );
        }
        if( BoxCollider2D* box = Enabled<BoxCollider2D>( entity ) )
        {
            h = Mix( MixVector( Mix( Mix( h, 1 ), box->SettingsHash() ), box->Size ), Bits( box->EdgeRadius ) );
        }
        if( CircleCollider2D* circle = Enabled<CircleCollider2D>( entity ) )
        {
            h = Mix( Mix( Mix( h, 2 ), circle->SettingsHash() ), Bits( circle->Radius ) );
        }
        if( CapsuleCollider2D* capsule = Enabled<CapsuleCollider2D>( entity ) )
        {
            h = Mix( Mix( Mix( Mix( Mix( h, 3 ), capsule->SettingsHash() ), Bits( capsule->Radius ) ), Bits( capsule->Height ) ), static_cast<uint64_t>( capsule->Direction ) );
        }
        if( PolygonCollider2D* polygon = Enabled<PolygonCollider2D>( entity ) )
        {
            h = Mix( Mix( Mix( h, 4 ), polygon->SettingsHash() ), Bits( polygon->EdgeRadius ) );
            for( const Vector2& point : polygon->Points )
            {
                h = MixVector( h, point );
            }
        }
        if( EdgeCollider2D* edge = Enabled<EdgeCollider2D>( entity ) )
        {
            h = Mix( Mix( Mix( h, 5 ), edge->SettingsHash() ), edge->Loop ? 1 : 0 );
            for( const Vector2& point : edge->Points )
            {
                h = MixVector( h, point );
            }
        }
    }
    return h;
}


uint64_t Physics2DCore::EnsureGroundBody()
{
    if( !IsBody( m_groundBody ) )
    {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = b2_staticBody;
        m_groundBody = Pack( b2CreateBody( WorldId( m_world ), &def ) );
    }
    return m_groundBody;
}


void Physics2DCore::BuildShapes( BodyRecord& InRecord, const std::vector<EntityHandle>& InColliders )
{
    const b2BodyId body = Body( InRecord.Body );
    for( uint64_t shape : InRecord.Shapes )
    {
        const b2ShapeId id = Unpack<b2ShapeId>( shape );
        if( b2Shape_IsValid( id ) )
        {
            b2DestroyShape( id, false );
        }
    }
    InRecord.Shapes.clear();

    Transform& owner = InRecord.Owner->GetComponent<Transform>();
    for( const EntityHandle& handle : InColliders )
    {
        Entity& entity = *handle.Get();
        Transform& transform = entity.GetComponent<Transform>();
        Vector2 offset;
        float angle = 0.f;
        if( &transform != &owner )
        {
            RelativePose( transform, owner, offset, angle );
        }
        const b2Rot frameRotation = b2MakeRot( angle );
        const Vector3 worldScale = transform.GetWorldScale();
        const Vector2 scale( worldScale.x, worldScale.y );
        const Vector2 absScale = Abs( scale );
        // A local XY point of the collider entity, in the body's frame.
        auto toBody = [&]( const Vector2& InPoint ) {
            return b2Add( ToB2( offset ), b2RotateVector( frameRotation, b2Vec2{ InPoint.x * scale.x, InPoint.y * scale.y } ) );
        };
        auto makeDef = [&entity]( const Collider2DSettings& InSettings ) {
            b2ShapeDef def = b2DefaultShapeDef();
            def.userData = UserDataFor( entity );
            def.density = InSettings.Density;
            def.material.friction = InSettings.Friction;
            def.material.restitution = InSettings.Restitution;
            def.filter = FilterFor( entity );
            def.isSensor = InSettings.IsTrigger;
            def.enableSensorEvents = true;
            def.enableContactEvents = true;
            def.updateBodyMass = false;
            return def;
        };

        if( BoxCollider2D* box = Enabled<BoxCollider2D>( entity ) )
        {
            const b2ShapeDef def = makeDef( *box );
            const float hx = std::max( std::abs( box->Size.x ) * absScale.x * 0.5f, 0.005f );
            const float hy = std::max( std::abs( box->Size.y ) * absScale.y * 0.5f, 0.005f );
            const b2Polygon polygon = b2MakeOffsetRoundedBox( hx, hy, toBody( box->Offset ), frameRotation, std::max( box->EdgeRadius, 0.f ) );
            InRecord.Shapes.push_back( Pack( b2CreatePolygonShape( body, &def, &polygon ) ) );
        }
        if( CircleCollider2D* circle = Enabled<CircleCollider2D>( entity ) )
        {
            const b2ShapeDef def = makeDef( *circle );
            const b2Circle shape{ toBody( circle->Offset ), std::max( circle->Radius * std::max( absScale.x, absScale.y ), 0.005f ) };
            InRecord.Shapes.push_back( Pack( b2CreateCircleShape( body, &def, &shape ) ) );
        }
        if( CapsuleCollider2D* capsule = Enabled<CapsuleCollider2D>( entity ) )
        {
            const b2ShapeDef def = makeDef( *capsule );
            const bool vertical = capsule->Direction == CapsuleDirection2D::Vertical;
            const float radius = std::max( capsule->Radius * ( vertical ? absScale.x : absScale.y ), 0.005f );
            const float half = std::max( capsule->Height * ( vertical ? absScale.y : absScale.x ) * 0.5f - radius, 0.f );
            const b2Vec2 center = toBody( capsule->Offset );
            if( half < 0.01f )
            {
                const b2Circle shape{ center, radius };
                InRecord.Shapes.push_back( Pack( b2CreateCircleShape( body, &def, &shape ) ) );
            }
            else
            {
                const b2Vec2 direction = b2RotateVector( frameRotation, vertical ? b2Vec2{ 0.f, 1.f } : b2Vec2{ 1.f, 0.f } );
                const b2Capsule shape{ b2MulSub( center, half, direction ), b2MulAdd( center, half, direction ), radius };
                InRecord.Shapes.push_back( Pack( b2CreateCapsuleShape( body, &def, &shape ) ) );
            }
        }
        if( PolygonCollider2D* polygon = Enabled<PolygonCollider2D>( entity ) )
        {
            std::vector<Vector2> points;
            points.reserve( polygon->Points.size() );
            for( const Vector2& point : polygon->Points )
            {
                const b2Vec2 p = toBody( point );
                points.push_back( Vector2( p.x, p.y ) );
            }
            const std::vector<Vector2> hullPoints = Collider2DUtils::ConvexHull( points, B2_MAX_POLYGON_VERTICES );
            std::vector<b2Vec2> b2Points;
            for( const Vector2& point : hullPoints )
            {
                b2Points.push_back( ToB2( point ) );
            }
            const b2Hull hull = b2ComputeHull( b2Points.data(), static_cast<int>( b2Points.size() ) );
            if( hull.count >= 3 )
            {
                const b2ShapeDef def = makeDef( *polygon );
                const b2Polygon shape = b2MakePolygon( &hull, std::max( polygon->EdgeRadius, 0.f ) );
                InRecord.Shapes.push_back( Pack( b2CreatePolygonShape( body, &def, &shape ) ) );
            }
            else
            {
                BRUH( "Physics 2D: a PolygonCollider2D needs at least 3 points that aren't in a line" );
            }
        }
        if( EdgeCollider2D* edge = Enabled<EdgeCollider2D>( entity ) )
        {
            const b2ShapeDef def = makeDef( *edge );
            const size_t count = edge->Points.size();
            const size_t segments = count < 2 ? 0 : ( edge->Loop && count > 2 ? count : count - 1 );
            for( size_t i = 0; i < segments; ++i )
            {
                const b2Segment segment{ toBody( edge->Points[i] ), toBody( edge->Points[( i + 1 ) % count] ) };
                if( b2DistanceSquared( segment.point1, segment.point2 ) > 1e-8f )
                {
                    InRecord.Shapes.push_back( Pack( b2CreateSegmentShape( body, &def, &segment ) ) );
                }
            }
        }
    }
}


void Physics2DCore::ApplyBodySettings( BodyRecord& InRecord, Rigidbody2D* InRigidbody )
{
    const b2BodyId body = Body( InRecord.Body );
    if( !InRigidbody )
    {
        b2Body_SetType( body, b2_staticBody );
        return;
    }
    const b2BodyType type = InRigidbody->Type == BodyType::Static ? b2_staticBody : ( InRigidbody->Type == BodyType::Kinematic ? b2_kinematicBody : b2_dynamicBody );
    if( b2Body_GetType( body ) != type )
    {
        b2Body_SetType( body, type );
    }
    b2Body_SetLinearDamping( body, InRigidbody->LinearDamping );
    b2Body_SetAngularDamping( body, InRigidbody->AngularDamping );
    b2Body_SetGravityScale( body, InRigidbody->GravityScale );
    b2Body_SetBullet( body, InRigidbody->ContinuousCollision );
    b2Body_EnableSleep( body, InRigidbody->CanSleep );
    b2Body_SetFixedRotation( body, InRigidbody->FreezeRotation );

    if( type == b2_dynamicBody )
    {
        b2Body_ApplyMassFromShapes( body );
        b2MassData mass = b2Body_GetMassData( body );
        if( InRigidbody->Mass > 0.f )
        {
            if( mass.mass > 0.f )
            {
                // Keep the shapes' mass distribution, scaled to the requested mass.
                mass.rotationalInertia *= InRigidbody->Mass / mass.mass;
            }
            else
            {
                mass.rotationalInertia = 0.125f * InRigidbody->Mass;
            }
            mass.mass = InRigidbody->Mass;
            b2Body_SetMassData( body, mass );
        }
    }
}


void Physics2DCore::DestroyRecord( BodyRecord& InRecord )
{
    if( IsBody( InRecord.Body ) )
    {
        b2DestroyBody( Body( InRecord.Body ) );
    }
    if( InRecord.Owner )
    {
        if( Rigidbody2D* rigidbody = InRecord.Owner->TryGetComponent<Rigidbody2D>() )
        {
            rigidbody->m_body = 0;
        }
    }
    InRecord.Body = 0;
    InRecord.Shapes.clear();
}


void Physics2DCore::SyncNow()
{
    SyncBodies();
}


void Physics2DCore::SyncBodies()
{
    OPTICK_EVENT( "Physics2DCore::SyncBodies" );
    if( m_world == 0 )
    {
        return;
    }

    std::unordered_map<uint64_t, std::vector<EntityHandle>> collidersByOwner;
    std::unordered_map<uint64_t, EntityHandle> owners;
    for( Entity& entity : GetEntities() )
    {
        if( !entity.IsActiveInHierarchy() || Enabled<CharacterController2D>( entity ) )
        {
            continue;
        }
        const bool hasRigidbody = Enabled<Rigidbody2D>( entity ) != nullptr;
        if( hasRigidbody )
        {
            owners[entity.GetId().Value()] = entity.GetHandle();
        }
        if( !HasAnyCollider( entity ) )
        {
            continue;
        }
        Entity* owner = &entity;
        if( !hasRigidbody )
        {
            for( Transform* parent = entity.GetComponent<Transform>().GetParentTransform(); parent; parent = parent->GetParentTransform() )
            {
                if( parent->Parent && parent->Parent->IsActiveInHierarchy() && Enabled<Rigidbody2D>( *parent->Parent.Get() ) )
                {
                    owner = parent->Parent.Get();
                    break;
                }
            }
        }
        const uint64_t ownerId = owner->GetId().Value();
        owners[ownerId] = owner->GetHandle();
        collidersByOwner[ownerId].push_back( entity.GetHandle() );
    }

    for( auto& [id, record] : m_bodies )
    {
        record.Seen = false;
    }

    const b2WorldId world = WorldId( m_world );
    for( auto& [ownerId, handle] : owners )
    {
        Entity& owner = *handle.Get();
        Transform& transform = owner.GetComponent<Transform>();
        Rigidbody2D* rigidbody = Enabled<Rigidbody2D>( owner );
        std::vector<EntityHandle>& colliders = collidersByOwner[ownerId];
        std::sort( colliders.begin(), colliders.end(), []( const EntityHandle& a, const EntityHandle& b ) { return a->GetId().Value() < b->GetId().Value(); } );

        BodyRecord& record = m_bodies[ownerId];
        record.Seen = true;
        record.Owner = handle;

        const Vector3 worldPosition = transform.GetWorldPosition();
        const Vector2 position( worldPosition.x, worldPosition.y );
        const float angle = AngleOf( transform.GetWorldRotation() );
        if( !IsBody( record.Body ) )
        {
            b2BodyDef def = b2DefaultBodyDef();
            def.type = b2_staticBody;
            def.position = ToB2( position );
            def.rotation = b2MakeRot( angle );
            def.userData = UserDataFor( owner );
            record.Body = Pack( b2CreateBody( world, &def ) );
            record.BodySignature = 0;
            record.ShapeSignature = 0;
            record.AppliedPosition = record.PreviousPosition = record.CurrentPosition = position;
            record.AppliedAngle = record.PreviousAngle = record.CurrentAngle = angle;
        }
        if( rigidbody )
        {
            rigidbody->m_body = record.Body;
        }

        const uint64_t shapeSignature = ComputeShapeSignature( owner, colliders );
        uint64_t bodySignature = 0xb0d2ULL;
        if( rigidbody )
        {
            bodySignature = Mix( bodySignature, static_cast<uint64_t>( rigidbody->Type ) );
            for( float value : { rigidbody->Mass, rigidbody->LinearDamping, rigidbody->AngularDamping, rigidbody->GravityScale } )
            {
                bodySignature = Mix( bodySignature, Bits( value ) );
            }
            for( bool flag : { rigidbody->ContinuousCollision, rigidbody->CanSleep, rigidbody->FreezeRotation } )
            {
                bodySignature = Mix( bodySignature, flag ? 1 : 0 );
            }
        }
        const bool shapesChanged = shapeSignature != record.ShapeSignature;
        if( shapesChanged )
        {
            BuildShapes( record, colliders );
            record.ShapeSignature = shapeSignature;
        }
        if( shapesChanged || bodySignature != record.BodySignature )
        {
            ApplyBodySettings( record, rigidbody );
            record.BodySignature = bodySignature;
        }
    }

    for( auto it = m_bodies.begin(); it != m_bodies.end(); )
    {
        if( !it->second.Seen || !it->second.Owner )
        {
            DestroyRecord( it->second );
            it = m_bodies.erase( it );
        }
        else
        {
            ++it;
        }
    }
}


void Physics2DCore::SyncJoints()
{
    OPTICK_EVENT( "Physics2DCore::SyncJoints" );
    const b2WorldId world = WorldId( m_world );
    for( Entity& entity : GetEntities() )
    {
        PhysicsJoint2D* joint = entity.TryGetComponent<PhysicsJoint2D>();
        if( !joint )
        {
            continue;
        }
        const bool wanted = joint->IsEnabled() && entity.IsActiveInHierarchy() && !joint->m_broken;
        auto ownRecord = m_bodies.find( entity.GetId().Value() );
        const uint64_t bodyA = ( wanted && ownRecord != m_bodies.end() ) ? ownRecord->second.Body : 0;
        uint64_t bodyB = 0;
        if( joint->ConnectedBody )
        {
            auto other = m_bodies.find( joint->ConnectedBody->GetId().Value() );
            bodyB = other != m_bodies.end() ? other->second.Body : 0;
        }
        else if( bodyA != 0 )
        {
            bodyB = EnsureGroundBody();
        }

        uint64_t signature = Mix( Mix( 0x202ULL, bodyA ), bodyB );
        signature = Mix( Mix( signature, static_cast<uint64_t>( joint->Type ) ), joint->CollideConnected ? 1 : 0 );
        signature = MixVector( MixVector( MixVector( signature, joint->Anchor ), joint->Axis ), joint->ConnectedAnchor );
        for( float value : { joint->LowerLimit, joint->UpperLimit, joint->MotorSpeed, joint->MaxMotorForce, joint->SpringFrequency, joint->SpringDamping, joint->Distance } )
        {
            signature = Mix( signature, Bits( value ) );
        }
        signature = Mix( Mix( Mix( signature, joint->UseLimits ? 1 : 0 ), joint->UseMotor ? 1 : 0 ), joint->UseSpring ? 1 : 0 );

        const b2JointId existing = Unpack<b2JointId>( joint->m_joint );
        const bool valid = joint->m_joint != 0 && b2Joint_IsValid( existing );
        if( valid && signature == joint->m_signature )
        {
            continue;
        }
        if( valid )
        {
            b2DestroyJoint( existing );
        }
        joint->m_joint = 0;
        joint->m_signature = signature;
        if( !wanted || !IsBody( bodyA ) || !IsBody( bodyB ) || bodyA == bodyB )
        {
            continue;
        }

        // Box2D measures body B relative to body A: A is the connected body (or the ground), B this
        // entity, so positive motor speeds / translations / limits are about this entity's motion.
        const b2BodyId idA = Body( bodyB );
        const b2BodyId idB = Body( bodyA );
        Transform& transform = entity.GetComponent<Transform>();
        const Vector3 anchorWorld3 = transform.GetLocalToWorldMatrix().TransformPoint( Vector3( joint->Anchor.x, joint->Anchor.y, 0.f ) );
        const b2Vec2 anchor{ anchorWorld3.x, anchorWorld3.y };
        const Vector3 axisWorld3 = transform.GetWorldRotation() * Vector3( joint->Axis.x, joint->Axis.y, 0.f );
        b2Vec2 axis{ axisWorld3.x, axisWorld3.y };
        axis = b2LengthSquared( axis ) > 1e-8f ? b2Normalize( axis ) : b2Vec2{ 0.f, 1.f };
        const b2Vec2 localA = b2Body_GetLocalPoint( idA, anchor );
        const b2Vec2 localB = b2Body_GetLocalPoint( idB, anchor );
        const float referenceAngle = b2RelativeAngle( b2Body_GetRotation( idB ), b2Body_GetRotation( idA ) );

        b2JointId created = b2_nullJointId;
        switch( joint->Type )
        {
        case JointType2D::Fixed:
        {
            b2WeldJointDef def = b2DefaultWeldJointDef();
            def.bodyIdA = idA;
            def.bodyIdB = idB;
            def.localAnchorA = localA;
            def.localAnchorB = localB;
            def.referenceAngle = referenceAngle;
            def.collideConnected = joint->CollideConnected;
            def.userData = UserDataFor( entity );
            created = b2CreateWeldJoint( world, &def );
            break;
        }
        case JointType2D::Hinge:
        {
            b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
            def.bodyIdA = idA;
            def.bodyIdB = idB;
            def.localAnchorA = localA;
            def.localAnchorB = localB;
            def.referenceAngle = referenceAngle;
            def.enableLimit = joint->UseLimits;
            def.lowerAngle = joint->LowerLimit * kToRadians;
            def.upperAngle = joint->UpperLimit * kToRadians;
            def.enableMotor = joint->UseMotor;
            def.motorSpeed = joint->MotorSpeed * kToRadians;
            def.maxMotorTorque = joint->MaxMotorForce;
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            def.collideConnected = joint->CollideConnected;
            def.userData = UserDataFor( entity );
            created = b2CreateRevoluteJoint( world, &def );
            break;
        }
        case JointType2D::Slider:
        {
            b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
            def.bodyIdA = idA;
            def.bodyIdB = idB;
            def.localAnchorA = localA;
            def.localAnchorB = localB;
            def.localAxisA = b2Body_GetLocalVector( idA, axis );
            def.referenceAngle = referenceAngle;
            def.enableLimit = joint->UseLimits;
            def.lowerTranslation = joint->LowerLimit;
            def.upperTranslation = joint->UpperLimit;
            def.enableMotor = joint->UseMotor;
            def.motorSpeed = joint->MotorSpeed;
            def.maxMotorForce = joint->MaxMotorForce;
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            def.collideConnected = joint->CollideConnected;
            def.userData = UserDataFor( entity );
            created = b2CreatePrismaticJoint( world, &def );
            break;
        }
        case JointType2D::Distance:
        {
            b2DistanceJointDef def = b2DefaultDistanceJointDef();
            def.bodyIdA = idA;
            def.bodyIdB = idB;
            // The other end: ConnectedAnchor on the connected body, or a world point (ground at the origin).
            def.localAnchorA = ToB2( joint->ConnectedAnchor );
            def.localAnchorB = localB;
            const b2Vec2 otherEnd = b2Body_GetWorldPoint( idA, def.localAnchorA );
            def.length = joint->Distance > 0.f ? joint->Distance : std::max( b2Distance( anchor, otherEnd ), 0.01f );
            def.enableLimit = joint->UseLimits;
            def.minLength = std::max( joint->LowerLimit, 0.f );
            def.maxLength = std::max( joint->UpperLimit, def.minLength );
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            def.collideConnected = joint->CollideConnected;
            def.userData = UserDataFor( entity );
            created = b2CreateDistanceJoint( world, &def );
            break;
        }
        case JointType2D::Wheel:
        {
            b2WheelJointDef def = b2DefaultWheelJointDef();
            def.bodyIdA = idA;
            def.bodyIdB = idB;
            def.localAnchorA = localA;
            def.localAnchorB = localB;
            def.localAxisA = b2Body_GetLocalVector( idA, axis );
            def.enableLimit = joint->UseLimits;
            def.lowerTranslation = joint->LowerLimit;
            def.upperTranslation = joint->UpperLimit;
            def.enableMotor = joint->UseMotor;
            def.motorSpeed = joint->MotorSpeed * kToRadians;
            def.maxMotorTorque = joint->MaxMotorForce;
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            def.collideConnected = joint->CollideConnected;
            def.userData = UserDataFor( entity );
            created = b2CreateWheelJoint( world, &def );
            break;
        }
        }
        joint->m_joint = Pack( created );
    }
}


void Physics2DCore::BreakJoints()
{
    // Box2D has no joint events: compare the constraint loads after each step.
    for( Entity& entity : GetEntities() )
    {
        PhysicsJoint2D* joint = entity.TryGetComponent<PhysicsJoint2D>();
        if( !joint || joint->BreakForce <= 0.f || joint->m_joint == 0 )
        {
            continue;
        }
        const b2JointId id = Unpack<b2JointId>( joint->m_joint );
        if( !b2Joint_IsValid( id ) )
        {
            continue;
        }
        const float force = b2Length( b2Joint_GetConstraintForce( id ) );
        const float torque = std::abs( b2Joint_GetConstraintTorque( id ) );
        if( force > joint->BreakForce || torque > joint->BreakForce )
        {
            b2DestroyJoint( id );
            joint->m_joint = 0;
            joint->m_broken = true;
        }
    }
}


void Physics2DCore::SyncCharacterBody( CharacterController2D& InController, Entity& InEntity )
{
    const float half = std::max( InController.Height * 0.5f - InController.Radius, 0.f );
    if( InController.m_selfBit == 0 )
    {
        InController.m_selfBit = 1ull << ( 32 + ( m_nextCharacterSlot++ % 32 ) );
    }
    uint64_t signature = Mix( Mix( 0xcc2ULL, Bits( InController.Radius ) ), Bits( half ) );
    signature = Mix( Mix( signature, InEntity.GetLayer() ), ProjectSettings::Get().GetCollisionMask( InEntity.GetLayer() ) );
    if( IsBody( InController.m_body ) && InController.m_bodySignature == signature )
    {
        return;
    }
    if( IsBody( InController.m_body ) )
    {
        b2DestroyBody( Body( InController.m_body ) );
    }
    b2BodyDef def = b2DefaultBodyDef();
    def.type = b2_kinematicBody;
    def.position = ToB2( InController.m_position );
    def.userData = UserDataFor( InEntity );
    const b2BodyId body = b2CreateBody( WorldId( m_world ), &def );
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.userData = UserDataFor( InEntity );
    shapeDef.filter = FilterFor( InEntity );
    shapeDef.filter.maskBits &= ~InController.m_selfBit;   // invisible to its own mover queries
    shapeDef.enableSensorEvents = true;
    if( half > 0.01f )
    {
        const b2Capsule capsule{ b2Vec2{ 0.f, -half }, b2Vec2{ 0.f, half }, InController.Radius };
        b2CreateCapsuleShape( body, &shapeDef, &capsule );
    }
    else
    {
        const b2Circle circle{ b2Vec2{ 0.f, 0.f }, InController.Radius };
        b2CreateCircleShape( body, &shapeDef, &circle );
    }
    InController.m_body = Pack( body );
    InController.m_bodySignature = signature;
}


void Physics2DCore::StepCharacters( float InDeltaSeconds )
{
    OPTICK_EVENT( "Physics2DCore::StepCharacters" );
    const b2WorldId world = WorldId( m_world );
    const float gravity = b2World_GetGravity( world ).y;
    for( Entity& entity : GetEntities() )
    {
        CharacterController2D* controller = Enabled<CharacterController2D>( entity );
        if( !controller || !entity.IsActiveInHierarchy() )
        {
            if( controller && IsBody( controller->m_body ) )
            {
                b2DestroyBody( Body( controller->m_body ) );
                controller->m_body = 0;
            }
            continue;
        }
        CharacterController2D& c = *controller;
        Transform& transform = entity.GetComponent<Transform>();
        const Vector3 worldPosition = transform.GetWorldPosition();
        const Vector2 transformCenter = Vector2( worldPosition.x, worldPosition.y ) - c.Offset;
        if( !c.m_hasPosition || LengthSquared( transformCenter - c.m_appliedCenter ) > 1e-6f )
        {
            c.m_position = c.m_previousPosition = c.m_appliedCenter = transformCenter;
            c.m_hasPosition = true;
        }
        SyncCharacterBody( c, entity );

        // Velocity: run, fall, jump (with coyote time).
        const float desired = c.m_moveInput * c.MaxSpeed;
        const float acceleration = c.Acceleration * ( c.m_grounded ? 1.f : c.AirControl ) * InDeltaSeconds;
        float horizontal = c.m_velocity.x;
        horizontal += std::clamp( desired - horizontal, -acceleration, acceleration );
        float vertical = c.m_velocity.y;
        const float g = gravity * c.GravityScale;
        bool jumped = false;
        if( c.m_wantsJump && ( c.m_grounded || c.m_airTime <= c.CoyoteTime ) )
        {
            vertical = std::sqrt( 2.f * std::abs( g ) * std::max( c.JumpHeight, 0.f ) );
            jumped = true;
            c.m_airTime = c.CoyoteTime + 1.f;   // no second jump from the same ledge
        }
        else if( c.m_grounded && vertical < 0.f )
        {
            vertical = 0.f;
        }
        vertical += g * InDeltaSeconds;
        c.m_wantsJump = false;
        c.m_velocity = Vector2( horizontal, vertical );

        // Mover: collide, solve against the contact planes, sweep, repeat.
        const float half = std::max( c.Height * 0.5f - c.Radius, 0.f );
        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.categoryBits = c.m_selfBit;
        filter.maskBits = ProjectSettings::Get().GetCollisionMask( entity.GetLayer() );
        struct MoverContext
        {
            b2BodyId Self;
            float MinGroundY;
            std::vector<b2CollisionPlane> Planes;
            bool Grounded = false;
            b2Vec2 GroundNormal{ 0.f, 1.f };
            std::vector<b2BodyId> Pushed;
        } context{ Body( c.m_body ), std::cos( c.SlopeLimit * kToRadians ) };
        auto collectPlane = []( b2ShapeId shape, const b2PlaneResult* plane, void* ctx ) -> bool {
            MoverContext* self = static_cast<MoverContext*>( ctx );
            const b2BodyId body = b2Shape_GetBody( shape );
            if( !plane->hit || B2_ID_EQUALS( body, self->Self ) || b2Shape_IsSensor( shape ) )
            {
                return true;
            }
            self->Planes.push_back( b2CollisionPlane{ plane->plane, FLT_MAX, 0.f, true } );
            if( plane->plane.normal.y >= self->MinGroundY )
            {
                self->Grounded = true;
                self->GroundNormal = plane->plane.normal;
            }
            if( b2Body_GetType( body ) == b2_dynamicBody && std::none_of( self->Pushed.begin(), self->Pushed.end(), [body]( b2BodyId id ) { return B2_ID_EQUALS( id, body ); } ) )
            {
                self->Pushed.push_back( body );
            }
            return true;
        };
        auto capsuleAt = [half, &c]( b2Vec2 InCenter ) {
            return b2Capsule{ b2Vec2{ InCenter.x, InCenter.y - half }, b2Vec2{ InCenter.x, InCenter.y + half }, c.Radius };
        };
        auto collideAt = [&]( b2Vec2 InCenter ) {
            context.Planes.clear();
            context.Grounded = false;
            const b2Capsule mover = capsuleAt( InCenter );
            b2World_CollideMover( world, &mover, filter, collectPlane, &context );
        };

        const Vector2 intended = c.m_velocity;
        const Vector2 delta = c.m_velocity * InDeltaSeconds + c.m_pendingMove;
        c.m_pendingMove = Vector2();
        b2Vec2 position = ToB2( c.m_position );
        const b2Vec2 target = b2Add( position, ToB2( delta ) );
        const bool wasGrounded = c.m_grounded;
        for( int iteration = 0; iteration < 5; ++iteration )
        {
            collideAt( position );
            const b2PlaneSolverResult solved = b2SolvePlanes( b2Sub( target, position ), context.Planes.data(), static_cast<int>( context.Planes.size() ) );
            const b2Capsule mover = capsuleAt( position );
            const float fraction = b2World_CastMover( world, &mover, solved.translation, filter );
            const b2Vec2 step = b2MulSV( fraction, solved.translation );
            position = b2Add( position, step );
            if( b2LengthSquared( step ) < 1e-8f )
            {
                break;
            }
        }

        // Contact state where we ended up: grounding, and velocity lost into walls / the floor.
        collideAt( position );
        bool grounded = context.Grounded && !jumped;
        if( !grounded && wasGrounded && !jumped && c.GroundSnap > 0.f && c.m_velocity.y <= 0.f )
        {
            const b2Vec2 down{ 0.f, -c.GroundSnap };
            const b2Capsule mover = capsuleAt( position );
            const float fraction = b2World_CastMover( world, &mover, down, filter );
            if( fraction < 1.f )
            {
                position = b2MulAdd( position, fraction, down );
                collideAt( position );
                grounded = context.Grounded;
            }
        }
        c.m_velocity = FromB2( b2ClipVector( ToB2( c.m_velocity ), context.Planes.data(), static_cast<int>( context.Planes.size() ) ) );
        c.m_grounded = grounded;
        c.m_groundNormal = grounded ? FromB2( context.GroundNormal ) : Vector2( 0.f, 1.f );
        c.m_airTime = grounded ? 0.f : c.m_airTime + InDeltaSeconds;

        if( c.PushStrength > 0.f )
        {
            for( b2BodyId other : context.Pushed )
            {
                b2Body_ApplyLinearImpulseToCenter( other, b2Vec2{ intended.x * c.PushStrength * InDeltaSeconds * b2Body_GetMass( other ), 0.f }, true );
            }
        }

        c.m_previousPosition = c.m_position;
        c.m_position = FromB2( position );
        b2Body_SetTargetTransform( Body( c.m_body ), b2Transform{ position, b2Rot_identity }, InDeltaSeconds );
    }
}


void Physics2DCore::FixedUpdate( const UpdateContext& inUpdateContext )
{
    if( !m_simulating || m_world == 0 )
    {
        return;
    }
    OPTICK_CATEGORY( "Physics2DCore::FixedUpdate", Optick::Category::Physics );
    const float dt = inUpdateContext.GetFixedDeltaTime();
    SyncBodies();
    SyncJoints();

    // Bodies whose Transform was moved by something else follow it.
    for( auto& [id, record] : m_bodies )
    {
        if( !IsBody( record.Body ) || !record.Owner )
        {
            continue;
        }
        const b2BodyId body = Body( record.Body );
        Transform& transform = record.Owner->GetComponent<Transform>();
        const Vector3 worldPosition = transform.GetWorldPosition();
        Vector2 position( worldPosition.x, worldPosition.y );
        float angle = AngleOf( transform.GetWorldRotation() );
        Rigidbody2D* rigidbody = record.Owner->TryGetComponent<Rigidbody2D>();
        if( b2Body_GetType( body ) == b2_kinematicBody )
        {
            if( rigidbody && rigidbody->m_hasPendingMove )
            {
                position = rigidbody->m_movePosition;
                angle = rigidbody->m_moveAngle;
                rigidbody->m_hasPendingMove = false;
                transform.SetWorldPosition( Vector3( position.x, position.y, worldPosition.z ) );
                transform.SetWorldRotation( WithAngle( transform.GetWorldRotation(), angle ) );
            }
            b2Body_SetTargetTransform( body, b2Transform{ ToB2( position ), b2MakeRot( angle ) }, dt );
            record.AppliedPosition = position;
            record.AppliedAngle = angle;
        }
        else if( !SamePose( position, angle, record.AppliedPosition, record.AppliedAngle ) )
        {
            b2Body_SetTransform( body, ToB2( position ), b2MakeRot( angle ) );
            record.AppliedPosition = record.PreviousPosition = record.CurrentPosition = position;
            record.AppliedAngle = record.PreviousAngle = record.CurrentAngle = angle;
        }
    }

    StepCharacters( dt );

    const auto start = std::chrono::steady_clock::now();
    m_taskCount = 0;
    b2World_Step( WorldId( m_world ), dt, kSubSteps );
    m_lastStepMs = std::chrono::duration<float, std::milli>( std::chrono::steady_clock::now() - start ).count();

    for( auto& [id, record] : m_bodies )
    {
        if( !IsBody( record.Body ) || b2Body_GetType( Body( record.Body ) ) != b2_dynamicBody )
        {
            continue;
        }
        const b2Transform pose = b2Body_GetTransform( Body( record.Body ) );
        record.PreviousPosition = record.CurrentPosition;
        record.PreviousAngle = record.CurrentAngle;
        record.CurrentPosition = FromB2( pose.p );
        record.CurrentAngle = b2Rot_GetAngle( pose.q );
    }

    BreakJoints();
    ProcessEvents();
}


void Physics2DCore::ProcessEvents()
{
    World& world = GetWorld();
    const b2WorldId worldId = WorldId( m_world );
    auto zOf = []( const EntityHandle& InEntity ) {
        Transform* transform = InEntity ? InEntity->TryGetComponent<Transform>() : nullptr;
        return transform ? transform->GetWorldPosition().z : 0.f;
    };

    const b2ContactEvents contacts = b2World_GetContactEvents( worldId );
    for( int i = 0; i < contacts.beginCount; ++i )
    {
        const b2ContactBeginTouchEvent& touch = contacts.beginEvents[i];
        CollisionEvent event;
        event.A = EntityOfShape( world, touch.shapeIdA );
        event.B = EntityOfShape( world, touch.shapeIdB );
        event.State = CollisionEvent::Phase::Enter;
        event.Is2D = true;
        if( touch.manifold.pointCount > 0 )
        {
            event.Point = FromB2( touch.manifold.points[0].point, zOf( event.A ) );
            event.Normal = FromB2( touch.manifold.normal, 0.f );
        }
        m_pendingEvents.push_back( event );
    }
    for( int i = 0; i < contacts.endCount; ++i )
    {
        CollisionEvent event;
        event.A = EntityOfShape( world, contacts.endEvents[i].shapeIdA );
        event.B = EntityOfShape( world, contacts.endEvents[i].shapeIdB );
        event.State = CollisionEvent::Phase::Exit;
        event.Is2D = true;
        m_pendingEvents.push_back( event );
    }

    const b2SensorEvents sensors = b2World_GetSensorEvents( worldId );
    for( int i = 0; i < sensors.beginCount; ++i )
    {
        CollisionEvent event;
        event.A = EntityOfShape( world, sensors.beginEvents[i].sensorShapeId );
        event.B = EntityOfShape( world, sensors.beginEvents[i].visitorShapeId );
        event.State = CollisionEvent::Phase::Enter;
        event.IsTrigger = true;
        event.Is2D = true;
        m_pendingEvents.push_back( event );
    }
    for( int i = 0; i < sensors.endCount; ++i )
    {
        CollisionEvent event;
        event.A = EntityOfShape( world, sensors.endEvents[i].sensorShapeId );
        event.B = EntityOfShape( world, sensors.endEvents[i].visitorShapeId );
        event.State = CollisionEvent::Phase::Exit;
        event.IsTrigger = true;
        event.Is2D = true;
        m_pendingEvents.push_back( event );
    }

    std::vector<CollisionEvent> events;
    events.swap( m_pendingEvents );
    for( CollisionEvent& event : events )
    {
        event.Fire();
    }
}


void Physics2DCore::WritePoses( float InAlpha )
{
    for( auto& [id, record] : m_bodies )
    {
        if( !IsBody( record.Body ) || !record.Owner || b2Body_GetType( Body( record.Body ) ) != b2_dynamicBody )
        {
            continue;
        }
        Rigidbody2D* rigidbody = record.Owner->TryGetComponent<Rigidbody2D>();
        const bool interpolate = rigidbody && rigidbody->Interpolate;
        const Vector2 position = interpolate ? record.PreviousPosition + ( record.CurrentPosition - record.PreviousPosition ) * InAlpha : record.CurrentPosition;
        const float angle = interpolate ? record.PreviousAngle + b2UnwindAngle( record.CurrentAngle - record.PreviousAngle ) * InAlpha : record.CurrentAngle;
        Transform& transform = record.Owner->GetComponent<Transform>();
        transform.SetWorldPosition( Vector3( position.x, position.y, transform.GetWorldPosition().z ) );
        transform.SetWorldRotation( WithAngle( transform.GetWorldRotation(), angle ) );
        const Vector3 written = transform.GetWorldPosition();
        record.AppliedPosition = Vector2( written.x, written.y );
        record.AppliedAngle = AngleOf( transform.GetWorldRotation() );
    }

    for( Entity& entity : GetEntities() )
    {
        CharacterController2D* controller = Enabled<CharacterController2D>( entity );
        if( !controller || !controller->m_hasPosition )
        {
            continue;
        }
        const Vector2 center = controller->m_previousPosition + ( controller->m_position - controller->m_previousPosition ) * InAlpha;
        Transform& transform = entity.GetComponent<Transform>();
        transform.SetWorldPosition( Vector3( center.x + controller->Offset.x, center.y + controller->Offset.y, transform.GetWorldPosition().z ) );
        const Vector3 written = transform.GetWorldPosition();
        controller->m_appliedCenter = Vector2( written.x, written.y ) - controller->Offset;
    }
}


void Physics2DCore::Update( const UpdateContext& inUpdateContext )
{
    if( m_world == 0 )
    {
        return;
    }
    OPTICK_CATEGORY( "Physics2DCore::Update", Optick::Category::Physics );
    if( m_simulating )
    {
        WritePoses( inUpdateContext.GetInterpolationAlpha() );
    }
    else
    {
        SetGravity( Vector2( ProjectSettings::Get().Gravity.x, ProjectSettings::Get().Gravity.y ) );
        SyncBodies();
        SyncJoints();
        for( auto& [id, record] : m_bodies )
        {
            if( !IsBody( record.Body ) || !record.Owner )
            {
                continue;
            }
            Transform& transform = record.Owner->GetComponent<Transform>();
            const Vector3 worldPosition = transform.GetWorldPosition();
            const Vector2 position( worldPosition.x, worldPosition.y );
            const float angle = AngleOf( transform.GetWorldRotation() );
            if( !SamePose( position, angle, record.AppliedPosition, record.AppliedAngle ) )
            {
                b2Body_SetTransform( Body( record.Body ), ToB2( position ), b2MakeRot( angle ) );
                record.AppliedPosition = record.PreviousPosition = record.CurrentPosition = position;
                record.AppliedAngle = record.PreviousAngle = record.CurrentAngle = angle;
            }
        }
    }

    if( PhysicsCore::DebugDrawEnabled && !m_bodies.empty() )
    {
        b2DebugDraw draw = MakeDebugDraw();
        b2World_Draw( WorldId( m_world ), &draw );
    }
}


void Physics2DCore::SetGravity( const Vector2& InGravity )
{
    if( m_world != 0 )
    {
        b2World_SetGravity( WorldId( m_world ), ToB2( InGravity ) );
    }
}


Vector2 Physics2DCore::GetGravity() const
{
    return m_world != 0 ? FromB2( b2World_GetGravity( WorldId( m_world ) ) ) : Vector2( ProjectSettings::Get().Gravity.x, ProjectSettings::Get().Gravity.y );
}


namespace
{
    struct CastContext
    {
        World* GameWorld = nullptr;
        float MaxDistance = 0.f;
        bool All = false;
        bool Hit = false;
        RaycastHit Closest;
        std::vector<RaycastHit> Hits;
    };

    float CollectCast( b2ShapeId InShape, b2Vec2 InPoint, b2Vec2 InNormal, float InFraction, void* InContext )
    {
        CastContext* self = static_cast<CastContext*>( InContext );
        if( b2Shape_IsSensor( InShape ) )
        {
            return -1.f;    // ignore triggers
        }
        RaycastHit hit;
        hit.Entity = EntityOfShape( *self->GameWorld, InShape );
        Transform* transform = hit.Entity ? hit.Entity->TryGetComponent<Transform>() : nullptr;
        hit.Position = FromB2( InPoint, transform ? transform->GetWorldPosition().z : 0.f );
        hit.Normal = FromB2( InNormal, 0.f );
        hit.Fraction = InFraction;
        hit.Distance = InFraction * self->MaxDistance;
        if( self->All )
        {
            self->Hits.push_back( hit );
            return 1.f;
        }
        self->Hit = true;
        self->Closest = hit;
        return InFraction;   // clip: only closer hits from now on
    }

    struct OverlapContext
    {
        World* GameWorld = nullptr;
        std::vector<EntityHandle>* Results = nullptr;
        bool PointTest = false;
        b2Vec2 Point{ 0.f, 0.f };
    };

    bool CollectOverlap( b2ShapeId InShape, void* InContext )
    {
        OverlapContext* self = static_cast<OverlapContext*>( InContext );
        if( b2Shape_IsSensor( InShape ) || ( self->PointTest && !b2Shape_TestPoint( InShape, self->Point ) ) )
        {
            return true;
        }
        EntityHandle entity = EntityOfShape( *self->GameWorld, InShape );
        if( entity && std::find( self->Results->begin(), self->Results->end(), entity ) == self->Results->end() )
        {
            self->Results->push_back( entity );
        }
        return true;
    }
}


bool Physics2DCore::Raycast( const Vector2& InOrigin, const Vector2& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask )
{
    if( m_world == 0 || LengthSquared( InDirection ) < 1e-12f )
    {
        return false;
    }
    const Vector2 direction = InDirection / InDirection.Length();
    CastContext context;
    context.GameWorld = &GetWorld();
    context.MaxDistance = InMaxDistance;
    b2World_CastRay( WorldId( m_world ), ToB2( InOrigin ), ToB2( direction * InMaxDistance ), QueryFilterFor( InLayerMask ), CollectCast, &context );
    if( context.Hit )
    {
        OutHit = context.Closest;
    }
    return context.Hit;
}


std::vector<RaycastHit> Physics2DCore::RaycastAll( const Vector2& InOrigin, const Vector2& InDirection, float InMaxDistance, uint32_t InLayerMask )
{
    if( m_world == 0 || LengthSquared( InDirection ) < 1e-12f )
    {
        return {};
    }
    const Vector2 direction = InDirection / InDirection.Length();
    CastContext context;
    context.GameWorld = &GetWorld();
    context.MaxDistance = InMaxDistance;
    context.All = true;
    b2World_CastRay( WorldId( m_world ), ToB2( InOrigin ), ToB2( direction * InMaxDistance ), QueryFilterFor( InLayerMask ), CollectCast, &context );
    std::sort( context.Hits.begin(), context.Hits.end(), []( const RaycastHit& a, const RaycastHit& b ) { return a.Distance < b.Distance; } );
    return context.Hits;
}


bool Physics2DCore::Linecast( const Vector2& InStart, const Vector2& InEnd, RaycastHit& OutHit, uint32_t InLayerMask )
{
    const Vector2 delta = InEnd - InStart;
    return Raycast( InStart, delta, delta.Length(), OutHit, InLayerMask );
}


bool Physics2DCore::CircleCast( const Vector2& InOrigin, float InRadius, const Vector2& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask )
{
    if( m_world == 0 || LengthSquared( InDirection ) < 1e-12f )
    {
        return false;
    }
    const Vector2 direction = InDirection / InDirection.Length();
    CastContext context;
    context.GameWorld = &GetWorld();
    context.MaxDistance = InMaxDistance;
    const b2Vec2 center = ToB2( InOrigin );
    const b2ShapeProxy proxy = b2MakeProxy( &center, 1, InRadius );
    b2World_CastShape( WorldId( m_world ), &proxy, ToB2( direction * InMaxDistance ), QueryFilterFor( InLayerMask ), CollectCast, &context );
    if( context.Hit )
    {
        OutHit = context.Closest;
    }
    return context.Hit;
}


std::vector<EntityHandle> Physics2DCore::OverlapCircle( const Vector2& InCenter, float InRadius, uint32_t InLayerMask )
{
    std::vector<EntityHandle> results;
    if( m_world == 0 )
    {
        return results;
    }
    OverlapContext context{ &GetWorld(), &results };
    const b2Vec2 center = ToB2( InCenter );
    const b2ShapeProxy proxy = b2MakeProxy( &center, 1, InRadius );
    b2World_OverlapShape( WorldId( m_world ), &proxy, QueryFilterFor( InLayerMask ), CollectOverlap, &context );
    return results;
}


std::vector<EntityHandle> Physics2DCore::OverlapBox( const Vector2& InCenter, const Vector2& InHalfExtents, float InAngleDegrees, uint32_t InLayerMask )
{
    std::vector<EntityHandle> results;
    if( m_world == 0 )
    {
        return results;
    }
    OverlapContext context{ &GetWorld(), &results };
    const b2Rot rotation = b2MakeRot( InAngleDegrees * kToRadians );
    const b2Vec2 center = ToB2( InCenter );
    const b2Vec2 corners[4] = {
        b2Add( center, b2RotateVector( rotation, b2Vec2{ -InHalfExtents.x, -InHalfExtents.y } ) ),
        b2Add( center, b2RotateVector( rotation, b2Vec2{ InHalfExtents.x, -InHalfExtents.y } ) ),
        b2Add( center, b2RotateVector( rotation, b2Vec2{ InHalfExtents.x, InHalfExtents.y } ) ),
        b2Add( center, b2RotateVector( rotation, b2Vec2{ -InHalfExtents.x, InHalfExtents.y } ) ),
    };
    const b2ShapeProxy proxy = b2MakeProxy( corners, 4, 0.f );
    b2World_OverlapShape( WorldId( m_world ), &proxy, QueryFilterFor( InLayerMask ), CollectOverlap, &context );
    return results;
}


std::vector<EntityHandle> Physics2DCore::OverlapPoint( const Vector2& InPoint, uint32_t InLayerMask )
{
    std::vector<EntityHandle> results;
    if( m_world == 0 )
    {
        return results;
    }
    OverlapContext context{ &GetWorld(), &results, true, ToB2( InPoint ) };
    const b2AABB box{ b2Vec2{ InPoint.x - 0.001f, InPoint.y - 0.001f }, b2Vec2{ InPoint.x + 0.001f, InPoint.y + 0.001f } };
    b2World_OverlapAABB( WorldId( m_world ), box, QueryFilterFor( InLayerMask ), CollectOverlap, &context );
    return results;
}


#if USING( ME_EDITOR )
void Physics2DCore::OnEditorInspect()
{
    Base::OnEditorInspect();
    ImGui::Text( "Bodies: %zu  %s", m_bodies.size(), m_simulating ? "simulating" : "edit mode" );
    if( m_world != 0 )
    {
        const b2Counters counters = b2World_GetCounters( WorldId( m_world ) );
        ImGui::Text( "Shapes: %d  Contacts: %d  Joints: %d  Awake: %d", counters.shapeCount, counters.contactCount, counters.jointCount, b2World_GetAwakeBodyCount( WorldId( m_world ) ) );
    }
    ImGui::Text( "Step: %.2f ms", m_lastStepMs );
    ImGui::Checkbox( "Draw Physics", &PhysicsCore::DebugDrawEnabled );
    Vector2 gravity = GetGravity();
    if( ImGui::DragFloat2( "Gravity", &gravity.x, 0.05f ) )
    {
        SetGravity( gravity );
    }
}
#endif
