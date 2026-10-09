#include "PCH.h"
#include "PhysicsCore.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/PhysicsJoint.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Graphics/MeshData.h"
#include "Physics/Box3DUtils.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Physics/PhysicsHash.h"
#include "optick.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

using namespace Box3DUtils;
using namespace PhysicsHash;

namespace
{
    constexpr int kSubSteps = 4;
    constexpr float kPi = 3.14159265358979f;

    b3WorldId WorldId( uint64_t InValue )
    {
        b3WorldId id;
        std::memcpy( &id, &InValue, sizeof( id ) );
        return id;
    }

    uint64_t PackWorld( b3WorldId InId )
    {
        uint64_t value = 0;
        std::memcpy( &value, &InId, sizeof( InId ) );
        return value;
    }

    bool SamePose( const Vector3& InPositionA, const Quaternion& InRotationA, const Vector3& InPositionB, const Quaternion& InRotationB )
    {
        constexpr float kEpsilon = 1e-5f;
        return std::abs( InPositionA.x - InPositionB.x ) < kEpsilon && std::abs( InPositionA.y - InPositionB.y ) < kEpsilon && std::abs( InPositionA.z - InPositionB.z ) < kEpsilon
            && std::abs( InRotationA.x - InRotationB.x ) < kEpsilon && std::abs( InRotationA.y - InRotationB.y ) < kEpsilon && std::abs( InRotationA.z - InRotationB.z ) < kEpsilon
            && std::abs( InRotationA.w - InRotationB.w ) < kEpsilon;
    }

    Vector3 AbsVector( const Vector3& v )
    {
        return Vector3( std::abs( v.x ), std::abs( v.y ), std::abs( v.z ) );
    }

    Vector3 Scale( const Vector3& a, const Vector3& b )
    {
        return Vector3( a.x * b.x, a.y * b.y, a.z * b.z );
    }

    Quaternion Nlerp( const Quaternion& a, const Quaternion& b, float t )
    {
        const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        const float sign = dot < 0.f ? -1.f : 1.f;
        Quaternion q( a.x + ( b.x * sign - a.x ) * t, a.y + ( b.y * sign - a.y ) * t, a.z + ( b.z * sign - a.z ) * t, a.w + ( b.w * sign - a.w ) * t );
        const float length = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
        return length > 0.f ? Quaternion( q.x / length, q.y / length, q.z / length, q.w / length ) : b;
    }

    // Shortest rotation taking +InFrom (unit) to InTo (unit).
    b3Quat RotationBetween( b3Vec3 InFrom, b3Vec3 InTo )
    {
        const float d = b3Dot( InFrom, InTo );
        if( d < -0.9999f )
        {
            b3Vec3 axis = std::abs( InFrom.x ) < 0.9f ? b3Cross( InFrom, b3Vec3{ 1.f, 0.f, 0.f } ) : b3Cross( InFrom, b3Vec3{ 0.f, 1.f, 0.f } );
            axis = b3Normalize( axis );
            return b3Quat{ axis, 0.f };
        }
        const b3Vec3 c = b3Cross( InFrom, InTo );
        b3Quat q{ c, 1.f + d };
        const float length = std::sqrt( q.v.x * q.v.x + q.v.y * q.v.y + q.v.z * q.v.z + q.s * q.s );
        return b3Quat{ b3Vec3{ q.v.x / length, q.v.y / length, q.v.z / length }, q.s / length };
    }

    // Copies what we set on a joint definition, keeping the defaults' validation cookie and thresholds.
    void ApplyJointBase( b3JointDef& InOut, const b3JointDef& InBase )
    {
        InOut.bodyIdA = InBase.bodyIdA;
        InOut.bodyIdB = InBase.bodyIdB;
        InOut.localFrameA = InBase.localFrameA;
        InOut.localFrameB = InBase.localFrameB;
        InOut.collideConnected = InBase.collideConnected;
        InOut.userData = InBase.userData;
    }


    EntityHandle EntityFromUserData( World& InWorld, void* InUserData )
    {
        return InWorld.FindEntityByIDValue( static_cast<uint64_t>( reinterpret_cast<uintptr_t>( InUserData ) ) );
    }

    void* UserDataFor( Entity& InEntity )
    {
        return reinterpret_cast<void*>( static_cast<uintptr_t>( InEntity.GetId().Value() ) );
    }

    b3Filter FilterFor( Entity& InEntity )
    {
        b3Filter filter = b3DefaultFilter();
        filter.categoryBits = PhysicsLayers::Bit( InEntity.GetLayer() );
        filter.maskBits = ProjectSettings::Get().GetCollisionMask( InEntity.GetLayer() );
        return filter;
    }

    b3QueryFilter QueryFilterFor( uint32_t InLayerMask )
    {
        b3QueryFilter filter = b3DefaultQueryFilter();
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
        return Enabled<BoxCollider>( InEntity ) || Enabled<SphereCollider>( InEntity ) || Enabled<CapsuleCollider>( InEntity ) || Enabled<MeshCollider>( InEntity );
    }

    bool HasTriangleMesh( Entity& InEntity )
    {
        MeshCollider* collider = Enabled<MeshCollider>( InEntity );
        return collider && !collider->Convex;
    }
}


PhysicsCore::PhysicsCore()
    : Base( ComponentFilter().Requires<Transform>().RequiresOneOf<Rigidbody>().RequiresOneOf<BoxCollider>().RequiresOneOf<SphereCollider>()
        .RequiresOneOf<CapsuleCollider>().RequiresOneOf<MeshCollider>().RequiresOneOf<CharacterController>().RequiresOneOf<PhysicsJoint>() )
{
    SetIsSerializable( false );
}


PhysicsCore::~PhysicsCore()
{
    DestroyWorld();
}


void PhysicsCore::Init()
{
    // A new World (scene load): start from a fresh simulation.
    DestroyWorld();
    CreateWorld();
}


void PhysicsCore::OnRemovedFromWorld()
{
    DestroyWorld();
}


void PhysicsCore::OnStart()
{
    m_simulating = true;
    SetGravity( ProjectSettings::Get().Gravity );
    // Bodies exist from the first frame of play (queries in OnStart work).
    SyncBodies();
    SyncJoints();
    // Interpolation starts from where everything is now.
    for( auto& [id, record] : m_bodies )
    {
        record.PreviousPosition = record.CurrentPosition = record.AppliedPosition;
        record.PreviousRotation = record.CurrentRotation = record.AppliedRotation;
    }
}


void PhysicsCore::OnStop()
{
    m_simulating = false;
}


void PhysicsCore::CreateWorld()
{
    b3WorldDef def = b3DefaultWorldDef();
    def.gravity = ToB3( ProjectSettings::Get().Gravity );
    def.workerCount = std::max<uint32_t>( 1, Jobs::JobSystem::Get().GetThreadCount() );
    def.enqueueTask = reinterpret_cast<b3EnqueueTaskCallback*>( &PhysicsCore::EnqueueTask );
    def.finishTask = &PhysicsCore::FinishTask;
    def.userTaskContext = this;
    def.createDebugShape = &PhysicsDebugDraw::CreateDebugShape;
    def.destroyDebugShape = &PhysicsDebugDraw::DestroyDebugShape;
    m_world = PackWorld( b3CreateWorld( &def ) );
}


void PhysicsCore::DestroyWorld()
{
    if( m_world != 0 && b3World_IsValid( WorldId( m_world ) ) )
    {
        b3DestroyWorld( WorldId( m_world ) );
    }
    m_world = 0;
    m_groundBody = 0;
    // Bodies died with the world. Components keep their stale ids, which Box3D rejects (the world
    // generation is part of every id); entities aren't touched since their World may be gone.
    m_bodies.clear();
    for( auto& [mesh, hull] : m_hulls )
    {
        b3DestroyHull( static_cast<b3HullData*>( hull ) );
    }
    m_hulls.clear();
    for( auto& [mesh, data] : m_meshes )
    {
        b3DestroyMesh( static_cast<b3MeshData*>( data ) );
    }
    m_meshes.clear();
}


