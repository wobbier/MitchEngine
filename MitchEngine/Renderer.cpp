#include "Renderer.h"
#include "Sprite.h"
#include "Transform.h"
#include "ComponentFilter.h"
#include "Logger.h"
#include "Window.h"
#include "Shader.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>

#include "Camera.h"

using namespace ma;

Renderer::Renderer() : Base(ComponentFilter().Requires<Transform>().Requires<Sprite>()){
}

void Renderer::Init() {
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		Logger::Get().Log(Logger::ERR, "Failed to initialize GLEW");
		assert(0);
	}
	glViewport(0, 0, Window::WINDOW_WIDTH, Window::WINDOW_HEIGHT);

	glEnable(GL_DEPTH_TEST);

	// Set up vertex data (and buffer(s)) and attribute pointers
	GLfloat vertices[] = {
		// Positions          // Colors           // Texture Coords
		1.f, 1.f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, // Top Right
		1.f, -1.f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, // Bottom Right
		-1.f, -1.f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, // Bottom Left
		-1.f, 1.f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f  // Top Left 
	};

	GLuint indices[] = {
		0, 1, 3, // First Triangle
		1, 2, 3  // Second Triangle
	};

	GLuint VBO, EBO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	// Position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);
	// Color attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);
	// TexCoord attribute
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)(6 * sizeof(GLfloat)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0); // Unbind VAO

	if (Camera::CurrentCamera == nullptr) {
		Camera::CurrentCamera = new Camera();
	}

	Logger::Get().Log(Logger::DEBUG, "Renderer Initialized...");
	Logger::Get().Log(Logger::DEBUG, (const char*)glGetString(GL_VERSION));
}

void Renderer::Update(float dt) {

}

Renderer::~Renderer() {
	Logger::Get().Log(Logger::DEBUG, "Renderer Destroyed...");
}

float x = 1.0f;
void ma::Renderer::Render() {
	x -= 0.001f;
	glClearColor(x, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables) {
		auto& trans = InEntity.GetComponent<Transform>();
		auto& sprite = InEntity.GetComponent<Sprite>();
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		sprite.CurrentShader.Use();

		glm::mat4 view;
		view = Camera::CurrentCamera->GetViewMatrix();
		glm::mat4 projection;
		projection = glm::ortho(0.f, GLfloat(Window::WINDOW_WIDTH), GLfloat(Window::WINDOW_HEIGHT), 0.f, -10.0f, 10.f);
		GLint modelLoc = glGetUniformLocation(sprite.CurrentShader.Program, "model");
		GLint viewLoc = glGetUniformLocation(sprite.CurrentShader.Program, "view");
		GLint projLoc = glGetUniformLocation(sprite.CurrentShader.Program, "projection");
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

		glm::mat4 model = glm::mat4(1);
		model = glm::translate(model, trans.Position);
		model = glm::scale(model, glm::vec3(sprite.SourceImage->Width, sprite.SourceImage->Height, 1.f));
		model = glm::scale(model, trans.Scale);
		
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, sprite.SourceImage->Id);
		glUniform1i(glGetUniformLocation(sprite.CurrentShader.Program, "texture_diffuse"), 0);

		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
	}
}
