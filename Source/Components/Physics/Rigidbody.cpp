#include "PCH.h"
#include "Rigidbody.h"
#include "Cores/PhysicsCore.h"

Rigidbody::Rigidbody(ColliderType type)
	: Type(type)
{
}

Rigidbody::~Rigidbody()
{
}

void Rigidbody::Init()
{

}


bool Rigidbody::IsRigidbodyInitialized()
{
	return IsInitialized;
}

void Rigidbody::ApplyForce(const Vector3& direction, float force)
{
	InternalRigidbody->setWorldTransform(btTransform::getIdentity());
	InternalRigidbody->applyForce(PhysicsCore::ToBulletVector(direction * force), -PhysicsCore::ToBulletVector(direction));
	InternalRigidbody->activate();
}

void Rigidbody::CreateObject(const Vector3& Position)
{
	btCollisionShape* fallShape;
	switch (Type)
	{
	case ColliderType::Sphere:
		fallShape = new btSphereShape(1.0f);
		break;
	case ColliderType::Box:
	default:
		fallShape = new btBoxShape(btVector3(1.0f, 1.0f, 1.0f));
		break;
	}

	btDefaultMotionState* fallMotionState =
		new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(Position.X(), Position.Y(), Position.Z())));
	btScalar mass = 10;
	btVector3 fallInertia(0, 0, 0);
	fallShape->calculateLocalInertia(mass, fallInertia);
	btRigidBody::btRigidBodyConstructionInfo fallRigidBodyCI(mass, fallMotionState, fallShape, fallInertia);
	InternalRigidbody = new btRigidBody(fallRigidBodyCI);
	IsInitialized = true;
}