void* PhysicsCore::EnqueueTask( void* InTask, void* InTaskContext, void* InUserContext, const char* )
{
    PhysicsCore* self = static_cast<PhysicsCore*>( InUserContext );
    b3TaskCallback* task = reinterpret_cast<b3TaskCallback*>( InTask );
    if( self->m_taskCount >= static_cast<int>( self->m_tasks.size() ) )
    {
        task( InTaskContext );   // out of slots: run inline
        return nullptr;
    }
    PhysicsTask& slot = self->m_tasks[self->m_taskCount++];
    slot.Task = InTask;
    slot.Context = InTaskContext;
    Jobs::JobSystem::Get().Submit( slot.Counter, []( void* context, uint32_t, uint32_t ) {
        PhysicsTask* job = static_cast<PhysicsTask*>( context );
        reinterpret_cast<b3TaskCallback*>( job->Task )( job->Context );
    }, &slot, 0, 1 );
    return &slot;
}


void PhysicsCore::FinishTask( void* InUserTask, void* )
{
    if( InUserTask )
    {
        Jobs::JobSystem::Get().Wait( static_cast<PhysicsTask*>( InUserTask )->Counter );
    }
}


void PhysicsCore::RelativePose( Transform& InCollider, Transform& InOwner, Vector3& OutPosition, Quaternion& OutRotation ) const
{
    // Walk up the local transforms (exact and stable, unlike differencing world matrices).
    Vector3 position;
    Quaternion rotation;
    Transform* node = &InCollider;
    while( node && node != &InOwner )
    {
        position = node->GetPosition() + node->GetRotation() * Scale( node->GetScale(), position );
        rotation = node->GetRotation() * rotation;
        node = node->GetParentTransform();
    }
    // The body frame is unscaled: apply the owner's scale to the offset.
    OutPosition = Scale( InOwner.GetWorldScale(), position );
    OutRotation = rotation;
}


uint64_t PhysicsCore::ComputeShapeSignature( Entity& InOwner, const std::vector<ColliderEntry>& InColliders ) const
{
    uint64_t h = 0x5ba9e5ULL;
    Transform& owner = InOwner.GetComponent<Transform>();
    h = MixVector( h, owner.GetWorldScale(), true );
    for( const ColliderEntry& entry : InColliders )
    {
        Entity& entity = *entry.Entity.Get();
        Transform& transform = entity.GetComponent<Transform>();
        h = Mix( h, entity.GetId().Value() );
        h = Mix( Mix( h, entity.GetLayer() ), ProjectSettings::Get().GetCollisionMask( entity.GetLayer() ) );
        h = MixVector( h, transform.GetWorldScale(), true );
        if( &transform != &owner )
        {
            Vector3 position;
            Quaternion rotation;
            RelativePose( transform, owner, position, rotation );
            h = MixVector( h, position, true );
            h = Mix( Mix( Mix( Mix( h, Quantized( rotation.x ) ), Quantized( rotation.y ) ), Quantized( rotation.z ) ), Quantized( rotation.w ) );
        }
        if( BoxCollider* box = Enabled<BoxCollider>( entity ) )
        {
            h = MixVector( Mix( Mix( h, 1 ), box->SettingsHash() ), box->Size );
        }
        if( SphereCollider* sphere = Enabled<SphereCollider>( entity ) )
        {
            h = Mix( Mix( Mix( h, 2 ), sphere->SettingsHash() ), Bits( sphere->Radius ) );
        }
        if( CapsuleCollider* capsule = Enabled<CapsuleCollider>( entity ) )
        {
            h = Mix( Mix( Mix( Mix( Mix( h, 3 ), capsule->SettingsHash() ), Bits( capsule->Radius ) ), Bits( capsule->Height ) ), static_cast<uint64_t>( capsule->Direction ) );
        }
        if( MeshCollider* meshCollider = Enabled<MeshCollider>( entity ) )
        {
            Mesh* mesh = entity.TryGetComponent<Mesh>();
            h = Mix( Mix( Mix( Mix( h, 4 ), meshCollider->SettingsHash() ), meshCollider->Convex ? 1 : 0 ), reinterpret_cast<uintptr_t>( mesh ? mesh->MeshReferece : nullptr ) );
        }
    }
    return h;
}


const void* PhysicsCore::GetHull( Moonlight::MeshData* InMesh )
{
    if( !InMesh || InMesh->CollisionPositions.size() < 4 )
    {
        return nullptr;
    }
    auto found = m_hulls.find( InMesh );
    if( found != m_hulls.end() )
    {
        return found->second;
    }
    std::vector<b3Vec3> points;
    points.reserve( InMesh->CollisionPositions.size() );
    for( const Vector3& position : InMesh->CollisionPositions )
    {
        points.push_back( ToB3( position ) );
    }
    // Box3D caps the hull's half-edges; simplify until it fits (smooth meshes like cylinders need it).
    b3HullData* hull = nullptr;
    for( int maxVertices : { 40, 28, 16, 8 } )
    {
        hull = b3CreateHull( points.data(), static_cast<int>( points.size() ), std::min( maxVertices, static_cast<int>( B3_MAX_HULL_VERTICES ) ) );
        if( hull )
        {
            break;
        }
    }
    if( !hull )
    {
        BRUH( "Physics: couldn't build a convex hull for a mesh collider (degenerate mesh?)" );
        return nullptr;
    }
    m_hulls[InMesh] = hull;
    return hull;
}


const void* PhysicsCore::GetTriangleMesh( Moonlight::MeshData* InMesh )
{
    if( !InMesh || InMesh->CollisionIndices.size() < 3 )
    {
        return nullptr;
    }
    auto found = m_meshes.find( InMesh );
    if( found != m_meshes.end() )
    {
        return found->second;
    }
    std::vector<b3Vec3> vertices;
    vertices.reserve( InMesh->CollisionPositions.size() );
    for( const Vector3& position : InMesh->CollisionPositions )
    {
        vertices.push_back( ToB3( position ) );
    }
    std::vector<int32_t> indices( InMesh->CollisionIndices.begin(), InMesh->CollisionIndices.end() );
    b3MeshDef def = {};
    def.vertices = vertices.data();
    def.stride = sizeof( b3Vec3 );
    def.indices = indices.data();
    def.vertexCount = static_cast<int>( vertices.size() );
    def.triangleCount = static_cast<int>( indices.size() / 3 );
    def.weldVertices = true;
    def.weldTolerance = 0.0001f;
    def.identifyEdges = true;
    b3MeshData* data = b3CreateMesh( &def, nullptr, 0 );
    if( !data )
    {
        BRUH( "Physics: couldn't build a triangle mesh collider" );
        return nullptr;
    }
    m_meshes[InMesh] = data;
    return data;
}


uint64_t PhysicsCore::EnsureGroundBody()
{
    if( !IsBody( m_groundBody ) )
    {
        b3BodyDef def = b3DefaultBodyDef();
        def.type = b3_staticBody;
        m_groundBody = Pack( b3CreateBody( WorldId( m_world ), &def ) );
    }
    return m_groundBody;
}


