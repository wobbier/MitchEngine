#pragma once
#include "Singleton.h"
#include "Engine/Clock.h"
#include "Math/Vector3.h"

enum class ProfileCategory : std::size_t
{
	Game = 0,
	UI,
	Rendering,
	Physics,
	Count
};

class ProfileInfo
{
public:
	ProfileCategory Category = ProfileCategory::Count;
	Clock Timer;
};

class FrameProfile
{
	friend class FrameProfileObject;

	ME_SINGLETON_DEFINITION(FrameProfile);

public:
	FrameProfile();

	void Start();
	void End(float frameDelta);
	void Dump();

	Vector3 GetCategoryColor(ProfileCategory category);

	std::map<std::string, ProfileInfo> Profiles;
	std::map<std::string, ProfileInfo> ProfileDump;

	float MainDelta = 0.f;

	void Set(const std::string& name, ProfileCategory category);
	void Complete(const std::string& name);
};

class FrameProfileObject
{
public:
	FrameProfileObject(const std::string& name, ProfileCategory cat)
		: m_cat(cat)
		, m_name(name)
	{
		FrameProfile::GetInstance().Set(name, cat);
	}

	~FrameProfileObject()
	{
		FrameProfile::GetInstance().Complete(m_name);
	}
	ProfileCategory m_cat;
	std::string m_name;
};

#define ME_PROFILE(name, cat) FrameProfileObject(name, cat);