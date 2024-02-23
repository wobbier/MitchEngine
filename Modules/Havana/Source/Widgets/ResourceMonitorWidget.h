#pragma once
#include <vector>

#include <HavanaWidget.h>
#include "Pointers.h"
#include "Dementia.h"

#if USING( ME_EDITOR )

class Resource;

class ResourceMonitorWidget
	: public HavanaWidget
{

	enum ResourceSortingIDs
	{
		Sort_LocalPath,
		Sort_RefCount
	};
public:
	ResourceMonitorWidget();

	void Init() final;
	void Destroy() final;

	void Update() final;
	void Render() final;

private:
	ImGuiWindowFlags WindowFlags = 0;
	bool ItemsNeedSorted = true;
	bool ItemsNeedGenerated = true;
	static const ImGuiTableSortSpecs* s_SortSpecs;

	static int CompareWithSortSpecs(const void* lhs, const void* rhs);
	std::vector<WeakPtr<Resource>> resourceList;
};

#endif