void PhysicsCore::BuildShapes( BodyRecord& InRecord, const std::vector<ColliderEntry>& InColliders )
{
    const b3BodyId body = Body( InRecord.Body );
    for( uint64_t shape : InRecord.Shapes )
    {
        const b3ShapeId id = Unpack<b3ShapeId>( shape );
        if( b3Shape_IsValid( id ) )
        {
            b3DestroyShape( id, false );
        }
    }
    InRecord.Shapes.clear();
    InRecord.Meshes.clear();

    Transform& owner = InRecord.Owner->GetComponent<Transform>();
    const bool dynamic = b3Body_GetType( body ) == b3_dynamicBody;
    for( const ColliderEntry& entry : InColliders )
    {
        Entity& entity = *entry.Entity.Get();
        Transform& transform = entity.GetComponent<Transform>();
        Vector3 offset;
        Quaternion rotation;
        if( &transform != &owner )
        {
            RelativePose( transform, owner, offset, rotation );
        }
        const b3Transform frame{ ToB3( offset ), ToB3( rotation ) };
        const Vector3 scale = transform.GetWorldScale();
        const Vector3 absScale = AbsVector( scale );

        auto makeDef = [&entity]( const ColliderSettings& InSettings ) {
            b3ShapeDef def = b3DefaultShapeDef();
            def.userData = UserDataFor( entity );
            def.density = InSettings.Density;
            def.baseMaterial.friction = InSettings.Friction;
            def.baseMaterial.restitution = InSettings.Restitution;
            def.filter = FilterFor( entity );
            def.isSensor = InSettings.IsTrigger;
            def.enableSensorEvents = true;
            def.enableContactEvents = true;
            def.updateBodyMass = false;
            return def;
        };
        auto centerIn = [&]( const Vector3& InCenter ) { return b3TransformPoint( frame, ToB3( Scale( InCenter, scale ) ) ); };

        if( BoxCollider* box = Enabled<BoxCollider>( entity ) )
        {
            const b3ShapeDef def = makeDef( *box );
            const Vector3 half = Scale( AbsVector( box->Size ), absScale ) * 0.5f;
            const b3BoxHull hull = b3MakeBoxHull( std::max( half.x, 0.001f ), std::max( half.y, 0.001f ), std::max( half.z, 0.001f ) );
            const b3Transform local{ centerIn( box->Center ), frame.q };
            InRecord.Shapes.push_back( Pack( b3CreateTransformedHullShape( body, &def, &hull.base, local, b3Vec3{ 1.f, 1.f, 1.f } ) ) );
        }
        if( SphereCollider* sphere = Enabled<SphereCollider>( entity ) )
        {
            const b3ShapeDef def = makeDef( *sphere );
            const b3Sphere shape{ centerIn( sphere->Center ), std::max( sphere->Radius * std::max( { absScale.x, absScale.y, absScale.z } ), 0.001f ) };
            InRecord.Shapes.push_back( Pack( b3CreateSphereShape( body, &def, &shape ) ) );
        }
        if( CapsuleCollider* capsule = Enabled<CapsuleCollider>( entity ) )
        {
            const b3ShapeDef def = makeDef( *capsule );
            const int axis = static_cast<int>( capsule->Direction );
            const float axisScale = axis == 0 ? absScale.x : ( axis == 1 ? absScale.y : absScale.z );
            const float radialScale = axis == 0 ? std::max( absScale.y, absScale.z ) : ( axis == 1 ? std::max( absScale.x, absScale.z ) : std::max( absScale.x, absScale.y ) );
            const float radius = std::max( capsule->Radius * radialScale, 0.001f );
            const float half = std::max( capsule->Height * axisScale * 0.5f - radius, 0.f );
            b3Vec3 direction = b3Vec3{ axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f };
            direction = b3RotateVector( frame.q, direction );
            const b3Vec3 center = centerIn( capsule->Center );
            const b3Capsule shape{ b3MulAdd( center, -half, direction ), b3MulAdd( center, half, direction ), radius };
            InRecord.Shapes.push_back( Pack( b3CreateCapsuleShape( body, &def, &shape ) ) );
        }
        if( MeshCollider* meshCollider = Enabled<MeshCollider>( entity ) )
        {
            Mesh* mesh = entity.TryGetComponent<Mesh>();
            Moonlight::MeshData* data = mesh ? mesh->MeshReferece : nullptr;
            const b3ShapeDef def = makeDef( *meshCollider );
            if( meshCollider->Convex || dynamic )
            {
                if( const b3HullData* hull = static_cast<const b3HullData*>( GetHull( data ) ) )
                {
                    InRecord.Meshes.push_back( data );
                    const b3Transform local{ centerIn( meshCollider->Center ), frame.q };
                    InRecord.Shapes.push_back( Pack( b3CreateTransformedHullShape( body, &def, hull, local, ToB3( scale ) ) ) );
                }
            }
            else if( const b3MeshData* triangles = static_cast<const b3MeshData*>( GetTriangleMesh( data ) ) )
            {
                InRecord.Meshes.push_back( data );
                // Triangle meshes own their body, so they sit at the body origin.
                InRecord.Shapes.push_back( Pack( b3CreateMeshShape( body, &def, triangles, ToB3( scale ) ) ) );
            }
        }
    }
}


void PhysicsCore::ApplyBodySettings( BodyRecord& InRecord, Rigidbody* InRigidbody )
{
    const b3BodyId body = Body( InRecord.Body );
    if( !InRigidbody )
    {
        b3Body_SetType( body, b3_staticBody );
        return;
    }
    const b3BodyType type = InRigidbody->Type == BodyType::Static ? b3_staticBody : ( InRigidbody->Type == BodyType::Kinematic ? b3_kinematicBody : b3_dynamicBody );
    if( b3Body_GetType( body ) != type )
    {
        b3Body_SetType( body, type );
    }
    b3Body_SetLinearDamping( body, InRigidbody->LinearDamping );
    b3Body_SetAngularDamping( body, InRigidbody->AngularDamping );
    b3Body_SetGravityScale( body, InRigidbody->GravityScale );
    b3Body_SetBullet( body, InRigidbody->ContinuousCollision );
    b3Body_EnableSleep( body, InRigidbody->CanSleep );
    b3MotionLocks locks = {};
    locks.linearX = InRigidbody->LockPositionX;
    locks.linearY = InRigidbody->LockPositionY;
    locks.linearZ = InRigidbody->LockPositionZ;
    locks.angularX = InRigidbody->LockRotationX;
    locks.angularY = InRigidbody->LockRotationY;
    locks.angularZ = InRigidbody->LockRotationZ;
    b3Body_SetMotionLocks( body, locks );

    if( type == b3_dynamicBody )
    {
        b3Body_ApplyMassFromShapes( body );
        b3MassData mass = b3Body_GetMassData( body );
        if( InRigidbody->Mass > 0.f )
        {
            if( mass.mass > 0.f )
            {
                // Keep the shapes' mass distribution, scaled to the requested mass.
                const float factor = InRigidbody->Mass / mass.mass;
                mass.inertia.cx = b3MulSV( factor, mass.inertia.cx );
                mass.inertia.cy = b3MulSV( factor, mass.inertia.cy );
                mass.inertia.cz = b3MulSV( factor, mass.inertia.cz );
            }
            else
            {
                const float inertia = 0.4f * InRigidbody->Mass * 0.25f;
                mass.inertia = b3Matrix3{ { inertia, 0.f, 0.f }, { 0.f, inertia, 0.f }, { 0.f, 0.f, inertia } };
            }
            mass.mass = InRigidbody->Mass;
            b3Body_SetMassData( body, mass );
        }
    }
}


void PhysicsCore::DestroyRecord( BodyRecord& InRecord )
{
    if( IsBody( InRecord.Body ) )
    {
        b3DestroyBody( Body( InRecord.Body ) );
    }
    if( InRecord.Owner )
    {
        if( Rigidbody* rigidbody = InRecord.Owner->TryGetComponent<Rigidbody>() )
        {
            rigidbody->m_body = 0;
        }
    }
    InRecord.Body = 0;
    InRecord.Shapes.clear();
    InRecord.Meshes.clear();
}


