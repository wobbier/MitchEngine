#include "PCH.h"

#include "BasicFrameProfile.h"

FrameProfile::FrameProfile()
{
}

void FrameProfile::Start()
{
}

void FrameProfile::End(float frameDelta)
{
	MainDelta = frameDelta;
}

void FrameProfile::Set(const std::string& name, ProfileCategory category)
{
	Profiles[name].Category = category;
	Profiles[name].Timer.Reset();
}

void FrameProfile::Complete(const std::string& name)
{
	Profiles[name].Timer.Update();
}

void FrameProfile::Dump()
{
	ProfileDump = Profiles;
	//std::sort(ProfileDump.begin(), ProfileDump.end(), [](const Clock& First, const Clock& Second) {
	//	return First.GetPreviousTime() < Second.GetPreviousTime();
	//});
}

Vector3 FrameProfile::GetCategoryColor(ProfileCategory category)
{
	switch (category)
	{
	case ProfileCategory::Game:
		return Vector3(0, 175, 84) / 255.f;
		break;
	case ProfileCategory::UI:
		return Vector3(153, 57, 85) / 255.f;
		break;
	case ProfileCategory::Rendering:
		return Vector3(25, 25, 35) / 255.f;
		break;
	default:
		break;
	}
	return Vector3(.1f, .1f, 1.f);
}

