#pragma once
#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/Dynamics/btActionInterface.h>

class btRigidBodyWithEventsEventDelegates;

class ICollisionEvents
	: public btActionInterface
{
public:
	virtual void OnCollisionStart(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse) = 0;
	virtual void OnCollisionContinue(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse) = 0;
	virtual void OnCollisionStop(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse) = 0;

	static void SetInstance(ICollisionEvents* instance, btDynamicsWorld* btWorld);

	virtual void updateAction(btCollisionWorld* collisionWorld, btScalar deltaTimeStep);

	virtual void debugDraw(btIDebugDraw* debugDrawer) {}

	virtual ~ICollisionEvents() {}
};