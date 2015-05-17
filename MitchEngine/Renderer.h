#pragma once
#include "Core.h"
namespace ma {
	class Renderer :
		public Core {
	public:
		Renderer();
		~Renderer();

		// Each core must update each loop
		virtual void Update(float dt);

		// Separate init from construction code.
		virtual void Init();

		// Can receive messages from the engine.
		virtual void SendMessage(Message* message);
	};
}