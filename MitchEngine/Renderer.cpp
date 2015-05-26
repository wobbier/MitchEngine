#include "Renderer.h"
#include "Engine.h"

using namespace ma;

Renderer::Renderer() : Core(){
}

void Renderer::Init() {
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	//GetWorld().Log.Log(Logger::INFO, "Renderer Initialized...");
	//GameEngine->Log.Log(Logger::INFO, (const char*)glGetString(GL_VERSION));
}
void Renderer::Update(float dt) {
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);



	//glfwSwapBuffers(GameEngine->GetWindow()->window);
}

void Renderer::SendMessage(class Message* message) {

}

Renderer::~Renderer() {
	//GameEngine->Log.Log(Logger::DEBUG, "Renderer Destroyed...");
}