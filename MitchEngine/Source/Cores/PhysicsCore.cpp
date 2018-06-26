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
}

void PhysicsCore::Update(float dt)
{
	auto PhysicsEntites = GetEntities();
	for (auto& InEntity : PhysicsEntites)
	{
		auto& TransformComponent = InEntity.GetComponent<Transform>();
		auto& RigidbodyComponent = InEntity.GetComponent<Rigidbody>();
		if (RigidbodyComponent.IsInitialized)
		{
		}
	}

	PhysicsWorld->stepSimulation(dt, 10);

	for (auto& InEntity : PhysicsEntites)
	{
		auto& TransformComponent = InEntity.GetComponent<Transform>();
		auto& RigidbodyComponent = InEntity.GetComponent<Rigidbody>();
		/*
		if (!ColliderComponent.IsInitialized)
		{
			ColliderComponent.BodyDefinition.position = b2Vec2(TransformComponent.Position.x, TransformComponent.Position.y);

			ColliderComponent.Body = PhysicsWorld->CreateBody(&ColliderComponent.BodyDefinition);

			if (&SpriteComponent)
			{
				ColliderComponent.ShapeDefinition.SetAsBox((SpriteComponent.FrameSize.x / 6.5f), (SpriteComponent.FrameSize.y / 6.5f));
			}
			else
			{
				ColliderComponent.ShapeDefinition.SetAsBox(1, 1);
			}
			ColliderComponent.FixtureDefinition.shape = &ColliderComponent.ShapeDefinition;
			ColliderComponent.FixtureDefinition.density = 1.f;
			ColliderComponent.FixtureDefinition.friction = .3f;

			ColliderComponent.Body->CreateFixture(&ColliderComponent.FixtureDefinition);
			ColliderComponent.IsInitialized = true;
		}

		TransformComponent.Position = glm::vec3(ColliderComponent.Body->GetPosition().x, ColliderComponent.Body->GetPosition().y, TransformComponent.Position.z);
		TransformComponent.Rotation = glm::vec3(0, 0, RADIANS_TO_DEGREES(ColliderComponent.Body->GetAngle()));
		*/
	}
}