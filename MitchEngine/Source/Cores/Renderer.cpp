#include "Renderer.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Sprite.h"
#include "Components/Transform.h"
#include "Engine/ComponentFilter.h"
#include "Utility/Logger.h"
#include "Engine/Window.h"
#include "Graphics/Shader.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>

#include "Components/Camera.h"
#include <iostream>

Renderer::Renderer() : Base(ComponentFilter().Requires<Transform>().Requires<Sprite>().Requires<DebugCube>())
{
}

void Renderer::Init()
{
	glEnable(GL_DEPTH_TEST);

	Logger::Get().Log(Logger::LogType::Debug, "Renderer Initialized...");
	Logger::Get().Log(Logger::LogType::Debug, (const char*)glGetString(GL_VERSION));
}

void Renderer::Update(float dt)
{
}

Renderer::~Renderer()
{
	Logger::Get().Log(Logger::LogType::Debug, "Renderer Destroyed...");
}

float x = 1.0f;
void Renderer::Render()
{
	Camera* CurrentCamera = Camera::CurrentCamera;
	if (!CurrentCamera)
	{
		return;
	}

	x -= 0.01f;
	glClearColor(x, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// world space positions of our cubes
	glm::vec3 cubePositions[] = {
		glm::vec3(0.0f,  0.0f,  0.0f),
		glm::vec3(2.0f,  5.0f, -15.0f),
		glm::vec3(-1.5f, -2.2f, -2.5f),
		glm::vec3(-3.8f, -2.0f, -12.3f),
		glm::vec3(2.4f, -0.4f, -3.5f),
		glm::vec3(-1.7f,  3.0f, -7.5f),
		glm::vec3(1.3f, -2.0f, -2.5f),
		glm::vec3(1.5f,  2.0f, -2.5f),
		glm::vec3(1.5f,  0.2f, -1.5f),
		glm::vec3(-1.3f,  1.0f, -1.5f)
	};

	glm::mat4 projection = glm::perspective(glm::radians(Camera::CurrentCamera->Zoom), (float)Window::WINDOW_WIDTH / (float)Window::WINDOW_HEIGHT, 0.1f, 100.0f);
	glm::mat4 view = Camera::CurrentCamera->GetViewMatrix();

	int i = 0;
	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
		Transform& trans = InEntity.GetComponent<Transform>();
		Sprite& sprite = InEntity.GetComponent<Sprite>();
		DebugCube& cube = InEntity.GetComponent<DebugCube>();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, sprite.GetTexture()->Id);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, sprite.GetTexture()->Id);

		Shader& shader = sprite.CurrentShader;
		shader.Use();
		GLuint Program = shader.GetProgram();

		shader.SetMat4("projection", projection);

		shader.SetMat4("view", view);

		// calculate the model matrix for each object and pass it to shader before drawing
		glm::mat4 model(1.0f);
		model = glm::translate(model, trans.Position);
		float angle = 20.0f * i;
		model = glm::rotate(model, glm::radians(angle), glm::vec3(1.0f, 0.3f, 0.5f));
		shader.SetMat4("model", model);

		cube.DrawCube();

		i++;
	}
}