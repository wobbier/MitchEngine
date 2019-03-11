#include "PCH.h"
#include "Animation.h"

Animation::Animation()
{
}

Animation::~Animation()
{
}

void Animation::Init()
{
	FPS = 60.f;
	CurrentFrame = Vector2(0, 0);
	GridSize = Vector2(1, 1);
	FramesAccumulated = 0;
}


const float Animation::GetFramesAccumulated() const
{
	return FramesAccumulated;
}

void Animation::SetAnimationInfo(int InWidth, int InHeight, int InColumns, int InRows)
{
	CurrentFrame = Vector2(2, 4);
	GridSize = Vector2(InColumns, InRows);
}

Animation::State::State() : Name("Default"), FPS(60.f)
{

}
