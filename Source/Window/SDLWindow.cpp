#include "PCH.h"
#include <Window/SDLWindow.h>
#include "bgfx/platform.h"
#include "CLog.h"
#include "Engine/Input.h"

#define RESIZE_BORDER 10
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

	if (engineWindow->IsMaximized() || engineWindow->IsFullscreen())
	{
		return SDL_HITTEST_NORMAL;
	}

	int w, h;
	SDL_GetWindowSize(window, &w, &h);

		//SDL_Log("HIT-TEST: RESIZE_" #name "\n");
	#define REPORT_RESIZE_HIT(name) {\
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
	WindowHandle = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_UNDEFINED, static_cast<int>(windowSize.x), static_cast<int>(windowSize.y), SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	if (WindowHandle == nullptr) {
		printf("Window could not be created. SDL_Error: %s\n", SDL_GetError());
	}
	SetWindow(WindowHandle);
	SDL_SetWindowHitTest(WindowHandle, HitTestCallback, this);

	// #TODO Icon support
	//SDL_Surface* surface;     // Declare an SDL_Surface to be filled in with pixel data from an image file
	//Uint16 pixels[16 * 16] = {  // ...or with raw pixel data:
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0aab, 0x0789, 0x0bcc, 0x0eee, 0x09aa, 0x099a, 0x0ddd,
	//  0x0fff, 0x0eee, 0x0899, 0x0fff, 0x0fff, 0x1fff, 0x0dde, 0x0dee,
	//  0x0fff, 0xabbc, 0xf779, 0x8cdd, 0x3fff, 0x9bbc, 0xaaab, 0x6fff,
	//  0x0fff, 0x3fff, 0xbaab, 0x0fff, 0x0fff, 0x6689, 0x6fff, 0x0dee,
	//  0xe678, 0xf134, 0x8abb, 0xf235, 0xf678, 0xf013, 0xf568, 0xf001,
	//  0xd889, 0x7abc, 0xf001, 0x0fff, 0x0fff, 0x0bcc, 0x9124, 0x5fff,
	//  0xf124, 0xf356, 0x3eee, 0x0fff, 0x7bbc, 0xf124, 0x0789, 0x2fff,
	//  0xf002, 0xd789, 0xf024, 0x0fff, 0x0fff, 0x0002, 0x0134, 0xd79a,
	//  0x1fff, 0xf023, 0xf000, 0xf124, 0xc99a, 0xf024, 0x0567, 0x0fff,
	//  0xf002, 0xe678, 0xf013, 0x0fff, 0x0ddd, 0x0fff, 0x0fff, 0xb689,
	//  0x8abb, 0x0fff, 0x0fff, 0xf001, 0xf235, 0xf013, 0x0fff, 0xd789,
	//  0xf002, 0x9899, 0xf001, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0xe789,
	//  0xf023, 0xf000, 0xf001, 0xe456, 0x8bcc, 0xf013, 0xf002, 0xf012,
	//  0x1767, 0x5aaa, 0xf013, 0xf001, 0xf000, 0x0fff, 0x7fff, 0xf124,
	//  0x0fff, 0x089a, 0x0578, 0x0fff, 0x089a, 0x0013, 0x0245, 0x0eff,
	//  0x0223, 0x0dde, 0x0135, 0x0789, 0x0ddd, 0xbbbc, 0xf346, 0x0467,
	//  0x0fff, 0x4eee, 0x3ddd, 0x0edd, 0x0dee, 0x0fff, 0x0fff, 0x0dee,
	//  0x0def, 0x08ab, 0x0fff, 0x7fff, 0xfabc, 0xf356, 0x0457, 0x0467,
	//  0x0fff, 0x0bcd, 0x4bde, 0x9bcc, 0x8dee, 0x8eff, 0x8fff, 0x9fff,
	//  0xadee, 0xeccd, 0xf689, 0xc357, 0x2356, 0x0356, 0x0467, 0x0467,
	//  0x0fff, 0x0ccd, 0x0bdd, 0x0cdd, 0x0aaa, 0x2234, 0x4135, 0x4346,
	//  0x5356, 0x2246, 0x0346, 0x0356, 0x0467, 0x0356, 0x0467, 0x0467,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
	//  0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff
	//};
	//surface = SDL_CreateRGBSurfaceFrom(pixels, 16, 16, 16, 16 * 2, 0x0f00, 0x00f0, 0x000f, 0xf000);

	//// The icon is attached to the window pointer
	//SDL_SetWindowIcon(WindowHandle, surface);

	//// ...and the surface containing the icon pixel data is no longer required.
	//SDL_FreeSurface(surface);
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
			//return;
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
			MouseScrollEvent evt(static_cast<float>(event.wheel.x), static_cast<float>(event.wheel.y));
			evt.Fire();
			break;
		}

		case SDL_WINDOWEVENT:
		{
			HandleWindowEvent(event.window);
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
	if (isMaximized)
	{
		SDL_RestoreWindow(WindowHandle);
	}
}

