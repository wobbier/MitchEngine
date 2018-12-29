#pragma once
#include "ECS/Component.h"
#include <btBulletDynamicsCommon.h>
#include <glm.hpp>

class Rigidbody : public Component<Rigidbody>
{
	friend class PhysicsCore;
public:
	Rigidbody();
	~Rigidbody();

	// Separate init from construction code.
	virtual void Init() final;

	bool IsRigidbodyInitialized();

private:
	void CreateObject(const glm::vec3& Position);
	btRigidBody* InternalRigidbody;

protected:
	bool IsInitialized = false;
};
