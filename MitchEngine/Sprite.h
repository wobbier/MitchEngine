#pragma once
#include "Component.h"
#include "Texture.h"
#include "Shader.h"
#include <glm.hpp>

namespace ma {
	class Sprite :
		public Component<Sprite> {
		friend class Renderer;
	public:

		Texture* SourceImage = nullptr;

		glm::vec4 TextureRect;

		// TODO: Remove this from sprite
		glm::vec2 FrameSize;

		Shader CurrentShader;

		Sprite();
		~Sprite();

		// Separate init from construction code.
		virtual void Init() final;

		void SetSourceImage(std::string InPath);

		void SetSpriteRect(float InX, float InY, float InWidth, float InHeight);

		void SetShader(std::string InVert, std::string InFrag);
	};
}