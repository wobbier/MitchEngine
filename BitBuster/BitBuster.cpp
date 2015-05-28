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
	ren = new Renderer();
	GameWorld->AddCore<Renderer>(*ren);
	Entity TestEnt = GameWorld->CreateEntity();
	Transform* tt = new Transform();
	tt->Position = glm::vec2(0.5f, 0.5f);
	auto& t = TestEnt.AddComponent<Transform>(tt);
	Sprite& SpriteComponent = TestEnt.AddComponent<Sprite>(new Sprite());
	ren->Add(TestEnt);
	t.Position = glm::vec2(1.f, 0.5f);
}

void BitBuster::Update(float DeltaTime) {
	GameWorld->Simulate();
	//ren->Update(DeltaTime);
}

void BitBuster::Render() {
	ren->Render();
}

void BitBuster::End() {

}