void PhysicsCore::CollectMeshCache()
{
    if( m_hulls.empty() && m_meshes.empty() )
    {
        return;
    }
    std::unordered_map<Moonlight::MeshData*, bool> used;
    for( const auto& [id, record] : m_bodies )
    {
        for( Moonlight::MeshData* mesh : record.Meshes )
        {
            used[mesh] = true;
        }
    }
    for( auto it = m_hulls.begin(); it != m_hulls.end(); )
    {
        if( used.count( it->first ) == 0 )
        {
            b3DestroyHull( static_cast<b3HullData*>( it->second ) );
            it = m_hulls.erase( it );
        }
        else
        {
            ++it;
        }
    }
    // Triangle data is referenced by its shapes, which are gone by now for unused entries.
    for( auto it = m_meshes.begin(); it != m_meshes.end(); )
    {
        if( used.count( it->first ) == 0 )
        {
            b3DestroyMesh( static_cast<b3MeshData*>( it->second ) );
            it = m_meshes.erase( it );
        }
        else
        {
            ++it;
        }
    }
}


void PhysicsCore::SyncBodies()
{
    OPTICK_EVENT( "PhysicsCore::SyncBodies" );
    if( m_world == 0 )
    {
        return;
    }

    // Resolve which body every collider belongs to.
    std::unordered_map<uint64_t, std::vector<ColliderEntry>> collidersByOwner;
    std::unordered_map<uint64_t, EntityHandle> owners;
    for( Entity& entity : GetEntities() )
    {
        if( !entity.IsActiveInHierarchy() || Enabled<CharacterController>( entity ) )
        {
            continue;
        }
        const bool hasRigidbody = Enabled<Rigidbody>( entity ) != nullptr;
        const bool hasCollider = HasAnyCollider( entity );
        if( hasRigidbody )
        {
            owners[entity.GetId().Value()] = entity.GetHandle();
        }
        if( !hasCollider )
        {
            continue;
        }
        Entity* owner = &entity;
        if( !hasRigidbody && !HasTriangleMesh( entity ) )
        {
            // Nearest ancestor with a Rigidbody, else this entity (static).
            for( Transform* parent = entity.GetComponent<Transform>().GetParentTransform(); parent; parent = parent->GetParentTransform() )
            {
                if( parent->Parent && parent->Parent->IsActiveInHierarchy() && Enabled<Rigidbody>( *parent->Parent.Get() ) )
                {
                    owner = parent->Parent.Get();
                    break;
                }
            }
        }
        const uint64_t ownerId = owner->GetId().Value();
        owners[ownerId] = owner->GetHandle();
        collidersByOwner[ownerId].push_back( { entity.GetHandle(), HasTriangleMesh( entity ) } );
    }

    for( auto& [id, record] : m_bodies )
    {
        record.Seen = false;
    }

    const b3WorldId world = WorldId( m_world );
    bool rebuilt = false;
    for( auto& [ownerId, handle] : owners )
    {
        Entity& owner = *handle.Get();
        Transform& transform = owner.GetComponent<Transform>();
        Rigidbody* rigidbody = Enabled<Rigidbody>( owner );
        std::vector<ColliderEntry>& colliders = collidersByOwner[ownerId];
        std::sort( colliders.begin(), colliders.end(), []( const ColliderEntry& a, const ColliderEntry& b ) { return a.Entity->GetId().Value() < b.Entity->GetId().Value(); } );

        BodyRecord& record = m_bodies[ownerId];
        record.Seen = true;
        record.Owner = handle;
        record.HasRigidbody = rigidbody != nullptr;

        const Vector3 position = transform.GetWorldPosition();
        const Quaternion rotation = transform.GetWorldRotation();
        if( !IsBody( record.Body ) )
        {
            b3BodyDef def = b3DefaultBodyDef();
            def.type = b3_staticBody;
            def.position = ToB3( position );
            def.rotation = ToB3( rotation );
            def.userData = UserDataFor( owner );
            record.Body = Pack( b3CreateBody( world, &def ) );
            record.BodySignature = 0;
            record.ShapeSignature = 0;
            record.AppliedPosition = record.PreviousPosition = record.CurrentPosition = position;
            record.AppliedRotation = record.PreviousRotation = record.CurrentRotation = rotation;
        }
        if( rigidbody )
        {
            rigidbody->m_body = record.Body;
        }

        const uint64_t shapeSignature = ComputeShapeSignature( owner, colliders );
        uint64_t bodySignature = 0xb0d1ULL;
        if( rigidbody )
        {
            bodySignature = Mix( bodySignature, static_cast<uint64_t>( rigidbody->Type ) );
            for( float value : { rigidbody->Mass, rigidbody->LinearDamping, rigidbody->AngularDamping, rigidbody->GravityScale } )
            {
                bodySignature = Mix( bodySignature, Bits( value ) );
            }
            const bool flags[] = { rigidbody->ContinuousCollision, rigidbody->CanSleep, rigidbody->LockPositionX, rigidbody->LockPositionY, rigidbody->LockPositionZ, rigidbody->LockRotationX, rigidbody->LockRotationY, rigidbody->LockRotationZ };
            for( bool flag : flags )
            {
                bodySignature = Mix( bodySignature, flag ? 1 : 0 );
            }
        }
        const bool shapesChanged = shapeSignature != record.ShapeSignature;
        if( shapesChanged )
        {
            rebuilt = true;
            BuildShapes( record, colliders );
            record.ShapeSignature = shapeSignature;
        }
        if( shapesChanged || bodySignature != record.BodySignature )
        {
            ApplyBodySettings( record, rigidbody );
            record.BodySignature = bodySignature;
        }
    }

    // Bodies whose owner is gone, inactive or has nothing left to simulate.
    for( auto it = m_bodies.begin(); it != m_bodies.end(); )
    {
        if( !it->second.Seen || !it->second.Owner )
        {
            DestroyRecord( it->second );
            it = m_bodies.erase( it );
            rebuilt = true;
        }
        else
        {
            ++it;
        }
    }
    if( rebuilt )
    {
        CollectMeshCache();
    }
}


