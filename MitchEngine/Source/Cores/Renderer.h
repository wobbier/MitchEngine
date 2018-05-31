#pragma once
#include "Engine/Core.h"
#include "Components/Sprite.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace MAN
{
	class Renderer :
		public Core<Renderer>
	{
	public:
		Renderer();
		~Renderer();

		// Separate init from construction code.
		virtual void Init() final;

		// Each core must update each loop
		virtual void Update(float dt) final;

		void Render();

		GLuint VAO;
		GLuint VBO[2];
		GLuint EBO;
	};
}