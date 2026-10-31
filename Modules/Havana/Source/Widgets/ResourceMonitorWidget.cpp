#include "Widgets/ResourceMonitorWidget.h"
#include <optick.h>
#include <ImGuiTheme.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <map>

#if USING( ME_EDITOR )

namespace
{
	const char* kKinds[] = { "All", "Texture", "Render Texture", "Model", "Sound", "Other" };


	bool ContainsNoCase(const std::string& InText, const char* InNeedle)
	{
		if (!InNeedle[0])
		{
			return true;
		}
		const std::string needle(InNeedle);
		return std::search(InText.begin(), InText.end(), needle.begin(), needle.end(), [](char InA, char InB) {
			return std::tolower(static_cast<unsigned char>(InA)) == std::tolower(static_cast<unsigned char>(InB));
		}) != InText.end();
	}
}


ResourceMonitorWidget::ResourceMonitorWidget()
	: HavanaWidget("Resource Monitor")
{
}


void ResourceMonitorWidget::Init()
{
}


void ResourceMonitorWidget::Destroy()
{
}


void ResourceMonitorWidget::Update()
{
}


void ResourceMonitorWidget::Refresh()
{
	m_costs = HardwareBudget::CollectAssetCosts();
	m_lastRefresh = ImGui::GetTime();
	m_needsSort = true;
}


void ResourceMonitorWidget::Sort(const ImGuiTableSortSpecs& InSpecs)
{
	std::stable_sort(m_costs.begin(), m_costs.end(), [&InSpecs](const AssetCost& InA, const AssetCost& InB) {
		for (int n = 0; n < InSpecs.SpecsCount; ++n)
		{
			const ImGuiTableColumnSortSpecs& spec = InSpecs.Specs[n];
			int delta = 0;
			switch (spec.ColumnUserID)
			{
			case Sort_LocalPath: delta = InA.Path.compare(InB.Path); break;
			case Sort_Kind: delta = InA.Kind.compare(InB.Kind); break;
			case Sort_Bytes: delta = InA.Bytes == InB.Bytes ? 0 : (InA.Bytes < InB.Bytes ? -1 : 1); break;
			case Sort_RefCount: delta = InA.References == InB.References ? 0 : (InA.References < InB.References ? -1 : 1); break;
			default: break;
			}
			if (delta != 0)
			{
				return spec.SortDirection == ImGuiSortDirection_Ascending ? delta < 0 : delta > 0;
			}
		}
		return InA.Path < InB.Path;
	});
}


void ResourceMonitorWidget::DrawTotals()
{
	// Per kind, against the profile's memory budget for it when there is one.
	std::map<std::string, std::pair<uint64_t, int>> kinds;
	uint64_t total = 0;
	for (const AssetCost& cost : m_costs)
	{
		auto& kind = kinds[cost.Kind];
		kind.first += cost.Bytes;
		++kind.second;
		total += cost.Bytes;
	}
	const HardwareProfile* profile = HardwareBudget::Get().GetActiveProfile();
	auto budgetFor = [profile](const std::string& InKind) -> uint64_t {
		if (!profile)
		{
			return 0;
		}
		const int mib = InKind == "Texture" ? profile->TextureMiB : InKind == "Render Texture" ? profile->RenderTargetMiB : InKind == "Model" ? profile->MeshMiB : InKind == "Sound" ? profile->AudioMiB : 0;
		return static_cast<uint64_t>(mib) * 1024ull * 1024ull;
	};
	ImGui::Text("%zu assets, %s", m_costs.size(), HardwareBudget::FormatBytes(total).c_str());
	for (const auto& [kind, sum] : kinds)
	{
		if (sum.first == 0)
		{
			continue;
		}
		ImGui::SameLine(0.f, 16.f);
		const uint64_t limit = budgetFor(kind);
		const bool over = limit > 0 && sum.first > limit;
		const ImVec4 color = over ? ImGuiTheme::ErrorText() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
		if (limit > 0)
		{
			ImGui::TextColored(color, "%s %s / %s", kind.c_str(), HardwareBudget::FormatBytes(sum.first).c_str(), HardwareBudget::FormatBytes(limit).c_str());
			ImGui::SetItemTooltip("%d %s assets; the %s profile's budget is %s", sum.second, kind.c_str(), profile->Name.c_str(), HardwareBudget::FormatBytes(limit).c_str());
		}
		else
		{
			ImGui::TextColored(color, "%s %s", kind.c_str(), HardwareBudget::FormatBytes(sum.first).c_str());
			ImGui::SetItemTooltip("%d %s assets", sum.second, kind.c_str());
		}
	}
}


void ResourceMonitorWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}
	OPTICK_CATEGORY("Resource Monitor", Optick::Category::UI);
	const bool open = ImGui::Begin("Resource Monitor", &IsOpen);
	if (open)
	{
		// Costs move as assets load and unload: refresh about once a second.
		if (m_lastRefresh < 0.0 || ImGui::GetTime() - m_lastRefresh > 1.0)
		{
			Refresh();
		}
		ImGui::SetNextItemWidth(220.f);
		ImGui::InputTextWithHint("##ResourceFilter", "Filter by path", m_filter, sizeof(m_filter));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.f);
		ImGui::Combo("##ResourceKind", &m_kind, kKinds, IM_ARRAYSIZE(kKinds));
		ImGui::SameLine();
		if (ImGui::Button("Copy Report"))
		{
			ImGui::SetClipboardText(HardwareBudget::AssetCostsToJson(m_costs, 100).dump(4).c_str());
		}
		ImGui::SetItemTooltip("The 100 most expensive assets and the totals by kind, as JSON");
		DrawTotals();
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("Estimates: textures at their uploaded size, models' vertex and index buffers, sounds' files.");
		ImGui::PopStyleColor();

		const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
			| ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody
			| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##ResourceTable", 5, flags, ImGui::GetContentRegionAvail()))
		{
			ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 3.f, Sort_LocalPath);
			ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 0.8f, Sort_Kind);
			ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthStretch, 0.7f, Sort_Bytes);
			ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.6f);
			ImGui::TableSetupColumn("References", ImGuiTableColumnFlags_WidthStretch, 0.6f, Sort_RefCount);
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableHeadersRow();

			if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && (specs->SpecsDirty || m_needsSort))
			{
				Sort(*specs);
				specs->SpecsDirty = false;
				m_needsSort = false;
			}

			std::vector<const AssetCost*> rows;
			rows.reserve(m_costs.size());
			for (const AssetCost& cost : m_costs)
			{
				if ((m_kind == 0 || cost.Kind == kKinds[m_kind]) && ContainsNoCase(cost.Path, m_filter))
				{
					rows.push_back(&cost);
				}
			}
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const AssetCost& cost = *rows[row];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::PushID(row);
					// The file name first: paths share long prefixes and the column is often narrow.
					const size_t slash = cost.Path.find_last_of('/');
					const char* name = slash == std::string::npos ? cost.Path.c_str() : cost.Path.c_str() + slash + 1;
					ImGui::Selectable(name, false, ImGuiSelectableFlags_SpanAllColumns);
					ImGui::SetItemTooltip("%s", cost.Path.c_str());
					if (ImGui::BeginPopupContextItem("ResourceContext"))
					{
						if (ImGui::MenuItem("Copy Path"))
						{
							ImGui::SetClipboardText(cost.Path.c_str());
						}
						ImGui::EndPopup();
					}
					ImGui::PopID();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(cost.Kind.c_str());
					ImGui::TableNextColumn();
					if (cost.Bytes > 0)
					{
						ImGui::TextUnformatted(HardwareBudget::FormatBytes(cost.Bytes).c_str());
					}
					else
					{
						ImGui::TextDisabled("-");
					}
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(cost.Detail.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%ld", cost.References);
				}
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}

#endif
