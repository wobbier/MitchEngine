// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>

#include "IWindow.h"

#if ME_DIRECTX && !ME_PLATFORM_UWP

class D3D12Window
	: public IWindow
{
public:
	D3D12Window(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
	~D3D12Window();

	virtual bool ShouldClose() final;
	virtual void ParseMessageQueue() final;
	virtual void Swap() final;

private:
	bool ExitRequested = false;

	HWND Window;
};

#endif