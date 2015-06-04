#pragma once
#include "Core.h"

namespace ma {
	class AnimationCore: public Core<AnimationCore> {
	public:
		AnimationCore();
		~AnimationCore();

		// Separate init from construction code.
		virtual void Init() final;

		// Each core must update each loop
		virtual void Update(float dt) final;
	};
}