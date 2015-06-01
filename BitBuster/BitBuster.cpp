#include "BitBuster.h"
#include "Renderer.h"
#include "Logger.h"
#include "Sprite.h"
#include "Component.h"
#include "Transform.h"
#include "Entity.h"
#include <string>

using namespace ma;

BitBuster::BitBuster() : Game() {
}

BitBuster::~BitBuster() {
	Game::~Game();
}
Transform* TransformComponent;
void BitBuster::Initialize() {
	Entity& TestEnt = GameWorld->CreateEntity();
	TransformComponent = &(TestEnt.AddComponent<Transform>());
	auto& SpriteComponent = TestEnt.AddComponent<Sprite>();
	TransformComponent->Position = glm::vec3(20.f, 20.0f, 0.0f);
	TransformComponent->Scale = glm::vec3(1.f);
	SpriteComponent.SetSourceImage("Default.png");
	TestEnt.SetActive(true);
}

void BitBuster::Update(float DeltaTime) {
	TransformComponent->Position += glm::vec3(10 * DeltaTime, 10 * DeltaTime, 0);
}

void BitBuster::Render() {
}

void BitBuster::End() {

}
