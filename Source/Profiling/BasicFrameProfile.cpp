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

