// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>
#include "Math/Vector2.h"

class IWindow
{
private:
public:
	static int WINDOW_HEIGHT, WINDOW_WIDTH;
	IWindow(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
	~IWindow();

	virtual bool ShouldClose() = 0;
	virtual void ParseMessageQueue() = 0;
	virtual void Swap() = 0;
	virtual Vector2 GetSize() const = 0;

	virtual void SetTitle(const std::string& title) = 0;

protected:
	bool m_windowVisible;
};
