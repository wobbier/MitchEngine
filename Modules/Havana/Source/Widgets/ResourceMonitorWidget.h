#pragma once
#include <HavanaWidget.h>

class ResourceMonitorWidget
	: public HavanaWidget
{
public:
	ResourceMonitorWidget();

	void Init() final;
	void Destroy() final;

	void Update() final;
	void Render() final;

private:
	ImGuiWindowFlags WindowFlags = 0;
};