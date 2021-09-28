#include "PCH.h"
#include "CharacterController.h"
#include "imgui.h"
#include <Utils/HavanaUtils.h>

class IgnoreBodyAndGhostCast
	: public btCollisionWorld::ClosestRayResultCallback
{
public: 
	IgnoreBodyAndGhostCast(btRigidBody* body, btPairCachingGhostObject* ghostObject)
		: btCollisionWorld::ClosestRayResultCallback(btVector3(), btVector3())
		, m_body(body)
		, m_ghostObject(ghostObject)
	{
	}

	virtual btScalar addSingleResult(btCollisionWorld::LocalRayResult& result, bool normalInWorldSpace) final
	{
		if (result.m_collisionObject == m_body || result.m_collisionObject == m_ghostObject)
		{
			return 1.f;
		}
		return ClosestRayResultCallback::addSingleResult(result, normalInWorldSpace);
	}

private:
	btRigidBody* m_body = nullptr;
	btPairCachingGhostObject* m_ghostObject = nullptr;
};


CharacterController::CharacterController()
	: Component("CharacterController")
{
}

CharacterController::~CharacterController()
{
}

void CharacterController::Init()
{
}

void CharacterController::Initialize(btDynamicsWorld* pPhysicsWorld, const Vector3 spawnPos, float radius, float height, float mass, float stepHeight)
{
	m_world = pPhysicsWorld;
	m_bottomYOffset = height / 2.f + radius;
	m_bottomRoundedRegionYOffset = (height + radius) / 2.f;
	m_stepHeight = stepHeight;

	m_shape = new btCapsuleShape(radius, height);
	m_motionState = new btDefaultMotionState(btTransform(btQuaternion(1.f, 0.f, 0.f, 0.f).normalized(), btVector3(spawnPos[0], spawnPos[1], spawnPos[2])));

	btVector3 inertia;

	m_shape->calculateLocalInertia(mass, inertia);

	btRigidBody::btRigidBodyConstructionInfo rigidbodyInfo(mass, m_motionState, m_shape, inertia);

	rigidbodyInfo.m_friction = 0.1f;
	rigidbodyInfo.m_restitution = 0.f;
	rigidbodyInfo.m_linearDamping = 0.f;

	m_rigidbody = new btRigidBody(rigidbodyInfo);

	m_rigidbody->setAngularFactor(0.f);
	m_rigidbody->setActivationState(DISABLE_DEACTIVATION);

	m_world->addRigidBody(m_rigidbody);

	m_ghostObject = new btPairCachingGhostObject();

	m_ghostObject->setCollisionShape(m_shape);
	m_ghostObject->setUserPointer(this);
	m_ghostObject->setCollisionFlags(btCollisionObject::CF_NO_CONTACT_RESPONSE);

	m_world->addCollisionObject(m_ghostObject, btBroadphaseProxy::KinematicFilter, btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
}

void CharacterController::Walk(const Vector3& direction)
{
	Walk(Vector2(direction.x, direction.z));
}

void CharacterController::Walk(Vector2 direction)
{
	Vector2 velocityXZ = direction + Vector2(m_manualVelocity.x, m_manualVelocity.z);

	const float speed = velocityXZ.Length();

	if (speed > MaxSpeed)
	{
		velocityXZ = velocityXZ / speed * MaxSpeed;
	}

	m_manualVelocity[0] = velocityXZ.x;
	m_manualVelocity[2] = velocityXZ.y;
}

void CharacterController::Update(float dt)
{
	m_ghostObject->setWorldTransform(m_rigidbody->getWorldTransform());

	m_motionState->getWorldTransform(m_motionTransform);

	m_isGrounded = false;

	ParseGhostContacts();

	UpdatePosition();
	UpdateVelocity(dt);

	if (m_jumpTimer < JumpRechargeTime)
	{
		m_jumpTimer += dt;
	}
}

void CharacterController::Jump()
{
	if (m_isGrounded && m_jumpTimer >= JumpRechargeTime)
	{
		m_jumpTimer = 0.f;

		m_rigidbody->applyCentralImpulse(btVector3(0.f, JumpForce, 0.f));

		const float jumpYOffset = 0.01f;

		float previousY = m_rigidbody->getWorldTransform().getOrigin().getY();

		m_rigidbody->getWorldTransform().getOrigin().setY(previousY + jumpYOffset);
	}
}

Vector3 CharacterController::GetPosition() const
{
	return Vector3(m_motionTransform.getOrigin());
}

Vector3 CharacterController::GetVelocity() const
{
	return Vector3(m_rigidbody->getLinearVelocity());
}

bool CharacterController::IsOnGround() const
{
	return m_isGrounded;
}

void CharacterController::OnSerialize(json& outJson)
{
	outJson["JumpForce"] = JumpForce;
	outJson["MaxSpeed"] = MaxSpeed;
	outJson["Deceleration"] = Deceleration;
	outJson["JumpRechargeTime"] = JumpRechargeTime;
	outJson["StepHeight"] = m_stepHeight;
}

void CharacterController::OnDeserialize(const json& inJson)
{
	if (inJson.contains("JumpForce"))
	{
		JumpForce = inJson["JumpForce"];
	}
	if (inJson.contains("MaxSpeed"))
	{
		MaxSpeed = inJson["MaxSpeed"];
	}
	if (inJson.contains("Deceleration"))
	{
		Deceleration = inJson["Deceleration"];
	}
	if (inJson.contains("JumpRechargeTime"))
	{
		JumpRechargeTime = inJson["JumpRechargeTime"];
	}
	if (inJson.contains("StepHeight"))
	{
		m_stepHeight = inJson["StepHeight"];
	}
}

#if ME_EDITOR

void CharacterController::OnEditorInspect()
{
	HavanaUtils::Label("Jump Force");
	ImGui::DragFloat("##Jump Force", &JumpForce);

	HavanaUtils::Label("Max Speed");
	ImGui::DragFloat("##Max Speed", &MaxSpeed);

	HavanaUtils::Label("Deceleration");
	ImGui::DragFloat("##Deceleration", &Deceleration);

	HavanaUtils::Label("Jump Recharge Time");
	ImGui::DragFloat("##Jump Recharge Time", &JumpRechargeTime);

	HavanaUtils::Label("Step Height");
	ImGui::DragFloat("##Step Height", &m_stepHeight);

}

#endif

void CharacterController::ParseGhostContacts()
{
	btManifoldArray manifoldArray;
	btBroadphasePairArray& pairArray = m_ghostObject->getOverlappingPairCache()->getOverlappingPairArray();
	int numPairs = pairArray.size();

	m_isHittingWall = false;
	m_surfaceHitNormals.clear();

	for (int i = 0; i < numPairs; ++i)
	{
		manifoldArray.clear();

		const btBroadphasePair& pait = pairArray[i];

		btBroadphasePair* collisionPair = m_world->getPairCache()->findPair(pait.m_pProxy0, pait.m_pProxy1);
		if (collisionPair == nullptr)
		{
			continue;
		}

		if (collisionPair->m_algorithm != nullptr)
		{
			collisionPair->m_algorithm->getAllContactManifolds(manifoldArray);
		}

		for (int j = 0; j < manifoldArray.size(); ++j)
		{
			btPersistentManifold* manifold = manifoldArray[i];

			if (manifold->getBody0() == m_rigidbody)
			{
				continue;
			}
			for (int k = 0; k < manifold->getNumContacts(); ++k)
			{
				const btManifoldPoint& point = manifold->getContactPoint(k);

				if (point.getDistance() < 0.f)
				{
					const btVector3& pointB = point.getPositionWorldOnB();

					if (pointB.getY() < m_motionTransform.getOrigin().y() - m_bottomRoundedRegionYOffset)
					{
						m_isGrounded = true;
					}
					else
					{
						m_isHittingWall = true;

						btVector3 normal = point.m_normalWorldOnB;

						m_surfaceHitNormals.push_back(Vector3(normal.x(), normal.y(), normal.z()));
					}
				}
			}
		}
	}
}

void CharacterController::UpdatePosition()
{
	IgnoreBodyAndGhostCast raycastBottom(m_rigidbody, m_ghostObject);

	m_world->rayTest(m_rigidbody->getWorldTransform().getOrigin(), m_rigidbody->getWorldTransform().getOrigin() - btVector3(0.f, m_bottomYOffset + m_stepHeight, 0.f), raycastBottom);
	if (raycastBottom.hasHit())
	{
		float previousY = m_rigidbody->getWorldTransform().getOrigin().getY();

		m_rigidbody->getWorldTransform().getOrigin().setY(previousY + (m_bottomYOffset + m_stepHeight) * (1.f - raycastBottom.m_closestHitFraction));

		btVector3 velocity = m_rigidbody->getLinearVelocity();

		velocity.setY(0.f);

		m_rigidbody->setLinearVelocity(velocity);

		m_isGrounded = true;
	}

	float testOffset = 0.7f;

	IgnoreBodyAndGhostCast raycastTop(m_rigidbody, m_ghostObject);

	m_world->rayTest(m_rigidbody->getWorldTransform().getOrigin(), m_rigidbody->getWorldTransform().getOrigin() + btVector3(0.f, m_bottomYOffset + testOffset, 0.f), raycastTop);

	if (raycastTop.hasHit())
	{
		m_rigidbody->getWorldTransform().setOrigin(m_previousPosition);

		btVector3 velocity = m_rigidbody->getLinearVelocity();

		velocity.setY(0.f);

		m_rigidbody->setLinearVelocity(velocity);
	}
	m_previousPosition = m_rigidbody->getWorldTransform().getOrigin();
}

void CharacterController::UpdateVelocity(float dt)
{
	m_manualVelocity.y = m_rigidbody->getLinearVelocity().y();

	const btVector3 velocity (m_manualVelocity.x, m_manualVelocity.y, m_manualVelocity.z);
	m_rigidbody->setLinearVelocity(velocity);

	m_manualVelocity = m_manualVelocity - (m_manualVelocity * Deceleration * dt);

	if (m_isHittingWall)
	{
		for (size_t i = 0, size = m_surfaceHitNormals.size(); i < size; i++)
		{
			auto vec = m_manualVelocity.Dot(m_surfaceHitNormals[i]);

			m_manualVelocity = m_manualVelocity - (vec * 1.05f);
		}
		return;
	}
}
