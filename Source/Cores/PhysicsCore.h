#pragma once
#include "ECS/Core.h"
#include <btBulletDynamicsCommon.h>
#include "Math/Vector3.h"
#include "ECS/CoreDetail.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Physics/RaycastHit.h"

class PhysicsCore
	: public Core<PhysicsCore>
{
	friend class Core<PhysicsCore>;
public:
	btDiscreteDynamicsWorld* PhysicsWorld = nullptr;
	btVector3 Gravity;

	PhysicsCore();
	~PhysicsCore();

	static inline btVector3 ToBulletVector(const Vector3& other)
	{
		return btVector3(other.x, other.y, other.z);
	}

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;

	void InitRigidbody(Rigidbody& RigidbodyComponent, Transform& TransformComponent);

	bool Raycast(const Vector3& InPosition, const Vector3& InDirection, RaycastHit& OutHit);

};

ME_REGISTER_CORE(PhysicsCore)