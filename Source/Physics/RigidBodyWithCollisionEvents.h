/*
Created: 11/10/2009
*/

#pragma once

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/NarrowPhaseCollision/btManifoldPoint.h>
#include "ContactPoint.h"
#include "ICollisionEventReciever.h"
#include <BulletCollision/CollisionDispatch/btGhostObject.h>

// USAGE:
/*
1) Inherit your application class from ICollisionEvents.
2) Add to your class the ICollisionEvents abstract methods:
	// ICollisionEvents
	virtual void  OnCollisionStart(btRigidBodyWithEvents* thisBodyA,btCollisionObject* bodyB,const btVector3& localSpaceContactPoint,const btVector3& worldSpaceContactPoint,const btVector3& worldSpaceContactNormal,const btScalar penetrationDistance,const btScalar appliedImpulse);
	virtual void  OnCollisionContinue(btRigidBodyWithEvents* thisBodyA,btCollisionObject* bodyB,const btVector3& localSpaceContactPoint,const btVector3& worldSpaceContactPoint,const btVector3& worldSpaceContactNormal,const btScalar penetrationDistance,const btScalar appliedImpulse);
	virtual void  OnCollisionStop(btRigidBodyWithEvents* thisBodyA,btCollisionObject* bodyB,const btVector3& localSpaceContactPoint,const btVector3& worldSpaceContactPoint,const btVector3& worldSpaceContactNormal,const btScalar penetrationDistance,const btScalar appliedImpulse);
	//--------------------
3) As soon as the Bullet World has been created, call the static method: ICollisionEvents::SetInstance(ICollisionEvents* instance,btDynamicsWorld* btWorld) like this:
	m_dynamicsWorld = new btDiscreteDynamicsWorld(m_dispatcher,m_broadphase,m_solver,m_collisionConfiguration);
	// ICollisionEvents (MANDATORY)--------------
	ICollisionEvents::SetInstance(this,m_dynamicsWorld);	// "this" is good if this snippet is inside a method of the application class that inherits from ICollisionEvents
	// ----------------------------------------------
4) When you want to monitor a rigid body for collisions, create it as a btRigidBodyWithEvents instead of btRigidBody, and call:
	body->setMonitorCollisions(true);
*/


/// TODO: Clean up and refactor this huge mess a bit, otherwise in a pair of weeks I won't be able to understand it anymore myself...



// This is just a partial part of btRigidBodyWithEvents (i.e. could just be merged to it):
///TODO: instead port everything we can from btRigidBodyWithEvents to this class, so that users
// may implement their own version of btRigidBodyWithEvents more easily (well, they can still inherit directly from
// it. Anyway the event handlers must take this class... it's not that easy at all for users to inglobe it)
class btRigidBodyWithEventsEventDelegates {
protected:
	static ICollisionEvents* eventDispatcher;
	friend class ICollisionEvents;
	friend class btRigidBodyWithEvents;

	btAlignedObjectArray< MyContactPoint >* pLastCollidingBodies;	// A list of colliding bodies in the last frame
	btAlignedObjectArray< MyContactPoint >* pNewCollidingBodies;	// A list of colliding bodies in the this frame

	bool _monitorCollisions;
	//------------OnCollisionEvent-------------
	SIMD_FORCE_INLINE void  OnCollisionStart(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse) {
		btAssert(eventDispatcher != NULL);
		eventDispatcher->OnCollisionStart(thisBodyA, bodyB, localSpaceContactPoint, worldSpaceContactPoint, worldSpaceContactNormal, penetrationDistance, appliedImpulse);
	}
	SIMD_FORCE_INLINE void  OnCollisionContinue(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse) {
		btAssert(eventDispatcher != NULL);
		eventDispatcher->OnCollisionContinue(thisBodyA, bodyB, localSpaceContactPoint, worldSpaceContactPoint, worldSpaceContactNormal, penetrationDistance, appliedImpulse);
	}
	SIMD_FORCE_INLINE void  OnCollisionStop(const btRigidBodyWithEventsEventDelegates* thisBodyA, const btCollisionObject* bodyB, const btVector3& localSpaceContactPoint, const btVector3& worldSpaceContactPoint, const btVector3& worldSpaceContactNormal, const btScalar penetrationDistance, const btScalar appliedImpulse) {
		btAssert(eventDispatcher != NULL);
		eventDispatcher->OnCollisionStop(thisBodyA, bodyB, localSpaceContactPoint, worldSpaceContactPoint, worldSpaceContactNormal, penetrationDistance, appliedImpulse);
	}

	//region  Completely Optional And Unused Stuff 
	SIMD_FORCE_INLINE MyContactPoint getContactPointForIndex(int index, bool* isOperationOKOut = NULL) const {
		MyContactPoint point;
		btAlignedObjectArray< MyContactPoint >* pBodies = pLastCollidingBodies;	//pNewCollidingBodies is probably swapped and reset after each step.
		bool ok = true;
		if (pBodies && index >= 0 && index < pBodies->size()) point = (*pBodies)[index];
		else ok = false;
		if (isOperationOKOut) *isOperationOKOut = ok;
		return point;
	}
	//endregion
	//------------------------------------------
public:
	btCollisionObject* self = nullptr;
	//region Ctr/Dtr
	btRigidBodyWithEventsEventDelegates(btCollisionObject* inSelf) : _monitorCollisions(false), pLastCollidingBodies(NULL), pNewCollidingBodies(NULL), self(inSelf) {}
	~btRigidBodyWithEventsEventDelegates() {
		if (pNewCollidingBodies) {
			pNewCollidingBodies->clear();
			delete pNewCollidingBodies; pNewCollidingBodies = NULL;
		}
		if (pLastCollidingBodies) {
			pLastCollidingBodies->clear();
			delete pLastCollidingBodies; pLastCollidingBodies = NULL;
		}
	}
	//endregion

