#include "PCH.h"
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
	Gravity = btVector3(0, -9.8, 0);
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

	PhysicsWorld->setDebugDrawer(new GLDebugDrawer());
	PhysicsWorld->getDebugDrawer()->setDebugMode(btIDebugDraw::DBG_DrawWireframe);

	PhysicsWorld->debugDrawWorld();
}

void PhysicsCore::Update(float dt)
{
	BROFILER_CATEGORY("PhysicsCore::Update", Brofiler::Color::Green)
	auto& PhysicsEntites = GetEntities();

	// Need a fixed delta probably
	PhysicsWorld->stepSimulation(dt, 10);

	for (auto& InEntity : PhysicsEntites)
	{
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		Rigidbody& RigidbodyComponent = InEntity.GetComponent<Rigidbody>();

		btRigidBody* rigidbody = RigidbodyComponent.InternalRigidbody;
		btTransform& trans = rigidbody->getWorldTransform();
		
		/*if (TransformComponent.IsDirty)
		{
			glm::vec3 transPos = TransformComponent.GetPosition();
			trans.setOrigin(btVector3(transPos.x, transPos.y, transPos.z));
			rigidbody->setWorldTransform(trans);
			rigidbody->activate();
		}
		else*/
		{
			TransformComponent.SetPosition(glm::vec3(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z()));
		}
	}

}

void PhysicsCore::OnEntityAdded(Entity& NewEntity)
{
	Transform& TransformComponent = NewEntity.GetComponent<Transform>();
	Rigidbody& RigidbodyComponent = NewEntity.GetComponent<Rigidbody>();
	if (!RigidbodyComponent.IsRigidbodyInitialized())
	{
		RigidbodyComponent.CreateObject(TransformComponent.GetPosition());
		PhysicsWorld->addRigidBody(RigidbodyComponent.InternalRigidbody);
	}
}

void GLDebugDrawer::drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor)
{

}

void GLDebugDrawer::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{

}

void GLDebugDrawer::drawSphere(const btVector3& p, btScalar radius, const btVector3& color)
{

}

void GLDebugDrawer::drawTriangle(const btVector3& a, const btVector3& b, const btVector3& c, const btVector3& color, btScalar alpha)
{

}

void GLDebugDrawer::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
{

}

void GLDebugDrawer::reportErrorWarning(const char* warningString)
{

}

void GLDebugDrawer::draw3dText(const btVector3& location, const char* textString)
{

}

void GLDebugDrawer::setDebugMode(int debugMode)
{

}
