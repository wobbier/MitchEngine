#include "BitBuster.h"
#include "Renderer.h"
#include "Logger.h"
#include "Sprite.h"
#include "Component.h"
#include "Transform.h"
#include "Entity.h"
#include <string>

#include <memory>

using namespace ma;

BitBuster::BitBuster() : Game() {
}

BitBuster::~BitBuster() {
	Game::~Game();
}

std::vector<Entity> EntList;

void BitBuster::Initialize() {
	for (int i = 0; i < 3; ++i) {
		EntList.push_back(GameWorld->CreateEntity());
		auto& TransformComponent = EntList[i].AddComponent<Transform>();
		auto& SpriteComponent = EntList[i].AddComponent<Sprite>();
		TransformComponent.Position = glm::vec3(20.f * i, 20.0f, 0.0f);
		TransformComponent.Scale = glm::vec3(1.f);
		SpriteComponent.SetSourceImage("Default.png");
		EntList[i].SetActive(true);
	}
}

void BitBuster::Update(float DeltaTime) {
	for (auto E : EntList)
	{
		Transform& TransformComponent = E.GetComponent<Transform>();
		TransformComponent.Position += glm::vec3(10 * DeltaTime, 10 * DeltaTime, 0);
	}
}

void BitBuster::Render() {
}

void BitBuster::End() {
}
