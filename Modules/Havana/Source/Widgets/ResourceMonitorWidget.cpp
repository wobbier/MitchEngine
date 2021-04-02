#include "Widgets/ResourceMonitorWidget.h"
#include <optick.h>
#include <Resource/ResourceCache.h>
#include <vector>

#if ME_EDITOR

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

void ResourceMonitorWidget::Render()
{
	if (IsOpen)
	{
		OPTICK_CATEGORY("Resource Monitor", Optick::Category::Debug);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::Begin("Resource Monitor", &IsOpen);
		{
			auto& resources = ResourceCache::GetInstance().GetResouceStack();
			std::vector<std::shared_ptr<Resource>> resourceList;
			resourceList.reserve(resources.size());

			for (auto resource : resources)
			{
				resourceList.push_back(resource.second);
			}

			ImVec2 size = ImVec2(0, ImGui::GetWindowSize().y - ImGui::GetCursorPosY());
			static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY /*| ImGuiTableFlags_ScrollFreezeTopRow*/ | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

			if (ImGui::BeginTable("##ResourceTable", 2, flags, size))
			{
				ImGui::TableSetupColumn("Resource Path", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("References", ImGuiTableColumnFlags_WidthFixed);
				//ImGui::TableAutoHeaders();
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(resourceList.size()));
				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
					{
						ImGui::TableNextRow();

						ImGui::TableSetColumnIndex(0);
						ImGui::Text(resourceList[row]->GetPath().LocalPath.c_str());
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();

							if (auto metaData = resourceList[row]->GetMetadata())
							{
								metaData->OnEditorInspect();
							}

							ImGui::EndTooltip();
						}

						ImGui::TableSetColumnIndex(1);
						ImGui::Text(std::to_string(resourceList[row].use_count()).c_str());

					}
				}
				ImGui::EndTable();
			}
			ImGui::End();
		}
		ImGui::PopStyleVar(1);
	}
}

#endif