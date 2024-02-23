/*
Created: 11/10/2009
By: Flix
From: https://pybullet.org/Bullet/phpBB3/viewtopic.php?f=9&t=1691&start=0&hilit=callback
*/
#include "PCH.h"

#include "RigidBodyWithCollisionEvents.h"


ICollisionEvents* btRigidBodyWithEventsEventDelegates::eventDispatcher = NULL;

btAlignedObjectArray< btRigidBodyWithEventsEventDelegates* > btRigidBodyWithEventsEventDelegates::monitorCollisionsBodies;	// This list is the list of bodies for which to monitor collisions

void btRigidBodyWithEventsEventDelegates::setMonitorCollisions( bool flag ) {
    _monitorCollisions = flag;
    int index = FindRigidBodyIn( this, monitorCollisionsBodies );
    if( flag && index == -1 ) {
        monitorCollisionsBodies.push_back( this );
        if( pLastCollidingBodies ) pLastCollidingBodies->clear();
        else {
            pLastCollidingBodies = new btAlignedObjectArray<MyContactPoint>;
            pLastCollidingBodies->reserve( 8 );
        }
        if( pNewCollidingBodies ) pNewCollidingBodies->clear();
        else {
            pNewCollidingBodies = new btAlignedObjectArray<MyContactPoint>;
            pNewCollidingBodies->reserve( 8 );
        }
    }
    else if( !flag && index >= 0 ) {
        monitorCollisionsBodies.remove( this );
        if( pLastCollidingBodies ) {
            delete pLastCollidingBodies; pLastCollidingBodies = NULL;
        }
        if( pNewCollidingBodies ) {
            delete pNewCollidingBodies; pNewCollidingBodies = NULL;
        }
    }
}

