#include "HistoryWidget.h"
#include "Editor/UndoStack.h"
#include <imgui.h>

#if USING( ME_EDITOR )

HistoryWidget::HistoryWidget()
	: HavanaWidget("History")
{
	IsOpen = false;
}


void HistoryWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}
	if (!ImGui::Begin(Name.c_str(), &IsOpen))
	{
		ImGui::End();
		return;
	}

	UndoStack& stack = UndoStack::Get();
	const std::vector<std::string> history = stack.GetHistory();
	const int cursor = stack.GetCursor();

	ImGui::BeginDisabled(!stack.CanUndo());
	if (ImGui::Button("Undo"))
	{
		stack.Undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!stack.CanRedo());
	if (ImGui::Button("Redo"))
	{
		stack.Redo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled(stack.IsDirty() ? "(unsaved changes)" : "(saved)");
	ImGui::Separator();

	ImGui::BeginChild("##HistoryList");
	int jumpTo = -1;
	if (ImGui::Selectable("<Scene Opened>", cursor == 0))
	{
		jumpTo = 0;
	}
	for (int i = 0; i < static_cast<int>(history.size()); ++i)
	{
		const bool applied = i < cursor;
		ImGui::PushID(i);
		if (!applied)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}
		if (ImGui::Selectable(history[i].c_str(), i + 1 == cursor))
		{
			jumpTo = i + 1;
		}
		if (!applied)
		{
			ImGui::PopStyleColor();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	if (jumpTo >= 0)
	{
		stack.JumpTo(jumpTo);
	}
	ImGui::End();
}

#endif
