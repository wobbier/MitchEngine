#pragma once
#include "Core.h"
#include "Engine.h"

namespace ma {
	class Renderer :
		public Core {
	public:
		Renderer();
		~Renderer();

		// Separate init from construction code.
		virtual void Init(Engine* e) final;

		// Each core must update each loop
		virtual void Update(float dt) final;

		// Can receive messages from the engine.
		virtual void SendMessage(class Message* message) final;
	};
}