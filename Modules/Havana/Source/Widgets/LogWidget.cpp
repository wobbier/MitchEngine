#include "LogWidget.h"
#include <optick.h>

LogWidget::LogWidget()
	: HavanaWidget("Log")
{
}

void LogWidget::Init()
{
	WindowFlags = 0;
	WindowFlags |= ImGuiWindowFlags_MenuBar;
	WindowFlags |= ImGuiWindowFlags_NoScrollbar;
}

void LogWidget::Destroy()
{
}

void LogWidget::Update()
{
}

void LogWidget::Render()
{
	if (IsOpen)
	{
		OPTICK_CATEGORY("Log", Optick::Category::Debug);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, ImGui::GetStyle().FramePadding.y));
		if (ImGui::Begin("Log", &IsOpen, WindowFlags))
		{
			// Menu
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Filter"))
				{
					ImGui::Checkbox("Debug", &DebugFilters[CLog::LogType::Debug]);
					ImGui::Checkbox("Error", &DebugFilters[CLog::LogType::Error]);
					ImGui::Checkbox("Info", &DebugFilters[CLog::LogType::Info]);
					ImGui::EndMenu();
				}

				ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 100.f);
				if (ImGui::Button("Clear Log", { 100.f, 0.f }))
				{
					CLog::Messages.clear();
				}

				ImGui::EndMenuBar();
			}

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(CLog::Messages.size()), -1.f);

            static ImGuiTableFlags flags =
                ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
                | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti
                | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody
                | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_SizingStretchProp;

			static float wOffset = 350.0f;
			static float hOffset = 30.f;
			static float h = ImGui::GetContentRegionAvail().y;
			static int selectedIndex = -1;
			float w = ImGui::GetContentRegionAvail().x;
			if (selectedIndex >= 0 && selectedIndex < static_cast<int>(CLog::Messages.size()))
			{
				h = ImGui::GetContentRegionAvail().y - hOffset;
			}
			else
			{
				h = ImGui::GetContentRegionAvail().y;
			}
			ImVec2 size = ImVec2(w, h);

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
			if (ImGui::BeginTable("##LogWidgetTable", 2, flags, size))
			{
				ImGui::PopStyleVar(1);

				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 15.f);
				ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 100.0f);
				ImGui::TableSetupScrollFreeze(1, 1);
				ImGui::TableHeadersRow();

				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, 16.f);
                        ImGui::TableSetColumnIndex(0);
						{
							ImGui::Text(CLog::GetInstance().TypeToName(CLog::Messages[line_no].Type).c_str());
						}

                        ImGui::TableSetColumnIndex(1);
						{
							bool selected = selectedIndex == line_no;
                            ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_AllowDoubleClick;
							if (ImGui::Selectable(CLog::Messages[line_no].Message.c_str(), &selected, selectable_flags, ImVec2(0.f, 15.f)))
							{
								selectedIndex = line_no;
							}
						}
					}
				}
				ImGui::EndTable();
			}
			else
			{
				ImGui::PopStyleVar(1);
			}
			clipper.End();

			if (selectedIndex >= 0 && selectedIndex < static_cast<int>(CLog::Messages.size()))
			{
				ImGui::Button("hsplitter", ImVec2(w, 4.f));
				if (ImGui::IsItemHovered())
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
				}
				if (ImGui::IsItemActive())
				{
					hOffset -= ImGui::GetIO().MouseDelta.y;
				}
				ImGui::TextWrapped(CLog::Messages[selectedIndex].Message.c_str());
			}
			//bool showMessage = true;
			//for (auto& msg : Logger::Messages)
			//{
			//	if (DebugFilters.find(msg.Type) == DebugFilters.end())
			//	{
			//		DebugFilters[msg.Type] = true;
			//	}
			//	if (DebugFilters[msg.Type])
			//	{
			//		ImGui::Text(msg.Message.c_str());
			//	}
			//}
		}

		ImGui::PopStyleVar(2);

		ImGui::End();
	}
}