void PhysicsCore::SyncJoints()
{
    OPTICK_EVENT( "PhysicsCore::SyncJoints" );
    const b3WorldId world = WorldId( m_world );
    for( Entity& entity : GetEntities() )
    {
        PhysicsJoint* joint = entity.TryGetComponent<PhysicsJoint>();
        if( !joint )
        {
            continue;
        }
        const bool wanted = joint->IsEnabled() && entity.IsActiveInHierarchy() && !joint->m_broken;
        auto ownRecord = m_bodies.find( entity.GetId().Value() );
        uint64_t bodyA = ( wanted && ownRecord != m_bodies.end() ) ? ownRecord->second.Body : 0;
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

        uint64_t signature = Mix( Mix( 0x101ULL, bodyA ), bodyB );
        signature = Mix( Mix( signature, static_cast<uint64_t>( joint->Type ) ), joint->CollideConnected ? 1 : 0 );
        signature = MixVector( MixVector( MixVector( signature, joint->Anchor ), joint->Axis ), joint->ConnectedAnchor );
        for( float value : { joint->LowerLimit, joint->UpperLimit, joint->MotorSpeed, joint->MaxMotorForce, joint->SpringFrequency, joint->SpringDamping, joint->Distance, joint->BreakForce } )
        {
            signature = Mix( signature, Bits( value ) );
        }
        signature = Mix( Mix( Mix( signature, joint->UseLimits ? 1 : 0 ), joint->UseMotor ? 1 : 0 ), joint->UseSpring ? 1 : 0 );

        const b3JointId existing = Unpack<b3JointId>( joint->m_joint );
        const bool valid = joint->m_joint != 0 && b3Joint_IsValid( existing );
        if( valid && signature == joint->m_signature )
        {
            continue;
        }
        if( valid )
        {
            b3DestroyJoint( existing, true );
        }
        joint->m_joint = 0;
        joint->m_signature = signature;
        if( !wanted || !IsBody( bodyA ) || !IsBody( bodyB ) || bodyA == bodyB )
        {
            continue;
        }

        // Joint frame at the anchor, its axis taken from this entity's rotation.
        Transform& transform = entity.GetComponent<Transform>();
        const b3Vec3 anchor = ToB3( transform.GetLocalToWorldMatrix().TransformPoint( joint->Anchor ) );
        Vector3 axis = transform.GetWorldRotation() * joint->Axis;
        axis = axis.LengthSquared() > 1e-8f ? axis.Normalized() : Vector3( 0.f, 1.f, 0.f );
        // Revolute joints turn about the frame's z axis, prismatic joints slide along its x axis.
        const b3Vec3 frameAxis = joint->Type == JointType::Slider ? b3Vec3{ 1.f, 0.f, 0.f } : b3Vec3{ 0.f, 0.f, 1.f };
        const b3Quat frameRotation = RotationBetween( frameAxis, ToB3( axis ) );
        // Box3D measures body B relative to body A: A is the connected body (or the ground), B this
        // entity, so positive motor speeds / translations / limits are about this entity's motion.
        const b3WorldTransform poseA = b3Body_GetTransform( Body( bodyB ) );
        const b3WorldTransform poseB = b3Body_GetTransform( Body( bodyA ) );

        b3JointDef base = {};
        base.bodyIdA = Body( bodyB );
        base.bodyIdB = Body( bodyA );
        base.localFrameA = b3Transform{ b3InvRotateVector( poseA.q, b3Sub( anchor, poseA.p ) ), b3InvMulQuat( poseA.q, frameRotation ) };
        base.localFrameB = b3Transform{ b3InvRotateVector( poseB.q, b3Sub( anchor, poseB.p ) ), b3InvMulQuat( poseB.q, frameRotation ) };
        base.collideConnected = joint->CollideConnected;
        base.userData = UserDataFor( entity );

        const float toRadians = kPi / 180.f;
        b3JointId created = b3_nullJointId;
        switch( joint->Type )
        {
        case JointType::Fixed:
        {
            b3WeldJointDef def = b3DefaultWeldJointDef();
            ApplyJointBase( def.base, base );
            created = b3CreateWeldJoint( world, &def );
            break;
        }
        case JointType::Hinge:
        {
            b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
            ApplyJointBase( def.base, base );
            def.enableLimit = joint->UseLimits;
            def.lowerAngle = joint->LowerLimit * toRadians;
            def.upperAngle = joint->UpperLimit * toRadians;
            def.enableMotor = joint->UseMotor;
            def.motorSpeed = joint->MotorSpeed * toRadians;
            def.maxMotorTorque = joint->MaxMotorForce;
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            created = b3CreateRevoluteJoint( world, &def );
            break;
        }
        case JointType::BallSocket:
        {
            b3SphericalJointDef def = b3DefaultSphericalJointDef();
            ApplyJointBase( def.base, base );
            def.enableConeLimit = joint->UseLimits;
            def.coneAngle = std::max( std::abs( joint->UpperLimit ), 1.f ) * toRadians;
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            created = b3CreateSphericalJoint( world, &def );
            break;
        }
        case JointType::Slider:
        {
            b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
            ApplyJointBase( def.base, base );
            def.enableLimit = joint->UseLimits;
            def.lowerTranslation = joint->LowerLimit;
            def.upperTranslation = joint->UpperLimit;
            def.enableMotor = joint->UseMotor;
            def.motorSpeed = joint->MotorSpeed;
            def.maxMotorForce = joint->MaxMotorForce;
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            created = b3CreatePrismaticJoint( world, &def );
            break;
        }
        case JointType::Distance:
        {
            b3DistanceJointDef def = b3DefaultDistanceJointDef();
            ApplyJointBase( def.base, base );
            // The other end: ConnectedAnchor on the connected body, or a world point (the ground body
            // sits at the origin).
            def.base.localFrameA = b3Transform{ ToB3( joint->ConnectedAnchor ), b3Quat_identity };
            const b3Vec3 otherEnd = b3Add( poseA.p, b3RotateVector( poseA.q, ToB3( joint->ConnectedAnchor ) ) );
            const float current = b3Length( b3Sub( anchor, otherEnd ) );
            def.length = joint->Distance > 0.f ? joint->Distance : current;
            def.enableLimit = joint->UseLimits;
            def.minLength = std::max( joint->LowerLimit, 0.f );
            def.maxLength = std::max( joint->UpperLimit, def.minLength );
            def.enableSpring = joint->UseSpring;
            def.hertz = joint->SpringFrequency;
            def.dampingRatio = joint->SpringDamping;
            created = b3CreateDistanceJoint( world, &def );
            break;
        }
        }
        if( b3Joint_IsValid( created ) && joint->BreakForce > 0.f )
        {
            b3Joint_SetForceThreshold( created, joint->BreakForce );
            b3Joint_SetTorqueThreshold( created, joint->BreakForce );
        }
        joint->m_joint = Pack( created );
    }
}


void PhysicsCore::SyncCharacterBody( CharacterController& InController, Entity& InEntity )
{
    const float half = std::max( InController.Height * 0.5f - InController.Radius, 0.f );
    uint64_t signature = Mix( Mix( 0xcc0ULL, Bits( InController.Radius ) ), Bits( half ) );
    signature = Mix( Mix( signature, InEntity.GetLayer() ), ProjectSettings::Get().GetCollisionMask( InEntity.GetLayer() ) );
    if( IsBody( InController.m_body ) && InController.m_bodySignature == signature )
    {
        return;
    }
    if( IsBody( InController.m_body ) )
    {
        b3DestroyBody( Body( InController.m_body ) );
    }
    // A kinematic capsule that follows the mover, so rigidbodies and queries see the character.
    b3BodyDef def = b3DefaultBodyDef();
    def.type = b3_kinematicBody;
    def.position = ToB3( InController.m_position );
    def.userData = UserDataFor( InEntity );
    const b3BodyId body = b3CreateBody( WorldId( m_world ), &def );
    b3ShapeDef shapeDef = b3DefaultShapeDef();
    shapeDef.userData = UserDataFor( InEntity );
    shapeDef.filter = FilterFor( InEntity );
    shapeDef.enableSensorEvents = true;
    const b3Capsule capsule{ b3Vec3{ 0.f, -half, 0.f }, b3Vec3{ 0.f, half, 0.f }, InController.Radius };
    b3CreateCapsuleShape( body, &shapeDef, &capsule );
    InController.m_body = Pack( body );
    InController.m_bodySignature = signature;
}


