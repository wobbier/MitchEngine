#pragma once
#include <string>

class ICommand
{
public:
	virtual void Do() {};
	virtual void Undo() {};

	std::string Name;
};