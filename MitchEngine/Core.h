// 2015 Mitchell Andrews
#pragma once

namespace ma {
	class Core {
	public:
		Core() = default;
		virtual ~Core();

		// Each core must update each loop
		virtual void Update(float dt) = 0;

		// Separate init from construction code.
		virtual void Init(class Engine* e);

		// Can receive messages from the engine.
		virtual void SendMessage(class Message* message) = 0;
	protected:
		class Engine* GameEngine;
	};
}

