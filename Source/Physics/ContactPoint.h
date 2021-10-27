#pragma once
#include <btBulletDynamicsCommon.h>

// Internal usage only (well, probably)
struct MyContactPoint
{
	const btCollisionObject* bodyB;
	btVector3 localSpaceContactPoint;
	btVector3 worldSpaceContactPoint;
	btVector3 worldSpaceContactNormal;	// from thisBodyA towards bodyB
	btScalar penetrationDistance;
	btScalar appliedImpulse;
	btScalar numContactPoints;
	bool startColliding;

	MyContactPoint() {}

	SIMD_FORCE_INLINE void Set(const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse, const btScalar numContactPoints, const bool averageAllPoints) {
		this->bodyB = bodyB;
		this->localSpaceContactPoint = localSpaceContactPoint;
		this->worldSpaceContactPoint = worldSpaceContactPoint;
		this->worldSpaceContactNormal = worldSpaceContactNormal;
		this->penetrationDistance = penetrationDistance;
		this->appliedImpulse = appliedImpulse;
		this->numContactPoints = (averageAllPoints ? numContactPoints : 1.0f);
	}

	SIMD_FORCE_INLINE void Add(const MyContactPoint& P, const bool averageAllPoints = false) {
		btScalar newNumContactPoints = P.numContactPoints;
		if (!averageAllPoints) {
			newNumContactPoints = numContactPoints + 1.0f;
		}
		btScalar newNumContactPointsInverse = (newNumContactPoints != 0 ? 1.0f / newNumContactPoints : 1.0f);

		localSpaceContactPoint = (localSpaceContactPoint * numContactPoints + P.localSpaceContactPoint * P.numContactPoints) * newNumContactPointsInverse;
		worldSpaceContactPoint = (worldSpaceContactPoint * numContactPoints + P.worldSpaceContactPoint * P.numContactPoints) * newNumContactPointsInverse;
		worldSpaceContactNormal = (worldSpaceContactNormal * numContactPoints + P.worldSpaceContactNormal * P.numContactPoints) * newNumContactPointsInverse;
		penetrationDistance = (penetrationDistance * numContactPoints + P.penetrationDistance * P.numContactPoints) * newNumContactPointsInverse;
		// The applied impulse must be averaged ?
		appliedImpulse = (appliedImpulse * numContactPoints + P.appliedImpulse * P.numContactPoints) * newNumContactPointsInverse;
		// or just summed:
		//appliedImpulse+=P.appliedImpulse;	
		// P.S. This must be changed in btRigidBodyWithEvents::PerformCollisionDetection(...) as well

		numContactPoints = newNumContactPoints;
		//startColliding = P.startColliding;	// NO! THIS MUST BE LEFT OUT
	}

	SIMD_FORCE_INLINE static int FindRigidBodyIn(const btCollisionObject* body, const btAlignedObjectArray< MyContactPoint >& list) {
		int size = list.size();
		for (int t = 0; t < size; t++) {
			if (body == list[t].bodyB) return t;
		}
		return -1;
	}

	static int AddAverageContactPointTo(const MyContactPoint& CP, btAlignedObjectArray< MyContactPoint >& list, const bool averageAllPoints = false) {
		int index = FindRigidBodyIn(CP.bodyB, list);
		if (index == -1) {
			list.push_back(CP);
			return index;
		}
		else {
			list[index].Add(CP, averageAllPoints);
			return index;
		}
		return index;
	}
};