void PhysicsCore::StepCharacters( float InDeltaSeconds )
{
    OPTICK_EVENT( "PhysicsCore::StepCharacters" );
    const b3WorldId world = WorldId( m_world );
    const Vector3 gravity = FromB3( b3World_GetGravity( world ) );
    for( Entity& entity : GetEntities() )
    {
        CharacterController* controller = Enabled<CharacterController>( entity );
        if( !controller || !entity.IsActiveInHierarchy() )
        {
            if( controller && IsBody( controller->m_body ) )
            {
                b3DestroyBody( Body( controller->m_body ) );
                controller->m_body = 0;
            }
            continue;
        }
        CharacterController& c = *controller;
        Transform& transform = entity.GetComponent<Transform>();
        const Vector3 transformCenter = transform.GetWorldPosition() - c.Center;
        if( !c.m_hasPosition || ( transformCenter - c.m_appliedCenter ).LengthSquared() > 1e-6f )
        {
            // First step, or something else moved the Transform: start from there.
            c.m_position = c.m_previousPosition = c.m_appliedCenter = transformCenter;
            c.m_hasPosition = true;
        }
        SyncCharacterBody( c, entity );
        const b3BodyId ownBody = Body( c.m_body );

        // Velocity: steer horizontally, fall, jump.
        const Vector3 desired = c.m_moveInput * c.MaxSpeed;
        Vector3 horizontal( c.m_velocity.x, 0.f, c.m_velocity.z );
        const float acceleration = c.Acceleration * ( c.m_grounded ? 1.f : c.AirControl ) * InDeltaSeconds;
        const Vector3 change = desired - horizontal;
        const float changeLength = change.Length();
        horizontal = changeLength <= acceleration || changeLength < 1e-6f ? desired : horizontal + change * ( acceleration / changeLength );
        float vertical = c.m_velocity.y;
        const float g = gravity.y * c.GravityScale;
        bool jumped = false;
        if( c.m_wantsJump && c.m_grounded )
        {
            vertical = std::sqrt( 2.f * std::abs( g ) * std::max( c.JumpHeight, 0.f ) );
            jumped = true;
        }
        else if( c.m_grounded && vertical < 0.f )
        {
            vertical = 0.f;
        }
        vertical += g * InDeltaSeconds;
        c.m_wantsJump = false;
        c.m_velocity = Vector3( horizontal.x, vertical, horizontal.z );

        // Mover: collide, solve against the contact planes, sweep, repeat.
        const float half = std::max( c.Height * 0.5f - c.Radius, 0.f );
        const b3Capsule mover{ b3Vec3{ 0.f, -half, 0.f }, b3Vec3{ 0.f, half, 0.f }, c.Radius };
        const b3QueryFilter filter = QueryFilterFor( ProjectSettings::Get().GetCollisionMask( entity.GetLayer() ) );
        struct MoverContext
        {
            b3BodyId Self;
            float MinGroundY;
            std::vector<b3CollisionPlane> Planes;
            bool Grounded = false;
            Vector3 GroundNormal = Vector3( 0.f, 1.f, 0.f );
            std::vector<b3ShapeId> Pushed;
        } context{ ownBody, std::cos( c.SlopeLimit * kPi / 180.f ) };
        auto ignoreSelf = []( b3ShapeId shape, void* ctx ) -> bool {
            const MoverContext* self = static_cast<const MoverContext*>( ctx );
            return !B3_ID_EQUALS( b3Shape_GetBody( shape ), self->Self );
        };
        auto collectPlanes = []( b3ShapeId shape, const b3PlaneResult* planes, int count, void* ctx ) -> bool {
            MoverContext* self = static_cast<MoverContext*>( ctx );
            if( B3_ID_EQUALS( b3Shape_GetBody( shape ), self->Self ) || b3Shape_IsSensor( shape ) )
            {
                return true;
            }
            for( int i = 0; i < count; ++i )
            {
                const b3Vec3 normal = planes[i].plane.normal;
                self->Planes.push_back( b3CollisionPlane{ planes[i].plane, FLT_MAX, 0.f, true } );
                if( normal.y >= self->MinGroundY )
                {
                    self->Grounded = true;
                    self->GroundNormal = Vector3( normal.x, normal.y, normal.z );
                }
            }
            if( b3Body_GetType( b3Shape_GetBody( shape ) ) == b3_dynamicBody )
            {
                self->Pushed.push_back( shape );
            }
            return true;
        };

        const Vector3 delta = c.m_velocity * InDeltaSeconds + c.m_pendingMove;
        const Vector3 intended = c.m_velocity;
        c.m_pendingMove = Vector3();
        b3Vec3 position = ToB3( c.m_position );
        const b3Vec3 target = b3Add( position, ToB3( delta ) );
        const bool wasGrounded = c.m_grounded;
        auto collideAt = [&]( b3Vec3 InPosition ) {
            context.Planes.clear();
            context.Grounded = false;
            b3World_CollideMover( world, InPosition, &mover, filter, collectPlanes, &context );
        };
        for( int iteration = 0; iteration < 5; ++iteration )
        {
            collideAt( position );
            const b3PlaneSolverResult solved = b3SolvePlanes( b3Sub( target, position ), context.Planes.data(), static_cast<int>( context.Planes.size() ) );
            const float fraction = b3World_CastMover( world, position, &mover, solved.delta, filter, ignoreSelf, &context );
            const b3Vec3 step = b3MulSV( fraction, solved.delta );
            position = b3Add( position, step );
            if( b3Dot( step, step ) < 1e-8f )
            {
                break;
            }
        }

        // Contact state where we ended up: grounding, and velocity lost into walls / the floor.
        collideAt( position );
        bool grounded = context.Grounded && !jumped;
        if( !grounded && wasGrounded && !jumped && c.GroundSnap > 0.f && c.m_velocity.y <= 0.f )
        {
            // Stay on the ground over small drops and down slopes.
            const b3Vec3 down{ 0.f, -c.GroundSnap, 0.f };
            const float fraction = b3World_CastMover( world, position, &mover, down, filter, ignoreSelf, &context );
            if( fraction < 1.f )
            {
                position = b3MulAdd( position, fraction, down );
                collideAt( position );
                grounded = context.Grounded;
            }
        }
        c.m_velocity = FromB3( b3ClipVector( ToB3( c.m_velocity ), context.Planes.data(), static_cast<int>( context.Planes.size() ) ) );
        c.m_grounded = grounded;
        c.m_groundNormal = grounded ? context.GroundNormal : Vector3( 0.f, 1.f, 0.f );

        // Shove dynamic bodies we walked into (once each, along the intended horizontal velocity).
        if( c.PushStrength > 0.f && !context.Pushed.empty() )
        {
            const Vector3 push( intended.x, 0.f, intended.z );
            std::vector<b3BodyId> pushed;
            for( b3ShapeId shape : context.Pushed )
            {
                const b3BodyId other = b3Shape_GetBody( shape );
                if( std::any_of( pushed.begin(), pushed.end(), [other]( b3BodyId id ) { return B3_ID_EQUALS( id, other ); } ) )
                {
                    continue;
                }
                pushed.push_back( other );
                b3Body_ApplyLinearImpulseToCenter( other, ToB3( push * ( c.PushStrength * InDeltaSeconds * b3Body_GetMass( other ) ) ), true );
            }
        }

        c.m_previousPosition = c.m_position;
        c.m_position = FromB3( position );
        b3WorldTransform bodyTarget{ position, b3Quat_identity };
        b3Body_SetTargetTransform( ownBody, bodyTarget, InDeltaSeconds, true );
    }
}


void PhysicsCore::FixedUpdate( const UpdateContext& inUpdateContext )
{
    if( !m_simulating || m_world == 0 )
    {
        return;
    }
    OPTICK_CATEGORY( "PhysicsCore::FixedUpdate", Optick::Category::Physics );
    const float dt = inUpdateContext.GetFixedDeltaTime();
    SyncBodies();
    SyncJoints();

    // Bodies whose Transform was moved by something else (scripts, the gizmo) follow it.
    for( auto& [id, record] : m_bodies )
    {
        if( !IsBody( record.Body ) || !record.Owner )
        {
            continue;
        }
        const b3BodyId body = Body( record.Body );
        Transform& transform = record.Owner->GetComponent<Transform>();
        const Vector3 position = transform.GetWorldPosition();
        const Quaternion rotation = transform.GetWorldRotation();
        const b3BodyType type = b3Body_GetType( body );
        Rigidbody* rigidbody = record.Owner->TryGetComponent<Rigidbody>();
        if( type == b3_kinematicBody )
        {
            Vector3 targetPosition = position;
            Quaternion targetRotation = rotation;
            if( rigidbody && rigidbody->m_hasPendingMove )
            {
                // The body glides there over this step; the Transform takes the new pose too.
                targetPosition = rigidbody->m_movePosition;
                targetRotation = rigidbody->m_moveRotation;
                rigidbody->m_hasPendingMove = false;
                transform.SetWorldPosition( targetPosition );
                transform.SetWorldRotation( targetRotation );
                targetPosition = transform.GetWorldPosition();
                targetRotation = transform.GetWorldRotation();
            }
            b3Body_SetTargetTransform( body, b3WorldTransform{ ToB3( targetPosition ), ToB3( targetRotation ) }, dt, true );
            record.AppliedPosition = targetPosition;
            record.AppliedRotation = targetRotation;
        }
        else if( !SamePose( position, rotation, record.AppliedPosition, record.AppliedRotation ) )
        {
            b3Body_SetTransform( body, ToB3( position ), ToB3( rotation ) );
            record.AppliedPosition = record.PreviousPosition = record.CurrentPosition = position;
            record.AppliedRotation = record.PreviousRotation = record.CurrentRotation = rotation;
        }
    }

    StepCharacters( dt );

    const auto start = std::chrono::steady_clock::now();
    m_taskCount = 0;
    b3World_Step( WorldId( m_world ), dt, kSubSteps );
    m_lastStepMs = std::chrono::duration<float, std::milli>( std::chrono::steady_clock::now() - start ).count();

    for( auto& [id, record] : m_bodies )
    {
        if( !IsBody( record.Body ) )
        {
            continue;
        }
        const b3BodyId body = Body( record.Body );
        if( b3Body_GetType( body ) != b3_dynamicBody )
        {
            continue;
        }
        const b3WorldTransform pose = b3Body_GetTransform( body );
        record.PreviousPosition = record.CurrentPosition;
        record.PreviousRotation = record.CurrentRotation;
        record.CurrentPosition = FromB3( pose.p );
        record.CurrentRotation = FromB3( pose.q );
    }

    ProcessEvents();
}


