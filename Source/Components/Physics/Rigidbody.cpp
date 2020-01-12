#include "PCH.h"
#include "Rigidbody.h"
#include "Cores/PhysicsCore.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#include "imgui.h"
#include "Utils/HavanaUtils.h"

Rigidbody::Rigidbody(ColliderType type)
	: Component("Rigidbody")
	, Type(type)
{
}

Rigidbody::Rigidbody()
	: Component("Rigidbody")
	, Type(ColliderType::Box)
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

void Rigidbody::SetScale(Vector3 InScale)
{
	Scale = InScale;
	if (fallShape)
	{
		fallShape->setLocalScaling(btVector3(InScale[0], InScale[1], InScale[2]));
	}
}

void Rigidbody::SetMass(float InMass)
{
	Mass = InMass;
	if (InternalRigidbody)
	{
		InternalRigidbody->setMassProps(InMass, btVector3());
	}
}

void Rigidbody::Serialize(json& outJson)
{
	Component::Serialize(outJson);

	outJson["Scale"] = { Scale.X(), Scale.Y(), Scale.Z() };
}

void Rigidbody::Deserialize(const json& inJson)
{
	if (inJson.contains("Scale"))
	{
		SetScale(Vector3((float)inJson["Scale"][0], (float)inJson["Scale"][1], (float)inJson["Scale"][2]));
	}
}

void Rigidbody::CreateObject(const Vector3& Position, Vector3& Rotation, btDiscreteDynamicsWorld* world)
{
	switch (Type)
	{
	case ColliderType::Sphere:
		fallShape = new btSphereShape(1.0f);
		break;
	case ColliderType::Box:
	default:
		fallShape = new btBoxShape(btVector3(Scale[0], Scale[1], Scale[2]));
		break;
	}
	//fallShape->setLocalScaling(btVector3(Scale[0], Scale[1], Scale[2]));

	btDefaultMotionState* fallMotionState =
		new btDefaultMotionState(btTransform(btQuaternion(Rotation.X(), Rotation.Y(), Rotation.Z()), btVector3(Position.X(), Position.Y(), Position.Z())));
	btVector3 fallInertia(0, 0, 0);
	fallShape->calculateLocalInertia(Mass, fallInertia);
	btRigidBody::btRigidBodyConstructionInfo fallRigidBodyCI(Mass, fallMotionState, fallShape, fallInertia);
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

	HavanaUtils::EditableVector3("Hitbox Scale", Scale);
}

#endif