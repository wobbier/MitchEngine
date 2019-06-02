#pragma once
#include "ECS/Core.h"
#include <btBulletDynamicsCommon.h>
#include "Math/Vector3.h"

class PhysicsCore
	: public Core<PhysicsCore>
{
	friend class Core<PhysicsCore>;
public:
	btDiscreteDynamicsWorld* PhysicsWorld;
	btVector3 Gravity;

	PhysicsCore();
	~PhysicsCore();


	static inline Vector3 ToVector3(const btVector3& other)
	{
		return Vector3(other.getX(), other.getY(), other.getZ());
	}

	static inline btVector3 ToBulletVector(const Vector3& other)
	{
		return btVector3(other.X(), other.Y(), other.Z());
	}

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
};
