#pragma once
#include "Singleton.h"
#include "Engine/Clock.h"
#include "Math/Vector3.h"
#include "imgui.h"

enum class ProfileCategory : std::size_t
{
	Game = 0,
	UI,
	Physics,
	Rendering,
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
#if ME_EDITOR
	static constexpr int kMinProfilerSize = 12;
#else
	static constexpr int kMinProfilerSize = 5;
#endif
	static constexpr int kMaxProfilerSize = 40;

	FrameProfile();

	void Start();
	void End(float frameDelta);
	void Dump();

	ImVec4 GetCategoryColor(ProfileCategory category);

	std::map<std::string, ProfileInfo> Profiles;
	std::map<std::string, ProfileInfo> ProfileDump;

	float MainDelta = 0.f;

	void Set(const std::string& name, ProfileCategory category);
	void Complete(const std::string& name);

	void Render(const Vector2& inPosition, const Vector2& inSize);
private:
	int CurrentProfilerSize = kMinProfilerSize;
	int PreviousTooltipHeight = 0;
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