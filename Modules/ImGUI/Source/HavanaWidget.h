#pragma once
#include <imgui.h>
#include <string>

class HavanaWidget
{
public:
	HavanaWidget(const std::string& inName, const std::string& inHotkey = "")
		: Name(inName)
		, Hotkey(inHotkey)
	{
	}

	virtual void Init() = 0;
	virtual void Destroy() = 0;

	virtual void Update() = 0;
	virtual void Render() = 0;

	bool IsOpen = true;
	std::string Name;
	std::string Hotkey;
};