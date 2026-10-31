#pragma once
#include <string>
#include <vector>

#include <HavanaWidget.h>
#include "Dementia.h"
#include "Engine/HardwareBudget.h"

#if USING( ME_EDITOR )

// Window > Resource Monitor: every loaded asset with what it costs (NEXT-11, HardwareBudget::
// CollectAssetCosts), most expensive first, totals by kind against the hardware profile's memory
// budgets, and a JSON report to copy.
class ResourceMonitorWidget
	: public HavanaWidget
{
	enum ResourceSortingIDs
	{
		Sort_LocalPath,
		Sort_Kind,
		Sort_Bytes,
		Sort_RefCount
	};
public:
	ResourceMonitorWidget();

	void Init() final;
	void Destroy() final;

	void Update() final;
	void Render() final;

private:
	void Refresh();
	void Sort(const ImGuiTableSortSpecs& InSpecs);
	void DrawTotals();

	std::vector<AssetCost> m_costs;
	double m_lastRefresh = -1.0;
	char m_filter[128] = {};
	int m_kind = 0;
	bool m_needsSort = true;
};

#endif
