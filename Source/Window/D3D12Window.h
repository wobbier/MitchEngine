// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>

#include "IWindow.h"

#if ME_PLATFORM_WIN64

class D3D12Window
	: public IWindow
{
public:
	D3D12Window(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
	~D3D12Window();

	virtual bool ShouldClose() final;
	virtual void ParseMessageQueue() final;
	virtual void Swap() final;

	Vector2 GetSize() const;

private:
	bool ExitRequested = false;

	HWND Window;
};

#endif