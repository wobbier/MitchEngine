#include "PCH.h"
#include "Renderer.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Graphics/Model.h"
#include "Components/Sprite.h"
#include "Components/Transform.h"
#include "Engine/ComponentFilter.h"
#include "Utility/Logger.h"
#include "Engine/Window.h"
#include "Graphics/Shader.h"
#include "Engine/Resource.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>
#include <gtx/quaternion.hpp>

#include "Components/Camera.h"
#include <iostream>

Renderer::Renderer() : Base(ComponentFilter().Requires<Transform>().Requires<Model>())
{
}

void Renderer::Init()
{
	glEnable(GL_DEPTH_TEST);

	Logger::GetInstance().Log(Logger::LogType::Debug, "Renderer Initialized...");
	Logger::GetInstance().Log(Logger::LogType::Debug, (const char*)glGetString(GL_VERSION));

	SkyboxMap = Cubemap::Load("Assets/skybox");
	SkyboxShader = new Shader("Assets/Shaders/Skybox.vert", "Assets/Shaders/Skybox.frag");
	float skyboxVertices[] = {
		// positions          
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
		1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		1.0f, -1.0f, -1.0f,
		1.0f, -1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		1.0f,  1.0f, -1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		1.0f, -1.0f,  1.0f
	};
	// skybox VAO
	glGenVertexArrays(1, &skyboxVAO);
	glGenBuffers(1, &skyboxVBO);
	glBindVertexArray(skyboxVAO);
	glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
}

void Renderer::Update(float dt)
{
}

Renderer::~Renderer()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "Renderer Destroyed...");
}

void Renderer::Render()
{
	Camera* CurrentCamera = Camera::CurrentCamera;
	if (!CurrentCamera)
	{
		return;
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	SkyboxShader->Use();
	SkyboxShader->SetInt("skybox", 0);

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
	float SCR_WIDTH = (float)Window::WINDOW_WIDTH;
	float SCR_HEIGHT = (float)Window::WINDOW_HEIGHT;

	glm::mat4 projection = glm::perspective(glm::radians(Camera::CurrentCamera->Zoom), SCR_WIDTH / SCR_HEIGHT, 0.1f, 100.0f);
	glm::mat4 view = Camera::CurrentCamera->GetViewMatrix();


	int i = 0;
	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
		Transform& trans = InEntity.GetComponent<Transform>();
		Model& modelComponent = InEntity.GetComponent<Model>();

		Shader* shader = modelComponent.ModelShader;
		shader->Use();
		GLuint Program = shader->GetProgram();

		shader->SetMat4("projection", projection);

		shader->SetMat4("view", view);

		// calculate the model matrix for each object and pass it to shader before drawing
		//glm::mat4 model(1.0f);
		//model = glm::translate(model, trans.GetPosition());
		//model = glm::scale(model, glm::vec3(1, 1, 1));
		//model = model * glm::toMat4(trans.Rotation);
		//model = glm::rotate(glm::toMat4(trans.Rotation), glm::vec3(1,1,1));
		//model = model * glm::toMat4(trans.Rotation);
		glm::mat4 model = trans.WorldTransform;
		model = glm::scale(model, glm::vec3(1, 1, 1));
		shader->SetMat4("model", model);
		modelComponent.Draw();
		//cube.DrawCube();
		i++;
	}

	// draw skybox as last
	glDepthFunc(GL_LEQUAL);  // change depth function so depth test passes when values are equal to depth buffer's content
	SkyboxShader->Use();
	SkyboxShader->SetMat4("view", glm::mat4(glm::mat3(Camera::CurrentCamera->GetViewMatrix())));
	SkyboxShader->SetMat4("projection", projection);
	// skybox cube
	glBindVertexArray(skyboxVAO);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, SkyboxMap->Id);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
	glDepthFunc(GL_LESS); // set depth function back to default
}