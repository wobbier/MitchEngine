#include "PCH.h"

#include "BasicFrameProfile.h"

FrameProfile::FrameProfile()
{

}

void FrameProfile::Set(ProfileCategory category)
{
	Profiles[category].Reset();
}

void FrameProfile::Complete(ProfileCategory category)
{
	Profiles[category].Update();
}

void FrameProfile::Dump()
{
	ProfileDump = Profiles;
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
	return Vector3(.1f, .1f, .1f);
}

