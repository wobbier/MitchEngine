#pragma once
#include "Singleton.h"
#include "Engine/Clock.h"
#include "Math/Vector3.h"

enum class ProfileCategory
{
	Game = 0,
	UI,
	Rendering
};

class FrameProfile
{
	ME_SINGLETON_DEFINITION(FrameProfile);

public:
	FrameProfile();

	void Start();

	void Set(ProfileCategory category);

	void Complete(ProfileCategory category);

	void Dump();

	Vector3 GetCategoryColor(ProfileCategory category);

	std::map<ProfileCategory, Clock> Profiles;
	std::map<ProfileCategory, Clock> ProfileDump;
};

class FrameProfileObject
{
public:
	FrameProfileObject(ProfileCategory cat)
		: m_cat(cat)
	{
		FrameProfile::GetInstance().Set(cat);
	}

	~FrameProfileObject()
	{
		FrameProfile::GetInstance().Complete(m_cat);
	}
	ProfileCategory m_cat;
};

#define ME_PROFILE(cat) FrameProfileObject(cat);