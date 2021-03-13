#include "PCH.h"
#include "Cores/PhysicsCore.h"
#include "Components/Transform.h"
#include "Components/Physics/Rigidbody.h"
#include "LinearMath/btScalar.h"
#include "RenderCommands.h"
#include "Engine/Engine.h"
#include "Math/Quaternion.h"
#include "Math/Matrix4.h"
#include "Components/Physics/CharacterController.h"
#include "BulletCollision/CollisionDispatch/btCollisionWorld.h"
#include "BulletCollision/NarrowPhaseCollision/btRaycastCallback.h"
#include "Math/Line.h"
#include "Mathf.h"
#include "Work/Burst.h"

PhysicsCore::PhysicsCore()
	: Base(ComponentFilter().Requires<Transform>().RequiresOneOf<Rigidbody>().RequiresOneOf<CharacterController>())
{
	Gravity = btVector3(0, -9.8f, 0);
	///collision configuration contains default setup for memory, collision setup. Advanced users can create their own configuration.
	btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();

	///use the default collision dispatcher. For parallel processing you can use a diffent dispatcher (see Extras/BulletMultiThreaded)
	btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collisionConfiguration);

	///btDbvtBroadphase is a good general purpose broadphase. You can also try out btAxis3Sweep.
	btBroadphaseInterface* overlappingPairCache = new btDbvtBroadphase();

	///the default constraint solver. For parallel processing you can use a different solver (see Extras/BulletMultiThreaded)
	btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver;

	PhysicsWorld = new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, solver, collisionConfiguration);

	PhysicsWorld->setGravity(Gravity);

	//btCollisionShape* groundShape = new btStaticPlaneShape(btVector3(0, 1, 0), 1);
	//btDefaultMotionState* groundMotionState = new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, -20, 0)));
	//btRigidBody::btRigidBodyConstructionInfo
	//	groundRigidBodyCI(0, groundMotionState, groundShape, btVector3(0, 0, 0));
	//btRigidBody* groundRigidBody = new btRigidBody(groundRigidBodyCI);
	//PhysicsWorld->addRigidBody(groundRigidBody);
}

PhysicsCore::~PhysicsCore()
{
}

void PhysicsCore::Init()
{
}

void PhysicsCore::Update(float dt)
{
	OPTICK_CATEGORY("PhysicsCore::Update", Optick::Category::Physics)
	auto& PhysicsEntites = GetEntities();

	// Need a fixed delta probably
	PhysicsWorld->stepSimulation(dt, 10);

	//std::vector<std::pair<int, int>> batches;
	//Burst::GenerateChunks(PhysicsEntites.size(), 11, batches);
	//if (PhysicsEntites.size() <= 0)
	//{
	//	return;
	//}
	//for (auto& batch : batches)
	//{
	//	int batchBegin = batch.first;
	//	int batchEnd = batch.second;
	//	int batchSize = batchEnd - batchBegin;

	//	//YIKES(std::to_string(batchBegin) + " End:" + std::to_string(batchEnd) + " Size:" + std::to_string(batchSize));
	//	auto m_callBack = [this, &PhysicsEntites, batchBegin, batchEnd, batchSize, dt]() {
	//		OPTICK_CATEGORY("Job::UpdatePhysics", Optick::Category::Physics);

	//		for (int entIndex = batchBegin; entIndex < batchEnd; ++entIndex)
	//		{
	//			Entity& InEntity = PhysicsEntites[entIndex];
	//			Transform& TransformComponent = InEntity.GetComponent<Transform>();

	//			if (InEntity.HasComponent<Rigidbody>())
	//			{
	//				Rigidbody& RigidbodyComponent = InEntity.GetComponent<Rigidbody>();

	//				InitRigidbody(RigidbodyComponent, TransformComponent);

	//				btRigidBody* rigidbody = RigidbodyComponent.InternalRigidbody;
	//				btTransform& trans = rigidbody->getWorldTransform();


	//				if (TransformComponent.IsDirty())
	//				{
	//					btTransform trans;
	//					Vector3 transPos = TransformComponent.GetPosition();
	//					trans.setRotation(btQuaternion(TransformComponent.LocalRotation.x, TransformComponent.LocalRotation.y, TransformComponent.LocalRotation.z, TransformComponent.LocalRotation.w));
	//					trans.setOrigin(btVector3(transPos.x, transPos.y, transPos.z));
	//					rigidbody->setWorldTransform(trans);
	//					rigidbody->activate();
	//				}
	//				else if (RigidbodyComponent.IsDynamic())
	//				{
	//					btTransform& trans = rigidbody->getWorldTransform();
	//					btQuaternion rot;
	//					trans.getBasis().getRotation(rot);
	//					Vector3 bulletPosition = Vector3(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z());
	//					TransformComponent.SetPosition(bulletPosition);
	//					btScalar x, y, z;
	//					rot.getEulerZYX(z, y, x);
	//					TransformComponent.SetRotation(Vector3(Mathf::Degrees(x), Mathf::Degrees(y), Mathf::Degrees(z)));
	//					//Transform tempTrans;
	//					//tempTrans.SetPosition(bulletPosition);

	//					//Matrix4 mat;
	//					//Quaternion rot2(tempTrans.Rotation.GetInternalVec());
	//					//mat.GetInternalMatrix().CreateWorld(tempTrans.GetPosition().GetInternalVec(), tempTrans.Rotation.GetInternalVec().Forward, tempTrans.Rotation.GetInternalVec().Up);
	//					//tempTrans.SetWorldTransform(mat);
	//					//DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
	//					//DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(XMQuaternionRotationRollPitchYawFromVector(tempTrans.Rotation.GetInternalVec()));
	//					//DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Child->GetScale().GetInternalVec());
	//					//DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Child->GetPosition().GetInternalVec());
	//					//Child->SetWorldTransform(Matrix4((rot * scale * pos) * CurrentTransform->WorldTransform.GetInternalMatrix()));
	//					//GetEngine().GetRenderer().UpdateMatrix(RigidbodyComponent.DebugColliderId, TransformComponent.GetMatrix().GetInternalMatrix());
	//				}
	//			}

	//			if (InEntity.HasComponent<CharacterController>())
	//			{
	//				CharacterController& Controller = InEntity.GetComponent<CharacterController>();

	//				//
	//				btRigidBody* rigidbody = Controller.m_rigidbody;
	//				btTransform& trans = rigidbody->getWorldTransform();

	//				Quaternion rotation = TransformComponent.GetWorldRotation();
	//				trans.setRotation(btQuaternion(rotation[0], rotation[1], rotation[2], rotation[3]));
	//				////trans.setOrigin(btVector3(transPos.X(), transPos.Y(), transPos.Z()));
	//				////rigidbody->setWorldTransform(trans);
	//				rigidbody->setWorldTransform(trans);
	//				rigidbody->activate();

	//				Controller.Update(dt);

	//				TransformComponent.SetWorldPosition(Controller.GetPosition());
	//			}
	//		}
	//	};
	//	GetEngine().GetJobSystem().AddWork(m_callBack);
	//	GetEngine().GetJobSystem().Wait();
		//burst.AddWork2(job, sizeof(Burst::LambdaWorkEntry));
	//}
	//burst.FinalizeWork();
}