void btRigidBodyWithEventsEventDelegates::PerformCollisionDetection( btDynamicsWorld* btWorld, bool processContactPointWithPositiveDistancesToo, bool averageAllContactPointsInDifferentManifolds ) {
    if( !btWorld || btRigidBodyWithEventsEventDelegates::monitorCollisionsBodies.size() == 0 ) return;

    const btRigidBodyWithEventsEventDelegates* bodyA;
    const btRigidBodyWithEventsEventDelegates* bodyB;
    bool monitorA, monitorB;

    //::MessageBoxA(NULL,"1","btRigidBodyWithEvents::PerformCollisionDetection()",MB_OK);
    int totContacts, numContacts;
    int numManifolds = btWorld->getDispatcher()->getNumManifolds();
    MyContactPoint CPA, CPB; btScalar btNumContacts; bool startCollidingA, startCollidingB; int indexA, indexB;

    for( int i = 0; i < numManifolds; i++ )
    {
        btPersistentManifold* contactManifold = btWorld->getDispatcher()->getManifoldByIndexInternal( i );

        const btCollisionObject* objA = contactManifold->getBody0();
        const btCollisionObject* objB = contactManifold->getBody1();

        // INFO: According to my experience btSoftBodies does not appear inside objA or objB: so collisions with them can't be monitored.

        bodyA = btRigidBodyWithEventsEventDelegates::upcast( objA );
        bodyB = btRigidBodyWithEventsEventDelegates::upcast( objB );
        if( !bodyA && !bodyB ) continue;

        monitorA = bodyA ? bodyA->getMonitorCollisions() : false;
        monitorB = bodyB ? bodyB->getMonitorCollisions() : false;
        if( !monitorA && !monitorB ) continue;


        totContacts = contactManifold->getNumContacts();
        numContacts = 0;
        btVector3 ln( 0.f, 0.f, 0.f );;
        btScalar ds( 0.f );
        btScalar imp( 0.f );

        if( monitorA ) {
            btVector3 lpA( 0.f, 0.f, 0.f );;
            btVector3 wpA( 0.f, 0.f, 0.f );;
            for( int j = 0; j < totContacts; j++ ) {
                btManifoldPoint& pt = contactManifold->getContactPoint( j );

                if( pt.m_distance1 < 0 || processContactPointWithPositiveDistancesToo ) {
                    ++numContacts;
                    lpA += pt.m_localPointA;
                    wpA += pt.m_positionWorldOnA;
                    ln += pt.m_normalWorldOnB;	// Will be inverted
                    ds += pt.m_distance1;
                    imp += pt.m_appliedImpulse;
                }
            }
            if( numContacts == 0 ) continue;
            if( numContacts >= 1 ) {
                btNumContacts = (btScalar)numContacts;
                lpA = lpA / btNumContacts;
                wpA = wpA / btNumContacts;
                ln = ln / btNumContacts;
                ds = ds / btNumContacts;
                imp = imp / btNumContacts;	// The applied impulse must be averaged or just summed ? 	        		
            }
            if( bodyA->pNewCollidingBodies ) {
                CPA.Set( objB, lpA, wpA, -ln, ds, imp, btNumContacts, averageAllContactPointsInDifferentManifolds );
                indexA = MyContactPoint::AddAverageContactPointTo( CPA, *bodyA->pNewCollidingBodies, averageAllContactPointsInDifferentManifolds );
                if( indexA == -1 ) {
                    startCollidingA = ( bodyA->pLastCollidingBodies ? ( MyContactPoint::FindRigidBodyIn( CPA.bodyB, *bodyA->pLastCollidingBodies ) == -1 ) : true );
                    ( *bodyA->pNewCollidingBodies )[bodyA->pNewCollidingBodies->size() - 1].startColliding = startCollidingA;
                }
                else {
                    startCollidingA = ( *bodyA->pNewCollidingBodies )[indexA].startColliding;
                }
            }
        }
        else if( monitorB ) {
            btVector3 lpB( 0.f, 0.f, 0.f );;
            btVector3 wpB( 0.f, 0.f, 0.f );;
            for( int j = 0; j < totContacts; j++ ) {
                btManifoldPoint& pt = contactManifold->getContactPoint( j );

                if( pt.m_distance1 < 0 || processContactPointWithPositiveDistancesToo ) {
                    ++numContacts;
                    lpB += pt.m_localPointB;
                    wpB += pt.m_positionWorldOnB;
                    ln += pt.m_normalWorldOnB;
                    ds += pt.m_distance1;
                    imp += pt.m_appliedImpulse;
                }
            }
            if( numContacts == 0 ) continue;
            if( numContacts >= 1 ) {
                btNumContacts = (btScalar)numContacts;
                lpB = lpB / btNumContacts;
                wpB = wpB / btNumContacts;
                ln = ln / btNumContacts;
                ds = ds / btNumContacts;
                imp = imp / btNumContacts;	// The applied impulse must be averaged or just summed ? 	        		
            }
            if( bodyB->pNewCollidingBodies ) {
                CPB.Set( objA, lpB, wpB, ln, ds, imp, btNumContacts, averageAllContactPointsInDifferentManifolds );
                indexB = MyContactPoint::AddAverageContactPointTo( CPB, *bodyB->pNewCollidingBodies, averageAllContactPointsInDifferentManifolds );
                if( indexB == -1 ) {
                    startCollidingB = ( bodyB->pLastCollidingBodies ? ( MyContactPoint::FindRigidBodyIn( CPB.bodyB, *bodyB->pLastCollidingBodies ) == -1 ) : true );
                    ( *bodyB->pNewCollidingBodies )[bodyB->pNewCollidingBodies->size() - 1].startColliding = startCollidingB;
                }
                else {
                    startCollidingB = ( *bodyB->pNewCollidingBodies )[indexB].startColliding;
                }
            }
        }
        else {	// Monitor both A and B
            btVector3 lpA( 0.f, 0.f, 0.f );;
            btVector3 wpA( 0.f, 0.f, 0.f );;
            btVector3 lpB( 0.f, 0.f, 0.f );;
            btVector3 wpB( 0.f, 0.f, 0.f );;
            for( int j = 0; j < totContacts; j++ ) {
                btManifoldPoint& pt = contactManifold->getContactPoint( j );

                if( pt.m_distance1 < 0 || processContactPointWithPositiveDistancesToo ) {
                    ++numContacts;
                    lpA += pt.m_localPointA;
                    wpA += pt.m_positionWorldOnA;
                    lpB += pt.m_localPointB;
                    wpB += pt.m_positionWorldOnB;
                    ln += pt.m_normalWorldOnB;
                    ds += pt.m_distance1;
                    imp += pt.m_appliedImpulse;
                }
            }
            if( numContacts == 0 ) continue;
            if( numContacts >= 1 ) {
                btNumContacts = (btScalar)numContacts;
                lpA = lpA / btNumContacts;
                wpA = wpA / btNumContacts;
                lpB = lpB / btNumContacts;
                wpB = wpB / btNumContacts;
                ln = ln / btNumContacts;
                ds = ds / btNumContacts;
                imp = imp / btNumContacts;	// The applied impulse must be averaged or just summed ? 	        		
            }

            startCollidingA = startCollidingB = false;
            if( bodyA->pNewCollidingBodies ) {
                CPA.Set( objB, lpA, wpA, -ln, ds, imp, btNumContacts, averageAllContactPointsInDifferentManifolds );
                indexA = MyContactPoint::AddAverageContactPointTo( CPA, *bodyA->pNewCollidingBodies, averageAllContactPointsInDifferentManifolds );
                if( indexA == -1 ) {
                    startCollidingA = ( bodyA->pLastCollidingBodies ? ( MyContactPoint::FindRigidBodyIn( CPA.bodyB, *bodyA->pLastCollidingBodies ) == -1 ) : true );
                    ( *bodyA->pNewCollidingBodies )[bodyA->pNewCollidingBodies->size() - 1].startColliding = startCollidingA;
                }
                else {
                    //::MessageBoxA(NULL,("3A "+Ogre::Helper::ToString(indexA)).c_str(),"btRigidBodyWithEvents::PerformCollisionDetection(...)",MB_OK);
                    startCollidingA = ( *bodyA->pNewCollidingBodies )[indexA].startColliding;
                }
            }
            if( bodyB->pNewCollidingBodies ) {
                CPB.Set( objA, lpB, wpB, ln, ds, imp, btNumContacts, averageAllContactPointsInDifferentManifolds );
                indexB = MyContactPoint::AddAverageContactPointTo( CPB, *bodyB->pNewCollidingBodies, averageAllContactPointsInDifferentManifolds );
                if( indexB == -1 ) {
                    startCollidingB = ( bodyB->pLastCollidingBodies ? ( MyContactPoint::FindRigidBodyIn( CPB.bodyB, *bodyB->pLastCollidingBodies ) == -1 ) : true );
                    ( *bodyB->pNewCollidingBodies )[bodyB->pNewCollidingBodies->size() - 1].startColliding = startCollidingB;
                }
                else {
                    //::MessageBoxA(NULL,("3B "+Ogre::Helper::ToString(indexB)).c_str(),"btRigidBodyWithEvents::PerformCollisionDetection(...)",MB_OK);
                    startCollidingB = ( *bodyB->pNewCollidingBodies )[indexB].startColliding;
                }
            }
        }
    }
    //Send Collision Events
    _SendCollisionEvents();
}

