#pragma once
#include <vector>

#include "ICommand.h"
#include <type_traits>

class CommandManager
{
public:
	void Push(ICommand* InCommand);
	void Undo();
	void Redo();

	void Draw();
	
private:
	void ClearRedoStack();
	std::vector<ICommand*> Items;
	std::vector<ICommand*> RedoStack;
};