void PhysicsCore::OnEntityAdded(Entity& NewEntity)
{
	Transform& TransformComponent = NewEntity.GetComponent<Transform>();
	if (NewEntity.HasComponent<Rigidbody>())
	{
		Rigidbody& RigidbodyComponent = NewEntity.GetComponent<Rigidbody>();
		InitRigidbody(RigidbodyComponent, TransformComponent);

	}

	if (NewEntity.HasComponent<CharacterController>())
	{
		CharacterController& Controller = NewEntity.GetComponent<CharacterController>();
		Controller.Initialize(nullptr, PhysicsWorld, TransformComponent.GetWorldPosition(), 1, 5, 5, 1);
	}
}

void PhysicsCore::InitRigidbody(Rigidbody& RigidbodyComponent, Transform& TransformComponent)
{
	if (!RigidbodyComponent.IsRigidbodyInitialized())
	{

		RigidbodyComponent.CreateObject(TransformComponent.GetPosition(), TransformComponent.LocalRotation, TransformComponent.GetScale(), PhysicsWorld);
        PhysicsWorld->addRigidBody(RigidbodyComponent.InternalRigidbody);
		//Moonlight::DebugColliderCommand cmd;
		//RigidbodyComponent.DebugColliderId = GetEngine().GetRenderer().PushDebugCollider(cmd);
	}
}

bool PhysicsCore::Raycast(const Vector3& InPosition, const Vector3& InDirection, RaycastHit& OutHit)
{
	btVector3 red(1, 0, 0);
	btVector3 blue(0, 0, 1);

	///all hits
	if (false)
	{
		btVector3 from(InPosition.x, InPosition.y, InPosition.z);
		btVector3 to(InDirection.x, InDirection.y, InDirection.z);
		//PhysicsWorld->getDebugDrawer()->drawLine(from, to, btVector4(0, 0, 0, 1));
		btCollisionWorld::AllHitsRayResultCallback allResults(from, to);
		allResults.m_flags |= btTriangleRaycastCallback::kF_KeepUnflippedNormal;
		//kF_UseGjkConvexRaytest flag is now enabled by default, use the faster but more approximate algorithm
		//allResults.m_flags |= btTriangleRaycastCallback::kF_UseSubSimplexConvexCastRaytest;
		allResults.m_flags |= btTriangleRaycastCallback::kF_UseSubSimplexConvexCastRaytest;

		PhysicsWorld->rayTest(from, to, allResults);

		for (int i = 0; i < allResults.m_hitFractions.size(); i++)
		{
			btVector3 p = from.lerp(to, allResults.m_hitFractions[i]);
			OutHit.Position = Vector3(p.x(), p.y(), p.z());
			btVector3& n = allResults.m_hitNormalWorld[i];
			OutHit.Normal = Vector3(n.x(), n.y(), n.z());
			//PhysicsWorld->getDebugDrawer()->drawSphere(p, 0.1, red);
			//PhysicsWorld->getDebugDrawer()->drawLine(p, p + , red);
		}
		if (allResults.hasHit())
		{
			return true;
		}
	}

	///first hit
	{
		btVector3 from(InPosition.x, InPosition.y, InPosition.z);
		btVector3 to(InDirection.x, InDirection.y, InDirection.z);
		//PhysicsWorld->getDebugDrawer()->drawLine(from, to, btVector4(0, 0, 1, 1));

		btCollisionWorld::ClosestRayResultCallback	closestResults(from, to);
		closestResults.m_flags |= btTriangleRaycastCallback::kF_FilterBackfaces;

		PhysicsWorld->rayTest(from, to, closestResults);

		if (closestResults.hasHit())
		{

			btVector3 p = from.lerp(to, closestResults.m_closestHitFraction);
			OutHit.Position = Vector3(p.x(), p.y(), p.z());
			btVector3& n = closestResults.m_hitNormalWorld;
			OutHit.Normal = Vector3(n.x(), n.y(), n.z());
			OutHit.What = static_cast<Rigidbody*>(closestResults.m_collisionObject->getUserPointer());
			//OutHit.Ray = Line(InDirection, OutHit.What);


			return true;
		}
	}
	return false;
}
