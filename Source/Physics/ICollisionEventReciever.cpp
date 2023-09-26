#include "PCH.h"
#include "ICollisionEventReciever.h"
#include "RigidBodyWithCollisionEvents.h"

void ICollisionEvents::updateAction( btCollisionWorld* collisionWorld, btScalar deltaTimeStep )
{
    const bool forceADiscreteCollisionDetection = false;	// true is useless
    const bool processContactPointWithPositiveDistancesToo = true;	// in my experience true is better and false in rare cases can make me miss some start/stop event
    const bool averageAllContactPointsInDifferentManifolds = false; // don't know what I wanted to do here, probably some unfinished stuff
    btDiscreteDynamicsWorld* btWorld = static_cast <btDiscreteDynamicsWorld*> ( collisionWorld );
    if( forceADiscreteCollisionDetection ) btWorld->performDiscreteCollisionDetection();
    btRigidBodyWithEvents::PerformCollisionDetection( btWorld, processContactPointWithPositiveDistancesToo, averageAllContactPointsInDifferentManifolds );
}

void ICollisionEvents::SetInstance( ICollisionEvents* instance, btDynamicsWorld* btWorld )
{
    btAssert( instance && btWorld );
    btRigidBodyWithEventsEventDelegates::eventDispatcher = instance;
    btWorld->addAction( instance );
}