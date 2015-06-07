#include "BitBuster.h"
#include "Renderer.h"
#include "Logger.h"
#include "Sprite.h"
#include "Component.h"
#include "Transform.h"
#include "Entity.h"
#include <string>
#include "Input.h"
#include "Window.h"
#include "Animation.h"
#include "Collider2D.h"

#include <memory>

using namespace ma;

BitBuster::BitBuster() : Game() {
}

BitBuster::~BitBuster() {
	Game::~Game();
}

std::vector<Entity> EntList;

void BitBuster::Initialize() {
	for (int i = 0; i < 4; ++i) {
		EntList.push_back(GameWorld->CreateEntity());
		auto& TransformComponent = EntList[i].AddComponent<Transform>();
		auto& SpriteComponent = EntList[i].AddComponent<Sprite>();
		auto& AnimationComponent = EntList[i].AddComponent<Animation>();
		auto& ColliderComponent = EntList[i].AddComponent<Collider2D>();

		TransformComponent.Position = glm::vec3((Window::WINDOW_HEIGHT / 3) + 200.f * i, Window::WINDOW_HEIGHT / 2, 0.0f);
		TransformComponent.Scale = glm::vec3(1.f);

		SpriteComponent.SetSourceImage("Default3.png");

		AnimationComponent.SetAnimationInfo(SpriteComponent.SourceImage->Width, SpriteComponent.SourceImage->Height, 7, 4);
		AnimationComponent.FPS = 24.f;
		EntList[i].SetActive(true);
	}
	for (int i = 0; i < 20; ++i) {
		Entity& Ground = GameWorld->CreateEntity();
		auto& TransformComponent = Ground.AddComponent<Transform>();
		auto& SpriteComponent = Ground.AddComponent<Sprite>();
		auto& ColliderComponent = Ground.AddComponent<Collider2D>();

		SpriteComponent.SetSourceImage("Grass.png");

		TransformComponent.Position = glm::vec3(SpriteComponent.SourceImage->Width * i, Window::WINDOW_HEIGHT - (SpriteComponent.SourceImage->Height/2), 0.0f);
		TransformComponent.Scale = glm::vec3(1.f);

		ColliderComponent.SetBodyType(b2_staticBody);

		Ground.SetActive(true);
	}
}

void BitBuster::Update(float DeltaTime) {
	for (auto E : EntList) {
		Transform& TransformComponent = E.GetComponent<Transform>();
		if (Input::Get().IsKeyDown(GLFW_KEY_W)) {
			TransformComponent.Position += glm::vec3(0, -40 * DeltaTime, 0);
		}
		if (Input::Get().IsKeyDown(GLFW_KEY_S)) {
			TransformComponent.Position += glm::vec3(0, 40 * DeltaTime, 0);
		}
		if (Input::Get().IsKeyDown(GLFW_KEY_A)) {
			TransformComponent.Position += glm::vec3(-40 * DeltaTime, 0, 0);
		}
		if (Input::Get().IsKeyDown(GLFW_KEY_D)) {
			TransformComponent.Position += glm::vec3(40 * DeltaTime, 0, 0);
		}
	}
}

void BitBuster::End() {
}
