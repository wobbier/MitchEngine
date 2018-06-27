#include "Cores/PhysicsCore.h"
#include "Components/Transform.h"
#include "Components/Physics/Rigidbody.h"
//#include "Box2D/Box2D.h"

#define M_PI 3.14159
#define RADIANS_TO_DEGREES(__ANGLE__) ((__ANGLE__) / M_PI * 180.0)

PhysicsCore::PhysicsCore() : Base(ComponentFilter().Requires<Transform>().Requires<Rigidbody>())
{
}

PhysicsCore::~PhysicsCore()
{
}

void PhysicsCore::Init()
{
	Gravity = btVector3(0, -10, 0);
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

	btCollisionShape* groundShape = new btStaticPlaneShape(btVector3(0, 3, 0), 1);
	btDefaultMotionState* groundMotionState = new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, -1, 0)));
	btRigidBody::btRigidBodyConstructionInfo
		groundRigidBodyCI(0, groundMotionState, groundShape, btVector3(0, 0, 0));
	btRigidBody* groundRigidBody = new btRigidBody(groundRigidBodyCI);
	PhysicsWorld->addRigidBody(groundRigidBody);
}

void PhysicsCore::Update(float dt)
{
	auto PhysicsEntites = GetEntities();
	for (auto& InEntity : PhysicsEntites)
	{
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		Rigidbody& RigidbodyComponent = InEntity.GetComponent<Rigidbody>();
		if (!RigidbodyComponent.IsRigidbodyInitialized())
		{
			RigidbodyComponent.CreateObject(TransformComponent.Position);
			PhysicsWorld->addRigidBody(RigidbodyComponent.InternalRigidbody);
		}
		else
		{
			btTransform trans;
			RigidbodyComponent.InternalRigidbody->getMotionState()->getWorldTransform(trans);
			trans.setRotation(btQuaternion(TransformComponent.Rotation.x, TransformComponent.Rotation.y, TransformComponent.Rotation.z));
			trans.setOrigin(btVector3(TransformComponent.Position.x, TransformComponent.Position.y, TransformComponent.Position.z));
			RigidbodyComponent.InternalRigidbody->getMotionState()->setWorldTransform(trans);
		}
	}

	// Need a fixed delta probably
	PhysicsWorld->stepSimulation(dt, 10);

	for (auto& InEntity : PhysicsEntites)
	{
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		Rigidbody& RigidbodyComponent = InEntity.GetComponent<Rigidbody>();

		btTransform trans;
		RigidbodyComponent.InternalRigidbody->getMotionState()->getWorldTransform(trans);

		TransformComponent.Position = glm::vec3(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z());
		TransformComponent.Rotation = glm::vec3(trans.getRotation().x(), trans.getRotation().y(), trans.getRotation().z());
	}

}