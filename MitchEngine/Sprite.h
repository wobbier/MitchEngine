#pragma once
#include "Component.h"
#include "Texture.h"
#include "Shader.h"

namespace ma {
	class Sprite :
		public Component<Sprite> {
	public:

		Texture* SourceImage = nullptr;

		Shader CurrentShader;

		Sprite();
		~Sprite();

		// Each core must update each loop
		virtual void Update(float dt) final;

		// Separate init from construction code.
		virtual void Init() final;

		// Can receive messages from the engine.
		virtual void SendMessage(class Message* message) final;

		void SetSourceImage(std::string InPath);

		void SetShader(std::string InVert, std::string InFrag);
	};
}