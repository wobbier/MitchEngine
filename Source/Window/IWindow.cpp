#include "PCH.h"
#include "IWindow.h"
#include "CLog.h"
#include <assert.h>

int IWindow::WINDOW_WIDTH = 960;
int IWindow::WINDOW_HEIGHT = 540;

IWindow::IWindow(std::string title, int width, int height)
{
	WINDOW_HEIGHT = height;
	WINDOW_WIDTH = width;
}

IWindow::~IWindow()
{
}