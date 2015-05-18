#pragma once
#include "Component.h"

namespace ma {
	class Transform :
		public Component {
	public:
		Transform();
		~Transform();

		// Each core must update each loop
		virtual void Update(float dt) final;

		// Separate init from construction code.
		virtual void Init() final;

		// Can receive messages from the engine.
		virtual void SendMessage(Message* message) final;
	};
}