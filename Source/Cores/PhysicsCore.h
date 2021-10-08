#pragma once
#include "ECS/Core.h"
#include <btBulletDynamicsCommon.h>
#include "Math/Vector3.h"
#include "ECS/CoreDetail.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Physics/RaycastHit.h"
#include "Physics/RigidBodyWithCollisionEvents.h"

class DebugDrawer;

class PhysicsCore
	: public Core<PhysicsCore>
	, public ICollisionEvents
{
	friend class Core<PhysicsCore>;
public:
	btDiscreteDynamicsWorld* PhysicsWorld = nullptr;
	btVector3 Gravity;

	PhysicsCore();
	~PhysicsCore();

	static inline btVector3 ToBulletVector(const Vector3& other)
	{
		return btVector3(other.x, other.y, other.z);
	}

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
	virtual void OnEntityRemoved(Entity& NewEntity) final;

	virtual void OnDrawGuizmo(DebugDrawer* inDrawer) final;

	void InitRigidbody(Rigidbody& RigidbodyComponent, Transform& TransformComponent);

	bool Raycast(const Vector3& InPosition, const Vector3& InDirection, RaycastHit& OutHit);

	virtual void OnCollisionStart(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse);
	virtual void OnCollisionContinue(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse);
	virtual void OnCollisionStop(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse);
};

ME_REGISTER_CORE(PhysicsCore)