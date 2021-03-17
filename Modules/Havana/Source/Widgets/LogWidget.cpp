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
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.f, 0.f));
		if (ImGui::Begin("Log", &IsOpen, WindowFlags))
		{
			// Menu
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Show"))
				{
					ImGui::Checkbox("Debug", &DebugFilters[CLog::LogType::Debug]);
					ImGui::Checkbox("Error", &DebugFilters[CLog::LogType::Error]);
					ImGui::Checkbox("Info", &DebugFilters[CLog::LogType::Info]);
					ImGui::EndMenu();
				}

				if (ImGui::Button("Clear Log"))
				{
					CLog::Messages.clear();
				}

				ImGui::EndMenuBar();
			}

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(CLog::Messages.size()), 12.f);

			static float currentHeight = 0.f;
			ImVec2 size = ImVec2(0, (ImGui::GetWindowSize().y - currentHeight) - ImGui::GetCursorPosY());
			static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY /*| ImGuiTableFlags_ScrollFreezeTopRow*/ | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

			const int COLUMNS_COUNT = 1;
			static int selectedIndex = -1;
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
			if (ImGui::BeginTable("##table1ss", COLUMNS_COUNT, flags, size))
			{
				ImGui::PopStyleVar(1);

				ImGui::TableSetupColumn("Message");

				// Dummy entire-column selection storage
				// FIXME: It would be nice to actually demonstrate full-featured selection using those checkbox.
				static bool column_selected[1] = {};

				// Instead of calling TableAutoHeaders() we'll submit custom headers ourselves
				ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
				for (int column = 0; column < COLUMNS_COUNT; column++)
				{
					ImGui::TableSetColumnIndex(column);
					const char* column_name = ImGui::TableGetColumnName(column); // Retrieve name passed to TableSetupColumn()
					ImGui::PushID(column);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					ImGui::Checkbox("##checkall1", &column_selected[column]);
					ImGui::PopStyleVar();
					ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
					ImGui::TableHeader(column_name);
					ImGui::PopID();
				}
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						ImGui::TableNextRow(0, 12.f);
						for (int column = 0; column < 1; column++)
						{
							ImGui::TableSetColumnIndex(column);
							if (ImGui::Selectable(CLog::Messages[line_no].Message.c_str(), column_selected[column], 0, ImVec2(0.f, 12.f)))
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

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.f, 5.f));
			if (selectedIndex >= 0 && selectedIndex < static_cast<int>(CLog::Messages.size()))
			{
				ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
				ImGui::BeginChild("ChildL", ImVec2(ImGui::GetWindowContentRegionWidth(), 150), false, window_flags);
				ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionWidth() - 10.f);
				ImGui::TextWrapped(CLog::Messages[selectedIndex].Message.c_str());
				ImGui::PopTextWrapPos();
				currentHeight = ImGui::GetWindowHeight();
				ImGui::EndChild();
			}
			ImGui::PopStyleVar(1);
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

		ImGui::PopStyleVar(1);

		ImGui::End();

	}
}