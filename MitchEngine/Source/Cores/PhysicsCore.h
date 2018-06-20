#pragma once
#include "Engine/Core.h"
//#include "Box2D/Box2D.h"

class PhysicsCore : public Core<PhysicsCore>
{
	friend class Core<PhysicsCore>;
public:
	//b2World * PhysicsWorld;

	//b2Vec2 Gravity;

	PhysicsCore();
	~PhysicsCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;
};
