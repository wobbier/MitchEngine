#pragma once
#include "ECS/Component.h"
#include <btBulletDynamicsCommon.h>
#include "Math/Vector3.h"
#include <DirectXMath.h>

class Rigidbody
	: public Component<Rigidbody>
{
	friend class PhysicsCore;
public:

	enum class ColliderType : unsigned int
	{
		Box = 0,
		Sphere
	};

	Rigidbody(ColliderType type = ColliderType::Box);
	virtual ~Rigidbody() final;

	// Separate init from construction code.
	virtual void Init() final;

	bool IsRigidbodyInitialized();
	void ApplyForce(const Vector3& direction, float force);
	unsigned int Id = 0;

	DirectX::XMMATRIX GetMat()
	{
		btTransform trans;
		InternalRigidbody->getMotionState()->getWorldTransform(trans);

		float m[16];
		trans.getOpenGLMatrix(m);

		DirectX::XMMATRIX transform(m[0], m[1], m[2], 0.f,
			m[4], m[5], m[6], 0.f,
			m[8], m[9], m[10], 0.f,
			m[12], m[13], m[14], 1.f);

		return transform;
	}
private:
	void CreateObject(const Vector3& Position, class btDiscreteDynamicsWorld* world);
	btRigidBody* InternalRigidbody;

	ColliderType Type = ColliderType::Box;
protected:
	bool IsInitialized = false;
	class btDiscreteDynamicsWorld* m_world;

#if ME_EDITOR

	virtual void OnEditorInspect() final;

#endif
};