	bool getMonitorCollisions() const { return _monitorCollisions != NULL; }

	//region Completely Optional And Unused Stuff 
	bool isCollidingWith(const btCollisionObject* bodyB, int* collisionIndexOut = NULL) {
		const btAlignedObjectArray< MyContactPoint >* pBodies = pLastCollidingBodies;	//pNewCollidingBodies is probably swapped and reset after each step.
		return (_monitorCollisions && pBodies && bodyB) ?
			(
				!collisionIndexOut ? MyContactPoint::FindRigidBodyIn(bodyB, *pBodies) == -1 ? false : true :
				(*collisionIndexOut = MyContactPoint::FindRigidBodyIn(bodyB, *pBodies)) == -1 ? false : true
				)
			: false;
	}
	/** Similiar methods could be written for other variables
		@param bodyB - the possible colliding body of the query. If 'optionalIndex' is known (e.g. from isCollidingWith) it is ignored.
		@param contactPointLocalSpace - the return value
		@param optionalIndex - if it is already known that the bodyB is colliding (through a previous 'instant' call to isCollidingWith(...) that
							   can't be cached through different steps), reuse the "collisionIndexOut" of the previous call here.
		@return - true if contactPointLocalSpace has been filled
	*/
	bool getContactPointLocalSpace(const btCollisionObject* bodyB, btVector3& contactPointLocalSpace, int optionalIndex = -1) {
		contactPointLocalSpace = btVector3(0., 0., 0.);
		if (optionalIndex != -1) {
			bool ok;
			contactPointLocalSpace = getContactPointForIndex(optionalIndex, &ok).localSpaceContactPoint;
			return ok;
		}
		bool ok = isCollidingWith(bodyB, &optionalIndex);
		if (!ok) return false;
		contactPointLocalSpace = getContactPointForIndex(optionalIndex).localSpaceContactPoint;
		return true;
	}
	const btCollisionObject* isOnABody(const btScalar minYDifferenceBetweenBodyCOMAndCollisionPoint) const {
		const btAlignedObjectArray< MyContactPoint >* pBodies = pLastCollidingBodies;	//pNewCollidingBodies is probably swapped and reset after each step.
		if (_monitorCollisions && pBodies) {
			int size = (*pBodies).size();
			for (int t = 0; t < size; t++) {
				const MyContactPoint& CP = (*pBodies)[t];
				///TODO: Skip if CP.bodyB is camera
				if (CP.localSpaceContactPoint.y() < -minYDifferenceBetweenBodyCOMAndCollisionPoint) {
					return CP.bodyB;
				}
			}
		}
		return NULL;
	}

	static const btRigidBodyWithEventsEventDelegates* upcast(const btCollisionObject* colObj)
	{
		return dynamic_cast <const btRigidBodyWithEventsEventDelegates*> (colObj);
	}

	static btRigidBodyWithEventsEventDelegates* upcast(btCollisionObject* colObj)
	{
		return dynamic_cast <btRigidBodyWithEventsEventDelegates*> (colObj);
	}

	//endregion
	static btAlignedObjectArray< btRigidBodyWithEventsEventDelegates* > monitorCollisionsBodies;	// This list is the list of bodies for which to monitor collisions
	void setMonitorCollisions(bool flag = true);
	static void PerformCollisionDetection(btDynamicsWorld* btWorld, bool processContactPointWithPositiveDistancesToo = false, bool averageAllContactPointsInDifferentManifolds = false);
	static void _SendCollisionEvents();

	SIMD_FORCE_INLINE static int FindRigidBodyIn(const btRigidBodyWithEventsEventDelegates* body, const btAlignedObjectArray< btRigidBodyWithEventsEventDelegates* >& list) {
		int size = list.size();
		for (int t = 0; t < size; t++) {
			if (body == list[t]) return t;
		}
		return -1;
	}
};

class btRigidBodyWithEvents : public btRigidBody, public btRigidBodyWithEventsEventDelegates
{
public:
	// btRigidBody constructor using construction info
	btRigidBodyWithEvents(const btRigidBodyConstructionInfo& constructionInfo)
		: btRigidBody(constructionInfo)
		, btRigidBodyWithEventsEventDelegates(this)
	{}

	// btRigidBody constructor for backwards compatibility. 
	// To specify friction (etc) during rigid body construction, please use the other constructor (using btRigidBodyConstructionInfo)
	btRigidBodyWithEvents(btScalar mass, btMotionState* motionState, btCollisionShape* collisionShape, const btVector3& localInertia = btVector3(0, 0, 0))
		: btRigidBody(mass, motionState, collisionShape, localInertia)
		, btRigidBodyWithEventsEventDelegates(this)
	{}

	virtual ~btRigidBodyWithEvents() {}
};


class btPairCachingGhostObjectWithEvents : public btPairCachingGhostObject, public btRigidBodyWithEventsEventDelegates
{
public:
	// btRigidBody constructor using construction info
	btPairCachingGhostObjectWithEvents()
		: btPairCachingGhostObject()
		, btRigidBodyWithEventsEventDelegates(this)
	{}

	virtual ~btPairCachingGhostObjectWithEvents() {}

};