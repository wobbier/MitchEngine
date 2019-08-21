#include "PCH.h"
#include "Animation.h"

Animation::Animation()
	: Component("Animation")
{
}

Animation::~Animation()
{
}

void Animation::Init()
{
	FPS = 60.f;
	CurrentFrame = Vector2(0.f, 0.f);
	GridSize = Vector2(1.f, 1.f);
	FramesAccumulated = 0;
}


const float Animation::GetFramesAccumulated() const
{
	return FramesAccumulated;
}

void Animation::SetAnimationInfo(int InWidth, int InHeight, int InColumns, int InRows)
{
	CurrentFrame = Vector2(2.f, 4.f);
	GridSize = Vector2(static_cast<float>(InColumns), static_cast<float>(InRows));
}

Animation::State::State() : Name("Default"), FPS(60.f)
{

}
