#pragma once
#include "Component.h"
#include <string>
#include <glm.hpp>

namespace ma {
	class Animation : public Component<Animation> {
		friend class AnimationCore;
	public:
		struct State {
			State() : Name("Default"), FPS(60.f) {

			}
			std::string Name;
			float FPS;
		};
		bool IsPlaying;
		
		float FPS;

		glm::vec2 CurrentFrame;

		glm::vec2 GridSize;

		void SetAnimationInfo(int InWidth, int InHeight, int InColumns, int InRows);

		Animation();
		~Animation();

		// Each core must update each loop
		virtual void Update(float dt) final;

		// Separate init from construction code.
		virtual void Init() final;

	private:
		float FramesAccumulated;
	};
}