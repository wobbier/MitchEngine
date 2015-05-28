#include "BitBuster.h"
#include "Renderer.h"
#include "Logger.h"
#include "Sprite.h"
#include "Component.h"
#include "Transform.h"
#include "Entity.h"
#include <string>

using namespace ma;
Renderer* ren;
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

	TestEnt.SetActive(true);

	ren = new Renderer();
	GameWorld->AddCore<Renderer>(*ren);
}

void BitBuster::Update(float DeltaTime) {
	GameWorld->Simulate();
}

void BitBuster::Render() {
	ren->Render();
}

void BitBuster::End() {

}