void SDLWindow::SetTitle(const std::string& title)
{
}

void SDLWindow::Exit()
{
	CloseRequested = true;
}

void* SDLWindow::GetWindowPtr()
{
	return PlatformInfo.nwh;
}

void SDLWindow::CanMoveWindow(bool param1)
{
}

void SDLWindow::SetBorderless(bool isBorderless)
{
	SDL_SetWindowBordered(WindowHandle, (SDL_bool)!isBorderless);
}

bool SDLWindow::IsMaximized()
{
	return isMaximized;
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
#if ME_PLATFORM_MACOS
    PlatformInfo.ndt = nullptr;
    PlatformInfo.nwh = wmi.info.cocoa.window;
#endif
	PlatformInfo.context = nullptr;
	PlatformInfo.backBuffer = nullptr;
	PlatformInfo.backBufferDS = nullptr;
}

void SDLWindow::HandleWindowEvent(const SDL_WindowEvent& event)
{
	if (event.type != SDL_WINDOWEVENT)
	{
		YIKES("Event Type Mismatch: Expected WindowEvent");
		return;
	}
	switch (event.event) {
	case SDL_WINDOWEVENT_SHOWN:
		SDL_Log("Window %d shown", event.windowID);
		break;
	case SDL_WINDOWEVENT_HIDDEN:
		SDL_Log("Window %d hidden", event.windowID);
		break;
	case SDL_WINDOWEVENT_EXPOSED:
		SDL_Log("Window %d exposed", event.windowID);
		break;
	case SDL_WINDOWEVENT_MOVED:
		SDL_Log("Window %d moved to %d,%d",
			event.windowID, event.data1,
			event.data2);
		break;
	case SDL_WINDOWEVENT_RESIZED:
		ResizeCB(GetSize());
		SDL_Log("Window %d resized to %dx%d",
			event.windowID, event.data1,
			event.data2);
		break;
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		ResizeCB(GetSize());
		SDL_Log("Window %d size changed to %dx%d",
			event.windowID, event.data1,
			event.data2);
		break;
	case SDL_WINDOWEVENT_MINIMIZED:
		SDL_Log("Window %d minimized", event.windowID);
		break;
	case SDL_WINDOWEVENT_MAXIMIZED:
		isMaximized = true;
		SDL_Log("Window %d maximized", event.windowID);
		break;
	case SDL_WINDOWEVENT_RESTORED:
		isMaximized = false;
		SDL_Log("Window %d restored", event.windowID);
		break;
	case SDL_WINDOWEVENT_ENTER:
		SDL_Log("Mouse entered window %d",
			event.windowID);
		break;
	case SDL_WINDOWEVENT_LEAVE:
		SDL_Log("Mouse left window %d", event.windowID);
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
		SDL_Log("Window %d gained keyboard focus",
			event.windowID);
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
		SDL_Log("Window %d lost keyboard focus",
			event.windowID);
		break;
	case SDL_WINDOWEVENT_CLOSE:
		CloseRequested = true;
		SDL_Log("Window %d closed", event.windowID);
		break;
#if SDL_VERSION_ATLEAST(2, 0, 5)
	case SDL_WINDOWEVENT_TAKE_FOCUS:
		SDL_Log("Window %d is offered a focus", event.windowID);
		break;
	case SDL_WINDOWEVENT_HIT_TEST:
		SDL_Log("Window %d has a special hit test", event.windowID);
		break;
#endif
	default:
		SDL_Log("Window %d got unknown event %d",
			event.windowID, event.event);
		break;
	}
}
