#include "PCH.h"
#include <Window/SDLWindow.h>
#include "bgfx/platform.h"
#include "CLog.h"

SDLWindow::SDLWindow(const std::string& title, std::function<void(const Vector2&)> resizeFunc, int X, int Y, Vector2 windowSize)
	: ResizeCB(resizeFunc)
{
	SDL_Init(0);
	WindowHandle = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_UNDEFINED, windowSize.x, windowSize.y, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	if (WindowHandle == nullptr) {
		printf("Window could not be created. SDL_Error: %s\n", SDL_GetError());
	}
	SetWindow(WindowHandle);
}

bool SDLWindow::ShouldClose()
{
	return false;
}

void SDLWindow::ParseMessageQueue()
{
	bool exit = false;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {

		switch (event.type) {
		case SDL_QUIT:
			exit = true;
			break;

		case SDL_WINDOWEVENT: {
			const SDL_WindowEvent& wev = event.window;
			switch (wev.event) {
			case SDL_WINDOWEVENT_RESIZED:
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				ResizeCB(GetSize());
				break;

			case SDL_WINDOWEVENT_CLOSE:
				exit = true;
				break;
			}
		} break;
		}
	}
}

void SDLWindow::Swap()
{
}

Vector2 SDLWindow::GetSize() const
{
	int width = 0;
	int height = 0;
	SDL_GetWindowSize(WindowHandle, &width, &height);

	return Vector2(width, height);
}

Vector2 SDLWindow::GetPosition()
{
	int x, y;
	SDL_GetWindowPosition(WindowHandle, &x, &y);

	return Vector2(x, y);
}

bool SDLWindow::IsFullscreen()
{
	return false;
}

void SDLWindow::Maximize()
{
}

void SDLWindow::Minimize()
{
}

void SDLWindow::ExitMaximize()
{
}

void SDLWindow::SetTitle(const std::string& title)
{
}

void SDLWindow::Exit()
{
}

void* SDLWindow::GetWindowPtr()
{
	return PlatformInfo.nwh;
}

void SDLWindow::CanMoveWindow(bool param1)
{
}

void SDLWindow::SetWindow(SDL_Window* window)
{
	SDL_SysWMinfo wmi;

	SDL_VERSION(&wmi.version);
	if (!SDL_GetWindowWMInfo(window, &wmi))
	{
		return;
	}

#if ME_PLATFORM_WIN64
	PlatformInfo.ndt = nullptr;
	PlatformInfo.nwh = wmi.info.win.window;
#endif
	PlatformInfo.context = nullptr;
	PlatformInfo.backBuffer = nullptr;
	PlatformInfo.backBufferDS = nullptr;

	//bgfx::setPlatformData(PlatformInfo);
}

