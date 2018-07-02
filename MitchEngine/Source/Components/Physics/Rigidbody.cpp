#include "Rigidbody.h"

Rigidbody::Rigidbody()
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

void Rigidbody::CreateObject(const glm::vec3& Position)
{
	btCollisionShape* fallShape = new btBoxShape(btVector3(1,1,1));

	btDefaultMotionState* fallMotionState =
		new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(Position.x, Position.y, Position.z)));
	btScalar mass = 1;
	btVector3 fallInertia(0, 0, 0);
	fallShape->calculateLocalInertia(mass, fallInertia);
	btRigidBody::btRigidBodyConstructionInfo fallRigidBodyCI(mass, fallMotionState, fallShape, fallInertia);
	InternalRigidbody = new btRigidBody(fallRigidBodyCI);
	IsInitialized = true;
}

//void Collider2D::SetBodyType(b2BodyType InBodyType)
//{
//	BodyDefinition.type = InBodyType;
//}