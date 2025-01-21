#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Physics/RaycastHit.h"
#include "Physics/RigidBodyWithCollisionEvents.h"

#if USING( ME_PHYSICS_3D )
#include <btBulletDynamicsCommon.h>
#endif

class DebugDrawer;
class btDiscreteDynamicsWorld;

class PhysicsCore
    : public Core<PhysicsCore>
#if USING( ME_PHYSICS_3D )
    , public ICollisionEvents
#endif
{
    friend class Core<PhysicsCore>;
public:
#if USING( ME_PHYSICS_3D )
    btDiscreteDynamicsWorld* PhysicsWorld = nullptr;
    btVector3 Gravity;
#endif

    PhysicsCore();
    ~PhysicsCore();

#if USING( ME_PHYSICS_3D )
    static inline btVector3 ToBulletVector( const Vector3& other )
    {
        return btVector3( other.x, other.y, other.z );
    }
#endif

    // Separate init from construction code.
    virtual void Init() final;

    // Each core must update each loop
    virtual void Update( const UpdateContext& inUpdateContext ) final;

    virtual void OnEntityAdded( Entity& NewEntity ) final;
    virtual void OnEntityRemoved( Entity& NewEntity ) final;

    virtual void OnDrawGuizmo( DebugDrawer* inDrawer ) final;

    void InitRigidbody( Rigidbody& RigidbodyComponent, Transform& TransformComponent );

    bool Raycast( const Vector3& InPosition, const Vector3& InDirection, RaycastHit& OutHit );

#if USING( ME_PHYSICS_3D )
    virtual void OnCollisionStart( const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse );
    virtual void OnCollisionContinue( const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse );
    virtual void OnCollisionStop( const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse );
#endif
};

ME_REGISTER_CORE( PhysicsCore )