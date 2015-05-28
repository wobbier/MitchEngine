#include "Renderer.h"
#include "Sprite.h"
#include "Transform.h"
#include "ComponentFilter.h"
#include "Logger.h"

using namespace ma;

Renderer::Renderer() : Base(ComponentFilter().Requires<Transform>()){
}

void Renderer::Init() {
	Logger::Get().Log(Logger::INFO, "Renderer Initialized...");
	Logger::Get().Log(Logger::INFO, (const char*)glGetString(GL_VERSION));
}

float x = 1.0f;
void Renderer::Update(float dt) {

}

Renderer::~Renderer() {
	Logger::Get().Log(Logger::DEBUG, "Renderer Destroyed...");
}

void ma::Renderer::Render() {
	x -= 0.001f;
	glClearColor(x, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables) {
		auto& trans = InEntity.GetComponent<Transform>();
		std::cout << trans.Position.x << std::endl;
		glBegin(GL_POINTS);
		glColor3f(1.0f, 0, 0);
		glVertex2f(trans.Position.x, trans.Position.y);

		glEnd();
		std::cout << trans.Position.y << std::endl;
	}

	//glfwSwapBuffers(GameEngine->GetWindow()->window);
}
