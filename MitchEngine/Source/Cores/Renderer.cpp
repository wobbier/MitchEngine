#include "Renderer.h"
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

#include "Engine/Camera.h"

using namespace MAN;

Renderer::Renderer() : Base(ComponentFilter().Requires<Transform>().Requires<Sprite>())
{
}

void Renderer::Init()
{
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
	{
		Logger::Get().Log(Logger::LogType::Error, "Failed to initialize GLEW");
		assert(0);
	}
	glViewport(0, 0, Window::WINDOW_WIDTH, Window::WINDOW_HEIGHT);

	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Set up vertex data (and buffer(s)) and attribute pointers
	GLfloat vertices[] = {
		// Positions          // Colors
		0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
		0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f,
		-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f,
		-0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.0f,
	};

	GLuint indices[] = {
		0, 1, 3, // First Triangle
		1, 2, 3  // Second Triangle
	};

	glGenVertexArrays(1, &VAO);
	glGenBuffers(2, VBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	// Position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);
	// Color attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	GLfloat TextureCoords[] = {
		1.f, 1.f,
		1.f, 0.f,
		0.f, 0.f,
		0.f, 1.f
	};
	glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(TextureCoords), TextureCoords, GL_DYNAMIC_DRAW);

	// TexCoord attribute
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0 * sizeof(GLfloat), (GLvoid*)(0 * sizeof(GLfloat)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0); // Unbind VAO

	if (Camera::CurrentCamera == nullptr)
	{
		Camera::CurrentCamera = new Camera();
	}

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
void MAN::Renderer::Render()
{
	x -= 0.01f;
	glClearColor(x, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
		auto& trans = InEntity.GetComponent<Transform>();
		auto& sprite = InEntity.GetComponent<Sprite>();

		sprite.CurrentShader.Use();
		GLuint Program = sprite.CurrentShader.Program;

		glm::mat4 CameraView = Camera::CurrentCamera->GetViewMatrix();
		glm::mat4 ProjectionView = glm::ortho(0.f, GLfloat(Window::WINDOW_WIDTH), GLfloat(Window::WINDOW_HEIGHT), 0.f, -10.0f, 10.f);

		GLint ModelLoc = glGetUniformLocation(Program, "model");
		GLint ViewLoc = glGetUniformLocation(Program, "view");
		GLint ProjectionLoc = glGetUniformLocation(Program, "projection");
		GLint TextureCoordsLoc = glGetAttribLocation(Program, "texCoord");

		glUniformMatrix4fv(ViewLoc, 1, GL_FALSE, glm::value_ptr(CameraView));
		glUniformMatrix4fv(ProjectionLoc, 1, GL_FALSE, glm::value_ptr(ProjectionView));

		GLfloat TextureCoords[] = {
			sprite.TextureRect.z, sprite.TextureRect.w,
			sprite.TextureRect.z, sprite.TextureRect.y,
			sprite.TextureRect.x, sprite.TextureRect.y,
			sprite.TextureRect.x, sprite.TextureRect.w
		};

		glBindBuffer(GL_ARRAY_BUFFER, TextureCoordsLoc);
		glBufferData(GL_ARRAY_BUFFER, sizeof(TextureCoords), TextureCoords, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(TextureCoordsLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
		glEnableVertexAttribArray(TextureCoordsLoc);

		glm::mat4 model = glm::mat4(1);
		model = glm::translate(model, trans.Position);
		model = glm::scale(model, glm::vec3(sprite.FrameSize.x, sprite.FrameSize.y, 1.f));
		model = glm::scale(model, trans.Scale);
		model = glm::rotate(model, trans.Rotation.x, glm::vec3(1, 0, 0));
		model = glm::rotate(model, trans.Rotation.y, glm::vec3(0, 1, 0));
		model = glm::rotate(model, trans.Rotation.z, glm::vec3(0, 0, 1));

		glUniformMatrix4fv(ModelLoc, 1, GL_FALSE, glm::value_ptr(model));
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, sprite.SourceImage->Id);
		glUniform1i(glGetUniformLocation(Program, "texture_diffuse"), 0);

		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
	}
}