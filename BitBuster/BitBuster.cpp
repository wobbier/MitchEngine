#include "BitBuster.h"
#include "Renderer.h"
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
}

void BitBuster::Update(float DeltaTime) {
	ren->Update(DeltaTime);
}

void BitBuster::Render() {
}

void BitBuster::End() {

}
