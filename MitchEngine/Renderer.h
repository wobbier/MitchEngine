#pragma once
#include "Core.h"
#include "Sprite.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace ma {
	class Renderer :
		public Core<Renderer> {
	public:
		Renderer();
		~Renderer();

		// Separate init from construction code.
		virtual void Init() final;

		// Each core must update each loop
		virtual void Update(float dt) final;

		void Render();

		GLuint VAO;

	};
}