void btRigidBodyWithEventsEventDelegates::_SendCollisionEvents()
{
    //#define DEBUG_ME
#ifdef DEBUG_ME
    Ogre::String debug = "";
    static unsigned long frame = 0;
    ++frame;
#endif

    int sizeMonitor = monitorCollisionsBodies.size();
    btAlignedObjectArray< MyContactPoint >* pLastCollidingBodies;
    btAlignedObjectArray< MyContactPoint >* pNewCollidingBodies;
    btRigidBodyWithEventsEventDelegates* bodyToMonitor; int sizeLast, sizeNew;
    const btCollisionObject* bodyLast; bool found; bool startCollision;
    for( int t = 0; t < sizeMonitor; t++ ) {
        bodyToMonitor = monitorCollisionsBodies[t];
        if( bodyToMonitor ) {
            //::MessageBoxA(NULL,(Ogre::Helper::ToString(t)+")"+bodyToMonitor->getName()).c_str(),"btRigidBodyWithEvents::_SendCollisionEvents()",MB_OK);
            pLastCollidingBodies = bodyToMonitor->pLastCollidingBodies;
            pNewCollidingBodies = bodyToMonitor->pNewCollidingBodies;
            if( pLastCollidingBodies && pNewCollidingBodies ) {
                sizeLast = pLastCollidingBodies->size();
                sizeNew = pNewCollidingBodies->size();
                //region Send CollisionStart And CollisionContinue Events
                for( int i = 0; i < sizeNew; i++ ) {
                    const MyContactPoint& CP = ( *pNewCollidingBodies )[i];
                    startCollision = CP.startColliding;//(bodyToMonitor->pLastCollidingBodies ? (MyContactPoint::FindRigidBodyIn(CP.bodyB,*bodyToMonitor->pLastCollidingBodies)==-1) : true);
                    if( startCollision ) bodyToMonitor->OnCollisionStart( bodyToMonitor, CP.bodyB, CP.localSpaceContactPoint, CP.worldSpaceContactPoint, CP.worldSpaceContactNormal, CP.penetrationDistance, CP.appliedImpulse );
                    else bodyToMonitor->OnCollisionContinue( bodyToMonitor, CP.bodyB, CP.localSpaceContactPoint, CP.worldSpaceContactPoint, CP.worldSpaceContactNormal, CP.penetrationDistance, CP.appliedImpulse );
                }
                //endregion
                 //region Send CollisionStop Events			
                for( int n = 0; n < sizeLast; n++ ) {
                    bodyLast = ( *pLastCollidingBodies )[n].bodyB;
                    //::MessageBoxA(NULL,(Ogre::Helper::ToString(t)+")"+bodyToMonitor->getName()+" "+Ogre::Helper::ToString(n)+")"+bodyLast->getName()).c_str(),"btRigidBodyWithEvents::_SendCollisionEvents()",MB_OK);
                    found = false;
                    for( int o = 0; o < sizeNew; o++ ) {
                        //::MessageBoxA(NULL,(Ogre::Helper::ToString(t)+")"+bodyToMonitor->getName()+" "+Ogre::Helper::ToString(n)+")"+bodyNew->getName()+" "+Ogre::Helper::ToString(o)+")"+(*pNewCollidingBodies)[o]->getName()).c_str(),"btRigidBodyWithEvents::_SendCollisionEvents()",MB_OK);
                        if( bodyLast == ( *pNewCollidingBodies )[o].bodyB ) {
                            found = true; break;
                        }
                    }

                    if( !found ) {
                        //::MessageBoxA(NULL,"!found","btRigidBodyWithEvents::_SendCollisionEvents()",MB_OK);
                        const MyContactPoint& CP = ( *pLastCollidingBodies )[n];
                        bodyToMonitor->OnCollisionStop( bodyToMonitor, CP.bodyB, CP.localSpaceContactPoint, CP.worldSpaceContactPoint, CP.worldSpaceContactNormal, CP.penetrationDistance, CP.appliedImpulse );
                    }
                    //else ::MessageBoxA(NULL,"found","btRigidBodyWithEvents::_SendCollisionEvents()",MB_OK);	
                }
                //endregion
            }
#ifdef DEBUG_ME
            //region debugInfo
            {
                sizeLast = pLastCollidingBodies->size();
                sizeNew = pNewCollidingBodies->size();
                //int sizeST = bodyToMonitor->noMoreCollidingBodies.size();
                if( debug.size() > 0 || sizeLast > 0 || sizeNew > 0 )
                {
                    debug += bodyToMonitor->getName() + ":\n";
                    debug += "BEFORE: \nN(";
                    for( int o = 0; o < sizeNew; o++ ) {
                        debug += ( *pNewCollidingBodies )[o].bodyB->getName() + ( ( *pNewCollidingBodies )[o].startColliding ? "|SC|" : "" ) + " ";
                    }
                    debug += ")  \nL(";
                    for( int o = 0; o < sizeLast; o++ ) {
                        debug += ( *pLastCollidingBodies )[o].bodyB->getName() + " ";
                    }
                    debug += ")\n";
                    //::MessageBoxA(NULL,debug.c_str(),"btRigidBodyWithEvents::_SendCollisionEvents()",MB_OK);
                }
            }
            //endregion
#endif					
// Clear Old And Swap buffers:
//::MessageBoxA(NULL,"4","btRigidBodyWithEvents::_SendCollisionEvents()",MB_OK);
            pLastCollidingBodies->clear();
            btAlignedObjectArray< MyContactPoint >* pTemp = bodyToMonitor->pLastCollidingBodies;
            bodyToMonitor->pLastCollidingBodies = bodyToMonitor->pNewCollidingBodies;
            bodyToMonitor->pNewCollidingBodies = pTemp;

            pLastCollidingBodies = bodyToMonitor->pLastCollidingBodies;
            pNewCollidingBodies = bodyToMonitor->pNewCollidingBodies;

#ifdef DEBUG_ME
            //region debugInfo
            {
                sizeLast = pLastCollidingBodies->size();
                sizeNew = pNewCollidingBodies->size();
                //int sizeST = bodyToMonitor->noMoreCollidingBodies.size();
                if( debug.size() > 0 || sizeLast > 0 || sizeNew > 0 )
                {
                    debug += "AFTER: \nN(";
                    for( int o = 0; o < sizeNew; o++ ) {
                        debug += ( *pNewCollidingBodies )[o].bodyB->getName() + ( ( *pNewCollidingBodies )[o].startColliding ? "|SC|" : "" ) + " ";
                    }
                    debug += ") \nL(";
                    for( int o = 0; o < sizeLast; o++ ) {
                        debug += ( *pLastCollidingBodies )[o].bodyB->getName() + " ";
                    }
                    debug += ")\n\n";
                }
            }
            //endregion
#endif
        }
    }
#ifdef	DEBUG_ME
    if( debug.size() > 0 ) {
        debug = " FRAME: " + Ogre::Helper::ToString( frame ) + "\n\n" + debug;
        ::MessageBoxA( NULL, debug.c_str(), "btRigidBodyWithEvents::_SendCollisionEvents()", MB_OK );
    }
#undef DEBUG_ME
#endif  

}
