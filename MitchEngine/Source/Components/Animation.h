#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>

class Animation : public Component<Animation>
{
	friend class AnimationCore;
public:

	Animation();
	~Animation();

	struct State
	{
		State();
		std::string Name;
		float FPS;
	};

	void SetAnimationInfo(int InWidth, int InHeight, int InColumns, int InRows);
	// Separate init from construction code.
	virtual void Init() final;
	
	const float GetFramesAccumulated() const;

	bool IsPlaying;

	float FPS;

	glm::vec2 CurrentFrame;

	glm::vec2 GridSize;


private:
	float FramesAccumulated;
};