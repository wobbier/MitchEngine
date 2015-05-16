// 2015 Mitchell Andrews
#pragma once

// Message is nonexistent at the moment.
class Message;

namespace ma {
	class Core {
	public:
		Core() = default;
		virtual ~Core();

		// Each core must update each loop
		virtual void Update(float dt) = 0;

		// Separate init from construction code.
		virtual void Init() = 0;

		// Can receive messages from the engine.
		virtual void SendMessage(Message* message) = 0;
	};
}

