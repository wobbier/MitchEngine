#include "BitBuster.h"
#include "Cores/Renderer.h"
#include "Utility/Logger.h"
#include "Components/Sprite.h"
#include "Engine/Component.h"
#include "Components/Transform.h"
#include "Engine/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Engine/Window.h"
#include "Components/Animation.h"
#include "Components/Collider2D.h"

#include <memory>

using namespace MAN;

BitBuster::BitBuster() : Game()
{
}

BitBuster::~BitBuster()
{
	Game::~Game();
}

std::vector<Entity> EntList;

void BitBuster::Initialize()
{
	Resources.Push();
	for (int i = 0; i < 3; ++i)
	{
		Entity& Background = GameWorld->CreateEntity();
		Transform& BGPos = Background.GetComponent<Transform>();
		Sprite& BGSprite = Background.AddComponent<Sprite>();
		BGSprite.SetImage(Resources.Get<Texture>("Assets\\colored_grass.png"));
		BGPos.Position = glm::vec3(BGSprite.FrameSize.x * i, Window::WINDOW_HEIGHT / 2, 0.0f);
	}

	for (int i = 0; i < 10; ++i)
	{
		EntList.push_back(GameWorld->CreateEntity());
		auto& TransformComponent = EntList[i].GetComponent<Transform>();
		auto& SpriteComponent = EntList[i].AddComponent<Sprite>();
		auto& AnimationComponent = EntList[i].AddComponent<Animation>();
		auto& ColliderComponent = EntList[i].AddComponent<Collider2D>();

		TransformComponent.Position = glm::vec3((SpriteComponent.FrameSize.x + 200) * i, SpriteComponent.FrameSize.y * i, 0.0f);

		SpriteComponent.SetImage(Resources.Get<Texture>("Assets\\Default3.png"));

		AnimationComponent.SetAnimationInfo(SpriteComponent.FrameSize.x, SpriteComponent.FrameSize.y, 7, 4);
		AnimationComponent.FPS = 60.f;
	}

	for (int i = 0; i < 30; ++i)
	{
		Entity& Ground = GameWorld->CreateEntity();
		auto& TransformComponent = Ground.GetComponent<Transform>();
		auto& SpriteComponent = Ground.AddComponent<Sprite>();
		auto& ColliderComponent = Ground.AddComponent<Collider2D>();

		SpriteComponent.SetImage(Resources.Get<Texture>("Assets\\Grass.png"));

		TransformComponent.Position = glm::vec3(SpriteComponent.FrameSize.x * i, Window::WINDOW_HEIGHT - (SpriteComponent.FrameSize.y / 2), 0.0f);
		TransformComponent.Scale = glm::vec3(1.f);
		//ColliderComponent.SetBodyType(b2_staticBody);
	}
}

void BitBuster::Update(float DeltaTime)
{
	for (auto E : EntList)
	{
		Transform& TransformComponent = E.GetComponent<Transform>();
		if (Input::Get().IsKeyDown(GLFW_KEY_W))
		{
			TransformComponent.Position += glm::vec3(0, -40 * DeltaTime, 0);
		}
		if (Input::Get().IsKeyDown(GLFW_KEY_S))
		{
			TransformComponent.Position += glm::vec3(0, 40 * DeltaTime, 0);
		}
		if (Input::Get().IsKeyDown(GLFW_KEY_A))
		{
			TransformComponent.Position += glm::vec3(-40 * DeltaTime, 0, 0);
		}
		if (Input::Get().IsKeyDown(GLFW_KEY_D))
		{
			TransformComponent.Position += glm::vec3(40 * DeltaTime, 0, 0);
		}
	}
}

void BitBuster::End()
{
}