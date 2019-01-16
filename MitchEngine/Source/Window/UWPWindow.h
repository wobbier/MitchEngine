// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>

#include "IWindow.h"

#if ME_PLATFORM_UWP

class UWPWindow
	: public IWindow
{
public:
	UWPWindow(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
	~UWPWindow();

	virtual bool ShouldClose() final;
	virtual void ParseMessageQueue() final;
	virtual void Swap() final;

private:
	bool ExitRequested = false;
};

#endif