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

void BitBuster::Initialize() {
	Entity& TestEnt = GameWorld->CreateEntity();
	auto& TransformComponent = TestEnt.AddComponent<Transform>();
	auto& SpriteComponent = TestEnt.AddComponent<Sprite>();
	TransformComponent.Position = glm::vec2(1.f, 0.5f);
	SpriteComponent.SetSourceImage("Default.png");
	TestEnt.SetActive(true);
}

void BitBuster::Update(float DeltaTime) {
}

void BitBuster::Render() {
}

void BitBuster::End() {

}
