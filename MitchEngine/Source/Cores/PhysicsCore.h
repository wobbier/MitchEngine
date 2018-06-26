#pragma once
#include "Engine/Core.h"
#include <btBulletDynamicsCommon.h>

class PhysicsCore : public Core<PhysicsCore>
{
	friend class Core<PhysicsCore>;
public:
	btDiscreteDynamicsWorld* PhysicsWorld;
	btVector3 Gravity;

	PhysicsCore();
	~PhysicsCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;
};
