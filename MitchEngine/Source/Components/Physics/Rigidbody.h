#pragma once
#include "Engine/Component.h"
//#include "Box2D/Box2D.h"

class Rigidbody : public Component<Rigidbody>
{
	friend class PhysicsCore;
public:
	Rigidbody();
	~Rigidbody();

	// Separate init from construction code.
	virtual void Init() final;

	//void SetBodyType(b2BodyType InBodyType);
private:
	//class b2PhysicsWorld;
	//b2Body* Body;

	//b2BodyDef BodyDefinition;
	//b2FixtureDef FixtureDefinition;
	//b2PolygonShape ShapeDefinition;

	bool IsInitialized;
};
