#pragma once
#include "ECS/Component.h"
#include <btBulletDynamicsCommon.h>
#include "Math/Vector3.h"

class Rigidbody
	: public Component<Rigidbody>
{
	friend class PhysicsCore;
public:
	Rigidbody();
	~Rigidbody();

	// Separate init from construction code.
	virtual void Init() final;

	bool IsRigidbodyInitialized();
	void ApplyForce(const Vector3& direction, float force);
private:
	void CreateObject(const Vector3& Position);
	btRigidBody* InternalRigidbody;

protected:
	bool IsInitialized = false;
};
