#include "PCH.h"
#include <Window/SDLWindow.h>
#include "bgfx/platform.h"
#include "CLog.h"
#include "Engine/Input.h"

#define RESIZE_BORDER 20
SDL_HitTestResult HitTestCallback(SDL_Window* window, const SDL_Point* area, void* data)
{
	SDLWindow* engineWindow = static_cast<SDLWindow*>(data);
	if (engineWindow->CustomDragCB)
	{
		if (auto result = engineWindow->CustomDragCB(Vector2(area->x, area->y)))
		{
			return *result;
		}
	}

	int w, h;
	SDL_GetWindowSize(window, &w, &h);

	#define REPORT_RESIZE_HIT(name) {\
		SDL_Log("HIT-TEST: RESIZE_" #name "\n"); \
		return SDL_HITTEST_RESIZE_##name; \
	}
	if (area->x < RESIZE_BORDER && area->y < RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(TOPLEFT);
	}
	else if (area->x > RESIZE_BORDER&& area->x < w - RESIZE_BORDER && area->y < RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(TOP);
	}
	else if (area->x > w - RESIZE_BORDER && area->y < RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(TOPRIGHT);
	}
	else if (area->x > w - RESIZE_BORDER && area->y > RESIZE_BORDER&& area->y < h - RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(RIGHT);
	}
	else if (area->x > w - RESIZE_BORDER && area->y > h - RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(BOTTOMRIGHT);
	}
	else if (area->x < w - RESIZE_BORDER && area->x > RESIZE_BORDER&& area->y > h - RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(BOTTOM);
	}
	else if (area->x < RESIZE_BORDER && area->y > h - RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(BOTTOMLEFT);
	}
	else if (area->x < RESIZE_BORDER && area->y < h - RESIZE_BORDER && area->y > RESIZE_BORDER)
	{
		REPORT_RESIZE_HIT(LEFT);
	}

	return SDL_HITTEST_NORMAL;
}

SDLWindow::SDLWindow(const std::string& title, std::function<void(const Vector2&)> resizeFunc, int X, int Y, Vector2 windowSize)
	: ResizeCB(resizeFunc)
{
	SDL_Init(0);
	WindowHandle = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_UNDEFINED, windowSize.x, windowSize.y, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	if (WindowHandle == nullptr) {
		printf("Window could not be created. SDL_Error: %s\n", SDL_GetError());
	}
	SetWindow(WindowHandle);
	SDL_SetWindowHitTest(WindowHandle, HitTestCallback, this);
}

bool SDLWindow::ShouldClose()
{
	return CloseRequested;
}

extern bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event);
void SDLWindow::ParseMessageQueue()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (ImGui_ImplSDL2_ProcessEvent(&event))
		{
			return;
		}
		switch (event.type)
		{
		case SDL_QUIT:
		{
			CloseRequested = true;
			break;
		}

		case SDL_MOUSEWHEEL:
		{
			MouseScrollEvent evt(event.wheel.x, event.wheel.y);
			evt.Fire();
			break;
		}

		case SDL_WINDOWEVENT:
		{
			const SDL_WindowEvent& wev = event.window;
			switch (wev.event) {
			case SDL_WINDOWEVENT_RESIZED:
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				ResizeCB(GetSize());
				break;

			case SDL_WINDOWEVENT_CLOSE:
				CloseRequested = true;
				break;
			}
			break;
		}
		default:
			break;
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
	SDL_MaximizeWindow(WindowHandle);
}

void SDLWindow::Minimize()
{
	SDL_MinimizeWindow(WindowHandle);
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

void SDLWindow::SetBorderless(bool borderless)
{
	SDL_SetWindowBordered(WindowHandle, (SDL_bool)!borderless);
}

void SDLWindow::SetCustomDragCallback(std::function<std::optional<SDL_HitTestResult>(const Vector2&)> cb)
{
	CustomDragCB = cb;
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