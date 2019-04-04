#include "PCH.h"
#include "Rigidbody.h"
#include "Cores/PhysicsCore.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#include "imgui.h"

Rigidbody::Rigidbody(ColliderType type)
	: Type(type)
{
}

Rigidbody::~Rigidbody()
{
	if (m_world && InternalRigidbody)
	{
		m_world->removeRigidBody(InternalRigidbody);
	}
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

void Rigidbody::CreateObject(const Vector3& Position, btDiscreteDynamicsWorld* world)
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
	m_world = world;

	IsInitialized = true;
}

#if ME_EDITOR

void Rigidbody::OnEditorInspect()
{
	const char* name = "";
	switch (Type)
	{
	case ColliderType::Sphere:
		name = "Sphere";
		break;
	case ColliderType::Box:
		name = "Box";
		break;
	}
	ImGui::Text("Collider Type:");
	ImGui::SameLine();
	ImGui::Text(name);
}

#endif