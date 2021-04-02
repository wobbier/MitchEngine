#pragma once
#include <HavanaWidget.h>

#if ME_EDITOR

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

#endif