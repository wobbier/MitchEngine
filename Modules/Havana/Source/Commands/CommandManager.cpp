#include "CommandManager.h"

#include "imgui.h"

void CommandManager::Push(ICommand* InCommand)
{
	InCommand->Do();
	Items.push_back(std::move(InCommand));

	ClearRedoStack();
}

void CommandManager::Undo()
{
	if (Items.size() == 0)
	{
		return;
	}
	Items.back()->Undo();
	RedoStack.push_back(Items.back());
	Items.pop_back();
}

void CommandManager::Redo()
{
	if (RedoStack.size() == 0)
	{
		return;
	}
	RedoStack.back()->Do();
	Items.push_back(RedoStack.back());
	RedoStack.pop_back();
}

void CommandManager::Draw()
{
	ImGui::Begin("History");

	for (auto& cmd : RedoStack)
	{
		ImGui::Text(cmd->Name.c_str());
	}

	ImGui::NewLine();

	for (auto& cmd : Items)
	{
		ImGui::Text(cmd->Name.c_str());
	}

	ImGui::End();
}

void CommandManager::ClearRedoStack()
{
	for (auto ptr : RedoStack)
	{
		delete ptr;
	}

	RedoStack.clear();
}

