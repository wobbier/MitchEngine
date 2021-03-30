#pragma once
#include <unordered_map>

#include "HavanaWidget.h"

#include <CLog.h>
#include <imgui.h>

class LogWidget
	: public HavanaWidget
{
public:
	LogWidget();

	void Init() final;
	void Destroy() final;

	void Update() final;
	void Render() final;

private:
	std::unordered_map<CLog::LogType, bool> DebugFilters;
	ImGuiWindowFlags WindowFlags = 0;
};