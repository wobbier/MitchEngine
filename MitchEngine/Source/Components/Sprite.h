#pragma once
#include "Engine/Component.h"
#include "Graphics/Texture.h"
#include "Graphics/Shader.h"
#include <glm.hpp>

namespace MAN
{
	class Sprite :
		public Component<Sprite>
	{
		friend class Renderer;
		friend class AnimationCore;
	public:
		glm::vec4 TextureRect;

		glm::vec2 FrameSize;

		Shader CurrentShader;

		Sprite();
		~Sprite();

		// Separate init from construction code.
		virtual void Init() final;

		void SetImage(Texture* InTexture);

		void SetSpriteRect(float InX, float InY, float InWidth, float InHeight);

		void SetShader(std::string InVert, std::string InFrag);
	private:
		Texture * SourceImage = nullptr;
	};
}