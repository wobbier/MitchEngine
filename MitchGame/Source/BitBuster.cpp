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
	Entity& Background = GameWorld->CreateEntity();
	Transform& BGPos = Background.GetComponent<Transform>();
	Sprite& BGSprite = Background.AddComponent<Sprite>();
	BGSprite.SetImage(Resources.Get<Texture>("Assets/colored_grass.png"));
	BGPos.Position = glm::vec3(BGSprite.FrameSize.x, Window::WINDOW_HEIGHT / 2, 0.0f);
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