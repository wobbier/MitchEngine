#include "PCH.h"
#include "Cores/PhysicsCore.h"
#include "Components/Transform.h"
#include "Components/Physics/Rigidbody.h"
#include "LinearMath/btScalar.h"

#define M_PI 3.14159
#define RADIANS_TO_DEGREES(__ANGLE__) ((__ANGLE__) / M_PI * 180.0)

PhysicsCore::PhysicsCore() : Base(ComponentFilter().Requires<Transform>().Requires<Rigidbody>())
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

	btCollisionShape* groundShape = new btStaticPlaneShape(btVector3(0, 1, 0), 1);
	btDefaultMotionState* groundMotionState = new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, -1, 0)));
	btRigidBody::btRigidBodyConstructionInfo
		groundRigidBodyCI(0, groundMotionState, groundShape, btVector3(0, 0, 0));
	btRigidBody* groundRigidBody = new btRigidBody(groundRigidBodyCI);
	PhysicsWorld->addRigidBody(groundRigidBody);
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
	auto PhysicsEntites = GetEntities();

	// Need a fixed delta probably
	PhysicsWorld->stepSimulation(dt, 10);

	for (auto& InEntity : PhysicsEntites)
	{
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		Rigidbody& RigidbodyComponent = InEntity.GetComponent<Rigidbody>();

		btRigidBody* rigidbody = RigidbodyComponent.InternalRigidbody;
		btTransform& trans = rigidbody->getWorldTransform();
		
		if (TransformComponent.IsDirty)
		{
			Vector3 transPos = TransformComponent.GetPosition();
			trans.setOrigin(btVector3(transPos.X(), transPos.Y(), transPos.Z()));
			trans.setRotation(btQuaternion(TransformComponent.Rotation.X(), TransformComponent.Rotation.Y(), TransformComponent.Rotation.Z()));
			rigidbody->setWorldTransform(trans);
			rigidbody->activate();
		}
		else
		{
			btQuaternion rot;
			trans.getBasis().getRotation(rot);
			TransformComponent.SetPosition(Vector3(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z()));
			btScalar x, y, z;
			rot.getEulerZYX(x, y, z);
			TransformComponent.SetRotation(Vector3(x, y, z));
		}
	}

}

void PhysicsCore::OnEntityAdded(Entity& NewEntity)
{
	Transform& TransformComponent = NewEntity.GetComponent<Transform>();
	Rigidbody& RigidbodyComponent = NewEntity.GetComponent<Rigidbody>();
	if (!RigidbodyComponent.IsRigidbodyInitialized())
	{
		RigidbodyComponent.CreateObject(TransformComponent.GetPosition(), TransformComponent.Rotation, PhysicsWorld);
		PhysicsWorld->addRigidBody(RigidbodyComponent.InternalRigidbody);
	}
}
