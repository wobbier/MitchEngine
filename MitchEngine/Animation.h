#pragma once
#include "Component.h"
namespace ma {
	class Animation : public Component<Animation> {
	public:
		float FPS;

		Animation();
		~Animation();

		// Each core must update each loop
		virtual void Update(float dt) final;

		// Separate init from construction code.
		virtual void Init() final;
	};
}