#include "PhysicsCore.h"
#include "Transform.h"
#include "Collider2D.h"
#include "Sprite.h"
#include "Box2D/Box2D.h"

#define M_PI 3.14159
#define RADIANS_TO_DEGREES(__ANGLE__) ((__ANGLE__) / M_PI * 180.0)

namespace MAN
{
	PhysicsCore::PhysicsCore() : Base(ComponentFilter().Requires<Transform>().Requires<Collider2D>())
	{
	}

	PhysicsCore::~PhysicsCore()
	{
	}

	void PhysicsCore::Init()
	{
		Gravity = b2Vec2(0, 100);
		PhysicsWorld = new b2World(Gravity);
	}

	void PhysicsCore::Update(float dt)
	{
		PhysicsWorld->Step(dt, 9, 3);

		auto PhysicsEntites = GetEntities();
		for (auto& InEntity : PhysicsEntites)
		{
			auto& TransformComponent = InEntity.GetComponent<Transform>();
			auto& ColliderComponent = InEntity.GetComponent<Collider2D>();
			auto& SpriteComponent = InEntity.GetComponent<Sprite>();

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
		}
	}
}