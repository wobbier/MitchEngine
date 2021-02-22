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
	//InternalRigidbody->setWorldTransform(btTransform::getIdentity());
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

void Rigidbody::SetVelocity(Vector3 newVelocity)
{
	Velocity = newVelocity;
}

const bool Rigidbody::IsDynamic() const
{
	return m_isDynamic;
}

std::string Rigidbody::GetColliderString(ColliderType InType)
{
	switch (Type)
	{
	case ColliderType::Sphere:
		return "Sphere";
	case ColliderType::Box:
		return "Box";
	default:
		return "Error";
	}
}

Matrix4 Rigidbody::GetMat()
{
	btTransform trans;
	InternalRigidbody->getMotionState()->getWorldTransform(trans);

	float m[16];
	trans.getOpenGLMatrix(m);

	//DirectX::XMMATRIX transform(m[0], m[4], m[8], m[12],
	//	m[1], m[5], m[9], m[13],
	//	m[2], m[6], m[10], m[14],
	//	m[3], m[7], m[11], m[15]);

	return Matrix4();// transform);
}

void Rigidbody::OnSerialize(json& outJson)
{
	outJson["Scale"] = { Scale.x, Scale.y, Scale.z };
	outJson["ColliderType"] = GetColliderString(Type);
	outJson["Mass"] = Mass;
}

void Rigidbody::OnDeserialize(const json& inJson)
{
	if (inJson.contains("Scale"))
	{
		SetScale(Vector3((float)inJson["Scale"][0], (float)inJson["Scale"][1], (float)inJson["Scale"][2]));
	}

	if (inJson.contains("ColliderType"))
	{
		std::string type = inJson["ColliderType"];
		if (type == "Sphere")
		{
			Type = ColliderType::Sphere;
		}
		else if (type == "Box")
		{
			Type = ColliderType::Box;
		}
	}

	if (inJson.contains("Mass"))
	{
		Mass = inJson["Mass"];
	}
}

void Rigidbody::CreateObject(const Vector3& Position, Quaternion& Rotation, Vector3& InScale, btDiscreteDynamicsWorld* world)
{
	m_world = world;
	if (Scale == Vector3())
	{
		Scale = InScale;
	}
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
	m_isDynamic = (Mass != 0.f);

	btDefaultMotionState* fallMotionState =
		new btDefaultMotionState(btTransform(btQuaternion(Rotation.x, Rotation.y, Rotation.z, Rotation.w), btVector3(Position.x, Position.y, Position.z)));
	btVector3 fallInertia(0, 0, 0);
	if (m_isDynamic)
	{
		fallShape->calculateLocalInertia(Mass, fallInertia);
	}
	btRigidBody::btRigidBodyConstructionInfo fallRigidBodyCI(Mass, fallMotionState, fallShape, fallInertia);
	InternalRigidbody = new btRigidBody(fallRigidBodyCI);
	InternalRigidbody->setUserPointer(this);
	InternalRigidbody->setLinearVelocity(PhysicsCore::ToBulletVector(Velocity));
	IsInitialized = true;
}

#if ME_EDITOR

void Rigidbody::OnEditorInspect()
{
	ImGui::Text("Collider Type:");
	ImGui::SameLine();
	if (ImGui::BeginCombo("##ColliderType", GetColliderString(Type).c_str()))
	{
		for (unsigned int n = 0; n < (unsigned int)ColliderType::Count; ++n)
		{
			const char* name = "";
			switch (n)
			{
			case ColliderType::Sphere:
				name = "Sphere";
				break;
			case ColliderType::Box:
				name = "Box";
				break;
			}

			if (ImGui::Selectable(name, false))
			{
				if ((ColliderType)n != Type)
				{
					Type = (ColliderType)n;
				}
				break;
			}
		}
		ImGui::EndCombo();
	}

	HavanaUtils::EditableVector3("Hitbox Scale", Scale);

	bool m_isDynamicTemp = (Mass != 0.f);
	ImGui::Checkbox("Is Dynamic", &m_isDynamicTemp);
	ImGui::DragFloat("Mass", &Mass);
}

#endif