void PhysicsCore::ProcessEvents()
{
    World& world = GetWorld();
    const b3WorldId worldId = WorldId( m_world );
    auto entityOf = [&world]( b3ShapeId shape ) -> EntityHandle {
        return b3Shape_IsValid( shape ) ? EntityFromUserData( world, b3Shape_GetUserData( shape ) ) : EntityHandle();
    };

    const b3ContactEvents contacts = b3World_GetContactEvents( worldId );
    for( int i = 0; i < contacts.beginCount; ++i )
    {
        const b3ContactBeginTouchEvent& touch = contacts.beginEvents[i];
        CollisionEvent event;
        event.A = entityOf( touch.shapeIdA );
        event.B = entityOf( touch.shapeIdB );
        event.State = CollisionEvent::Phase::Enter;
        if( b3Contact_IsValid( touch.contactId ) )
        {
            const b3ContactData data = b3Contact_GetData( touch.contactId );
            if( data.manifoldCount > 0 && data.manifolds[0].pointCount > 0 )
            {
                const b3Vec3 center = b3Body_GetWorldCenter( b3Shape_GetBody( touch.shapeIdA ) );
                event.Point = FromB3( b3Add( center, data.manifolds[0].points[0].anchorA ) );
                event.Normal = FromB3( data.manifolds[0].normal );
            }
        }
        m_pendingEvents.push_back( event );
    }
    for( int i = 0; i < contacts.endCount; ++i )
    {
        CollisionEvent event;
        event.A = entityOf( contacts.endEvents[i].shapeIdA );
        event.B = entityOf( contacts.endEvents[i].shapeIdB );
        event.State = CollisionEvent::Phase::Exit;
        m_pendingEvents.push_back( event );
    }

    const b3SensorEvents sensors = b3World_GetSensorEvents( worldId );
    for( int i = 0; i < sensors.beginCount; ++i )
    {
        CollisionEvent event;
        event.A = entityOf( sensors.beginEvents[i].sensorShapeId );
        event.B = entityOf( sensors.beginEvents[i].visitorShapeId );
        event.State = CollisionEvent::Phase::Enter;
        event.IsTrigger = true;
        m_pendingEvents.push_back( event );
    }
    for( int i = 0; i < sensors.endCount; ++i )
    {
        CollisionEvent event;
        event.A = entityOf( sensors.endEvents[i].sensorShapeId );
        event.B = entityOf( sensors.endEvents[i].visitorShapeId );
        event.State = CollisionEvent::Phase::Exit;
        event.IsTrigger = true;
        m_pendingEvents.push_back( event );
    }

    // Joints pulled past their break force.
    const b3JointEvents joints = b3World_GetJointEvents( worldId );
    for( int i = 0; i < joints.count; ++i )
    {
        const b3JointId id = joints.jointEvents[i].jointId;
        EntityHandle owner = EntityFromUserData( world, joints.jointEvents[i].userData );
        if( owner )
        {
            if( PhysicsJoint* joint = owner->TryGetComponent<PhysicsJoint>(); joint && joint->m_joint == Pack( id ) )
            {
                joint->m_broken = true;
                joint->m_joint = 0;
            }
        }
        if( b3Joint_IsValid( id ) )
        {
            b3DestroyJoint( id, true );
        }
    }

    // Deliver after reading everything, so handlers can freely change the world.
    std::vector<CollisionEvent> events;
    events.swap( m_pendingEvents );
    for( CollisionEvent& event : events )
    {
        event.Fire();
    }
}


void PhysicsCore::WritePoses( float InAlpha )
{
    for( auto& [id, record] : m_bodies )
    {
        if( !IsBody( record.Body ) || !record.Owner || b3Body_GetType( Body( record.Body ) ) != b3_dynamicBody )
        {
            continue;
        }
        Rigidbody* rigidbody = record.Owner->TryGetComponent<Rigidbody>();
        const bool interpolate = rigidbody && rigidbody->Interpolate;
        const Vector3 position = interpolate ? record.PreviousPosition + ( record.CurrentPosition - record.PreviousPosition ) * InAlpha : record.CurrentPosition;
        const Quaternion rotation = interpolate ? Nlerp( record.PreviousRotation, record.CurrentRotation, InAlpha ) : record.CurrentRotation;
        Transform& transform = record.Owner->GetComponent<Transform>();
        transform.SetWorldPosition( position );
        transform.SetWorldRotation( rotation );
        // Read back so later comparisons see exactly what the Transform holds.
        record.AppliedPosition = transform.GetWorldPosition();
        record.AppliedRotation = transform.GetWorldRotation();
    }

    for( Entity& entity : GetEntities() )
    {
        CharacterController* controller = Enabled<CharacterController>( entity );
        if( !controller || !controller->m_hasPosition )
        {
            continue;
        }
        const Vector3 center = controller->m_previousPosition + ( controller->m_position - controller->m_previousPosition ) * InAlpha;
        Transform& transform = entity.GetComponent<Transform>();
        transform.SetWorldPosition( center + controller->Center );
        // What the Transform now holds counts as ours (see StepCharacters).
        controller->m_appliedCenter = transform.GetWorldPosition() - controller->Center;
    }
}


void PhysicsCore::Update( const UpdateContext& inUpdateContext )
{
    if( m_world == 0 )
    {
        return;
    }
    OPTICK_CATEGORY( "PhysicsCore::Update", Optick::Category::Physics );
    if( m_simulating )
    {
        WritePoses( inUpdateContext.GetInterpolationAlpha() );
    }
    else
    {
        // Edit mode: bodies follow their Transforms, components and settings can change freely.
        SetGravity( ProjectSettings::Get().Gravity );
        SyncBodies();
        SyncJoints();
        for( auto& [id, record] : m_bodies )
        {
            if( !IsBody( record.Body ) || !record.Owner )
            {
                continue;
            }
            Transform& transform = record.Owner->GetComponent<Transform>();
            const Vector3 position = transform.GetWorldPosition();
            const Quaternion rotation = transform.GetWorldRotation();
            if( !SamePose( position, rotation, record.AppliedPosition, record.AppliedRotation ) )
            {
                b3Body_SetTransform( Body( record.Body ), ToB3( position ), ToB3( rotation ) );
                record.AppliedPosition = record.PreviousPosition = record.CurrentPosition = position;
                record.AppliedRotation = record.PreviousRotation = record.CurrentRotation = rotation;
            }
        }
    }

    if( DebugDrawEnabled )
    {
        b3DebugDraw draw = PhysicsDebugDraw::Make();
        b3World_Draw( WorldId( m_world ), &draw, ~0ull );
    }
}


