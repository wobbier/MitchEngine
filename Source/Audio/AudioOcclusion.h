#pragma once
#include "Math/Vector3.h"

class Entity;
class PhysicsCore;

// Audio occlusion against the physics world: how many distinct objects (hierarchy roots) have
// colliders on the line from the listener to a source. The source's and the listener's own
// hierarchies (a speaker's housing, the player's capsule) don't count, and triggers never block.
// The engine gives this to AudioCore as its occlusion query.
int CountAudioObstacles( PhysicsCore& InPhysics, const Vector3& InListener, const Vector3& InSource, Entity* InListenerEntity, Entity& InSourceEntity );
