#include "Renderer.h"
#include "Engine.h"

using namespace ma;

Renderer::Renderer() : Core(){
}


void Renderer::Update(float dt) {

}

void Renderer::Init(Engine* e) {
	Core::Init(e);
	e->Log.Log(Logger::INFO, "Renderer Initialized...");
}

void Renderer::SendMessage(Message* message) {

}

Renderer::~Renderer() {
	//e->Log.Log(Logger::DEBUG, "Renderer Destroyed...");
}