void PhysicsCore::SetGravity( const Vector3& InGravity )
{
    if( m_world != 0 )
    {
        b3World_SetGravity( WorldId( m_world ), ToB3( InGravity ) );
    }
}


Vector3 PhysicsCore::GetGravity() const
{
    return m_world != 0 ? FromB3( b3World_GetGravity( WorldId( m_world ) ) ) : ProjectSettings::Get().Gravity;
}


bool PhysicsCore::Raycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask )
{
    if( m_world == 0 || InDirection.LengthSquared() < 1e-12f )
    {
        return false;
    }
    const Vector3 direction = InDirection.Normalized();
    const b3RayResult result = b3World_CastRayClosest( WorldId( m_world ), ToB3( InOrigin ), ToB3( direction * InMaxDistance ), QueryFilterFor( InLayerMask ) );
    if( !result.hit )
    {
        return false;
    }
    OutHit.Entity = EntityFromUserData( GetWorld(), b3Shape_GetUserData( result.shapeId ) );
    OutHit.Position = FromB3( result.point );
    OutHit.Normal = FromB3( result.normal );
    OutHit.Fraction = result.fraction;
    OutHit.Distance = result.fraction * InMaxDistance;
    return true;
}


bool PhysicsCore::Linecast( const Vector3& InStart, const Vector3& InEnd, RaycastHit& OutHit, uint32_t InLayerMask )
{
    const Vector3 delta = InEnd - InStart;
    return Raycast( InStart, delta, delta.Length(), OutHit, InLayerMask );
}


std::vector<RaycastHit> PhysicsCore::RaycastAll( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, uint32_t InLayerMask )
{
    struct Context
    {
        World* GameWorld;
        float MaxDistance;
        std::vector<RaycastHit> Hits;
    } context{ &GetWorld(), InMaxDistance, {} };
    if( m_world == 0 || InDirection.LengthSquared() < 1e-12f )
    {
        return {};
    }
    const Vector3 direction = InDirection.Normalized();
    b3World_CastRay( WorldId( m_world ), ToB3( InOrigin ), ToB3( direction * InMaxDistance ), QueryFilterFor( InLayerMask ),
        []( b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int, void* ctx ) -> float {
            Context* self = static_cast<Context*>( ctx );
            RaycastHit hit;
            hit.Entity = EntityFromUserData( *self->GameWorld, b3Shape_GetUserData( shape ) );
            hit.Position = FromB3( point );
            hit.Normal = FromB3( normal );
            hit.Fraction = fraction;
            hit.Distance = fraction * self->MaxDistance;
            self->Hits.push_back( hit );
            return 1.f;
        }, &context );
    std::sort( context.Hits.begin(), context.Hits.end(), []( const RaycastHit& a, const RaycastHit& b ) { return a.Distance < b.Distance; } );
    return context.Hits;
}


bool PhysicsCore::SphereCast( const Vector3& InOrigin, float InRadius, const Vector3& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask )
{
    if( m_world == 0 || InDirection.LengthSquared() < 1e-12f )
    {
        return false;
    }
    struct Context
    {
        World* GameWorld;
        float MaxDistance;
        bool Hit = false;
        RaycastHit Result;
    } context{ &GetWorld(), InMaxDistance };
    const b3Vec3 center{ 0.f, 0.f, 0.f };
    const b3ShapeProxy proxy{ &center, 1, InRadius };
    b3World_CastShape( WorldId( m_world ), ToB3( InOrigin ), &proxy, ToB3( InDirection.Normalized() * InMaxDistance ), QueryFilterFor( InLayerMask ),
        []( b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int, void* ctx ) -> float {
            Context* self = static_cast<Context*>( ctx );
            self->Hit = true;
            self->Result.Entity = EntityFromUserData( *self->GameWorld, b3Shape_GetUserData( shape ) );
            self->Result.Position = FromB3( point );
            self->Result.Normal = FromB3( normal );
            self->Result.Fraction = fraction;
            self->Result.Distance = fraction * self->MaxDistance;
            return fraction;   // keep the closest
        }, &context );
    if( context.Hit )
    {
        OutHit = context.Result;
    }
    return context.Hit;
}


std::vector<EntityHandle> PhysicsCore::OverlapSphere( const Vector3& InCenter, float InRadius, uint32_t InLayerMask )
{
    std::vector<EntityHandle> results;
    if( m_world == 0 )
    {
        return results;
    }
    struct Context
    {
        World* GameWorld;
        std::vector<EntityHandle>* Results;
    } context{ &GetWorld(), &results };
    const b3Vec3 center{ 0.f, 0.f, 0.f };
    const b3ShapeProxy proxy{ &center, 1, InRadius };
    b3World_OverlapShape( WorldId( m_world ), ToB3( InCenter ), &proxy, QueryFilterFor( InLayerMask ),
        []( b3ShapeId shape, void* ctx ) -> bool {
            Context* self = static_cast<Context*>( ctx );
            EntityHandle entity = EntityFromUserData( *self->GameWorld, b3Shape_GetUserData( shape ) );
            if( entity && std::find( self->Results->begin(), self->Results->end(), entity ) == self->Results->end() )
            {
                self->Results->push_back( entity );
            }
            return true;
        }, &context );
    return results;
}


std::vector<EntityHandle> PhysicsCore::OverlapBox( const Vector3& InCenter, const Vector3& InHalfExtents, const Quaternion& InRotation, uint32_t InLayerMask )
{
    std::vector<EntityHandle> results;
    if( m_world == 0 )
    {
        return results;
    }
    struct Context
    {
        World* GameWorld;
        std::vector<EntityHandle>* Results;
    } context{ &GetWorld(), &results };
    b3Vec3 corners[8];
    const b3Quat rotation = ToB3( InRotation );
    for( int i = 0; i < 8; ++i )
    {
        corners[i] = b3RotateVector( rotation, b3Vec3{ ( i & 1 ) ? InHalfExtents.x : -InHalfExtents.x, ( i & 2 ) ? InHalfExtents.y : -InHalfExtents.y, ( i & 4 ) ? InHalfExtents.z : -InHalfExtents.z } );
    }
    const b3ShapeProxy proxy{ corners, 8, 0.f };
    b3World_OverlapShape( WorldId( m_world ), ToB3( InCenter ), &proxy, QueryFilterFor( InLayerMask ),
        []( b3ShapeId shape, void* ctx ) -> bool {
            Context* self = static_cast<Context*>( ctx );
            EntityHandle entity = EntityFromUserData( *self->GameWorld, b3Shape_GetUserData( shape ) );
            if( entity && std::find( self->Results->begin(), self->Results->end(), entity ) == self->Results->end() )
            {
                self->Results->push_back( entity );
            }
            return true;
        }, &context );
    return results;
}


#if USING( ME_EDITOR )
void PhysicsCore::OnEditorInspect()
{
    Base::OnEditorInspect();
    ImGui::Text( "Bodies: %zu  %s", m_bodies.size(), m_simulating ? "simulating" : "edit mode" );
    if( m_world != 0 )
    {
        const b3Counters counters = b3World_GetCounters( WorldId( m_world ) );
        ImGui::Text( "Shapes: %d  Contacts: %d  Awake: %d", counters.shapeCount, counters.contactCount, b3World_GetAwakeBodyCount( WorldId( m_world ) ) );
    }
    ImGui::Text( "Step: %.2f ms", m_lastStepMs );
    ImGui::Checkbox( "Draw Physics", &DebugDrawEnabled );
    Vector3 gravity = GetGravity();
    if( ImGui::DragFloat3( "Gravity", &gravity.x, 0.05f ) )
    {
        SetGravity( gravity );
    }
}
#endif
