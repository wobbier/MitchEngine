#include "ErrorListWidget.h"
#include "Scripting/ScriptEngine.h"
#include "Utils/PlatformUtils.h"
#include "ImGuiTheme.h"
#include <imgui.h>

#if USING( ME_EDITOR )

ErrorListWidget::ErrorListWidget()
	: HavanaWidget("Error List")
{
	IsOpen = false;
}


void ErrorListWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(ImGuiTheme::Em(40.f), ImGuiTheme::Em(12.f)), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(Name.c_str(), &IsOpen))
	{
		ImGui::End();
		return;
	}

#if USING( ME_SCRIPTING )
	const std::vector<ScriptEngine::BuildDiagnostic> diagnostics = ScriptEngine::GetBuildDiagnostics();
	if (diagnostics.empty())
	{
		ImGui::TextDisabled(ScriptEngine::HasBuildErrors() ? "The last script build failed: see the Log." : "No C# build errors.");
		ImGui::End();
		return;
	}
	const size_t errors = ScriptEngine::GetBuildErrorCount();
	ImGui::Text("%zu %s, %zu %s in the last script build (cleared when it builds)", errors, errors == 1 ? "error" : "errors",
		diagnostics.size() - errors, diagnostics.size() - errors == 1 ? "warning" : "warnings");

	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable("##ErrorList", 4, flags))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(4.5f));
		ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(12.f));
		ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(3.f));
		ImGui::TableHeadersRow();
		for (size_t i = 0; i < diagnostics.size(); ++i)
		{
			const ScriptEngine::BuildDiagnostic& diagnostic = diagnostics[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushStyleColor(ImGuiCol_Text, diagnostic.IsError ? ImGuiTheme::ErrorText() : ImGuiTheme::WarningText());
			if (ImGui::Selectable(diagnostic.Code.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)
				&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !diagnostic.File.empty())
			{
				PlatformUtils::OpenInCodeEditor(diagnostic.File, diagnostic.Line);
			}
			ImGui::PopStyleColor();
			if (!diagnostic.File.empty())
			{
				ImGui::SetItemTooltip("%s(%d,%d): double-click to open", diagnostic.File.c_str(), diagnostic.Line, diagnostic.Column);
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(diagnostic.Message.c_str());
			ImGui::TableNextColumn();
			const size_t slash = diagnostic.File.find_last_of('/');
			ImGui::TextUnformatted(slash == std::string::npos ? diagnostic.File.c_str() : diagnostic.File.c_str() + slash + 1);
			ImGui::TableNextColumn();
			if (diagnostic.Line > 0)
			{
				ImGui::Text("%d", diagnostic.Line);
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
#else
	ImGui::TextDisabled("C# scripting isn't in this build.");
#endif
	ImGui::End();
}

#endif
