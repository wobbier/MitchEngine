#include "PCH.h"
#include "Window/EditorWindow.h"
#include "Engine/Input.h"
#include "CLog.h"
#include <assert.h>

#if ME_PLATFORM_WIN64 && ME_EDITOR

#include "Renderer.h"
#include "Device/DX11Device.h"
#include <tchar.h>
#include <dwmapi.h>
#include "Utils/StringUtils.h"
#include "Engine/Input.h"
#include "Engine/Engine.h"
#include "SDL.h"

LRESULT CALLBACK WinProc(HWND Window, unsigned int msg, WPARAM wp, LPARAM lp);

EditorWindow::EditorWindow(std::string title, std::function<void(const Vector2&)> resizeFunc, int X, int Y, Vector2 windowSize)
	: ResizeFunc(resizeFunc)
	, WindowSize(windowSize)
{
	const wchar_t CLASS_NAME[] = L"EditorWindow";

	std::wstring windowTitle = StringUtils::ToWString(title);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.hInstance = nullptr;
	wc.lpfnWndProc = WinProc;
	wc.lpszClassName = CLASS_NAME;
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);

	wc.hIcon = (HICON)LoadImage(
		NULL,
		StringUtils::ToWString(Path("Assets/Havana/Icon.ico").FullPath).c_str(),
		IMAGE_ICON,
		0,
		0,
		LR_LOADFROMFILE |
		LR_DEFAULTSIZE
	);

	const ATOM result = ::RegisterClassExW(&wc);
	if (!result)
	{
		YIKES("Failed to register window class");
	}

	int w = static_cast<int>(windowSize.x);
	int h = static_cast<int>(windowSize.y);

	DWORD style = (DWORD)Style::ME_BASIC_BORDERLESS;

	Window = CreateWindowEx(
		0,                              // Optional D3D12Window styles.
		CLASS_NAME,                     // D3D12Window class
		windowTitle.c_str(),    // D3D12Window text
		style,            // D3D12Window style
		// Size and position
		X, Y, w, h,
		nullptr,       // Parent D3D12Window    
		nullptr,       // Menu
		nullptr,  // Instance handle
		this        // Additional application data
	);

	if (Window == NULL)
	{
		return;
	}

	borderless = true;
	borderless_shadow = true;

	SetBorderless(borderless);
	SetBorderlessShadow(borderless_shadow);
	ShowWindow(Window, SW_SHOW);

	SDL_Init(SDL_INIT_EVERYTHING);
	WindowHandle = SDL_CreateWindowFrom(Window);
}

EditorWindow::~EditorWindow()
{
}

bool EditorWindow::ShouldClose()
{
	return ExitRequested;
}

void EditorWindow::ParseMessageQueue()
{
	MSG msg = { 0 };
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);

		DispatchMessage(&msg);

		if (msg.message == WM_QUIT)
		{
			ExitRequested = true;
		}
		if (msg.message == WM_EXITSIZEMOVE)
		{
			Resized(Vector2(300,300));
		}
	}
}

void EditorWindow::Swap()
{
}

void EditorWindow::SetTitle(const std::string& title)
{
	SetWindowText(Window, StringUtils::ToWString(title).c_str());
}

Vector2 EditorWindow::GetSize() const
{
	RECT newSize;
	GetClientRect(Window, &newSize);

	return Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top));
}

Vector2 EditorWindow::GetPosition()
{
	RECT newSize;
	GetWindowRect(Window, &newSize);

	return Vector2(static_cast<float>(newSize.left), static_cast<float>(newSize.top));
}

void EditorWindow::CanMoveWindow(bool CanMove)
{
	canMoveWindow = CanMove;
}

void EditorWindow::SetBorderless(bool enabled)
{
	Style new_style = (enabled) ? SelectBorderlessStyle() : Style::ME_WINDOWED;
	Style old_style = static_cast<Style>(::GetWindowLongPtrW(Window, GWL_STYLE));

	if (new_style != old_style)
	{
		borderless = enabled;

		::SetWindowLongPtrW(Window, GWL_STYLE, static_cast<LONG>(new_style));

		SetShadow(Window, borderless_shadow && (new_style != Style::ME_WINDOWED));

		::SetWindowPos(Window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
		::ShowWindow(Window, SW_SHOW);
	}
}

auto EditorWindow::IsWindowCompositionEnabled() -> bool
{
	BOOL composition_enabled = FALSE;
	bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
	return composition_enabled && success;
}

EditorWindow::Style EditorWindow::SelectBorderlessStyle()
{
	return IsWindowCompositionEnabled() ? Style::ME_AERO_BORDERLESS : Style::ME_BASIC_BORDERLESS;
}

void EditorWindow::SetBorderlessShadow(bool enabled)
{
	if (borderless)
	{
		borderless_shadow = enabled;
		SetShadow(Window, enabled);
	}
}

auto EditorWindow::SetShadow(HWND handle, bool enabled) -> void
{
	if (IsWindowCompositionEnabled())
	{
		static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
		::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
	}
}

auto EditorWindow::AdjustMaximizedClientRect(HWND window, RECT& rect) -> void
{
	if (!IsMaximized(window))
	{
		return;
	}

	auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
	if (!monitor)
	{
		return;
	}

	MONITORINFO monitor_info{};
	monitor_info.cbSize = sizeof(monitor_info);
	if (!::GetMonitorInfoW(monitor, &monitor_info))
	{
		return;
	}
	rect = monitor_info.rcWork;
	Resized(Vector2(static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top)));
}

void EditorWindow::Minimize()
{
	ShowWindow(Window, SW_MINIMIZE);
}

void EditorWindow::ExitMaximize()
{
	ShowWindow(Window, SW_SHOWNORMAL);
}

bool EditorWindow::IsMaximized(HWND hwnd)
{
	WINDOWPLACEMENT placement;
	if (!::GetWindowPlacement(hwnd, &placement))
	{
		return false;
	}

	return placement.showCmd == SW_MAXIMIZE;
}

bool EditorWindow::IsMaximized()
{
	return IsMaximized(Window);
}

void EditorWindow::Maximize()
{
	ShowWindow(Window, SW_MAXIMIZE);

	RECT newSize;
	GetClientRect(Window, &newSize);
	Resized(Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top)));
}

LRESULT EditorWindow::HitTest(POINT cursor) const
{
	const POINT border{
		::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
		::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
	};
	RECT window;
	if (!::GetWindowRect(Window, &window))
	{
		return HTNOWHERE;
	}

	const auto drag = borderless_drag ? HTCAPTION : HTCLIENT;

	enum region_mask
	{
		client = 0b0000,
		left = 0b0001,
		right = 0b0010,
		top = 0b0100,
		bottom = 0b1000,
	};

	const auto result =
		left * (cursor.x < (window.left + border.x)) |
		right * (cursor.x >= (window.right - border.x)) |
		top * (cursor.y < (window.top + border.y)) |
		bottom * (cursor.y >= (window.bottom - border.y));

	switch (result)
	{
	case left: return borderless_resize ? HTLEFT : drag;
	case right: return borderless_resize ? HTRIGHT : drag;
	case top: return borderless_resize ? HTTOP : drag;
	case bottom: return borderless_resize ? HTBOTTOM : drag;
	case top | left: return borderless_resize ? HTTOPLEFT : drag;
	case top | right: return borderless_resize ? HTTOPRIGHT : drag;
	case bottom | left: return borderless_resize ? HTBOTTOMLEFT : drag;
	case bottom | right: return borderless_resize ? HTBOTTOMRIGHT : drag;
	case client:
	{
		POINT position;
		if (GetCursorPos(&position))
		{
			ScreenToClient(Window, &position);

			if (position.x > TitleBarDragPosition.x && position.x < TitleBarDragPosition.x + TitleBarDragSize.x && position.y < TitleBarDragPosition.y + TitleBarDragSize.y)
			{
				return drag;
			}
		}
		return HTNOWHERE;
	}
	default: return HTNOWHERE;
	}
}

void EditorWindow::Exit()
{
	ExitRequested = true;
}

bool EditorWindow::IsFullscreen()
{
	return IsMaximized();
}

void EditorWindow::Resized(const Vector2& NewSize)
{
	if (NewSize != WindowSize)
	{
		ResizeFunc(NewSize);
	}
	WindowSize = NewSize;
}

void* EditorWindow::GetWindowPtr()
{
	return Window;
}

void EditorWindow::SetDragBounds(const Vector2& DragPosition, const Vector2& Size)
{
	TitleBarDragPosition = DragPosition;
	TitleBarDragSize = Size;
}

POINT prevPos;
bool dragWindow = false;
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WinProc(HWND hwnd, unsigned int msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_NCCREATE)
	{
		auto userdata = reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams;
		::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userdata));
	}

	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
	{
		return true;
	}

	if (auto window_ptr = reinterpret_cast<EditorWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
	{
		auto& window = *window_ptr;

		switch (msg)
		{
		case WM_NCCALCSIZE:
		{
			if (wp == TRUE && window.borderless)
			{
				auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
				window.AdjustMaximizedClientRect(hwnd, params.rgrc[0]);
				return 0;
			}
			break;
		}
		case WM_NCHITTEST:
		{
			if (window.borderless)
			{
				LRESULT h = window.HitTest(POINT{
					(int)(short)LOWORD(lp),
					(int)(short)HIWORD(lp)
					});
				if (h != HTNOWHERE)
				{
					return h;
				}
			}
			break;
		}
		case WM_NCACTIVATE:
		{
			if (!window.IsWindowCompositionEnabled())
			{
				return 1;
			}
			break;
		}

		case WM_CLOSE:
		{
			::DestroyWindow(hwnd);
			return 0;
		}

		case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}
		case WM_ACTIVATEAPP:
			//DirectX::Keyboard::ProcessMessage(msg, wp, lp);
			//DirectX::Mouse::ProcessMessage(msg, wp, lp);
			break;
		case WM_INPUT:
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_MOUSEHOVER:
			//DirectX::Mouse::ProcessMessage(msg, wp, lp);
			break;
		case WM_MOUSEWHEEL:
		{
			MouseScrollEvent evt;
			evt.Scroll = Vector2(0.f, GET_WHEEL_DELTA_WPARAM(wp) / 120.f);
			evt.Fire();
			break;
		}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			//DirectX::Keyboard::ProcessMessage(msg, wp, lp);
			switch (wp)
			{
			case VK_F8: { window.borderless_drag = !window.borderless_drag;        return 0; }
			case VK_F9: { window.borderless_resize = !window.borderless_resize;    return 0; }
			case VK_F10: { window.SetBorderless(!window.borderless);               return 0; }
			case VK_F11: { window.SetBorderlessShadow(!window.borderless_shadow); return 0; }
			}
			break;
		}
		case WM_KEYUP:
		case WM_SYSKEYUP:
			break;
		case WM_SYSCOMMAND:
		{
			if (wp == SC_MAXIMIZE || wp == SC_RESTORE)
			{
				RECT newSize;
				GetClientRect(hwnd, &newSize);
				window.Resized(Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top)));
			}
		}
			break;
		case WM_DWMWINDOWMAXIMIZEDCHANGE:
		case WM_MDIMAXIMIZE:
		case WM_EXITSIZEMOVE:
			{
				RECT newSize;
				GetClientRect(hwnd, &newSize);
				window.Resized(Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top)));
			}
			break;
		}
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

#endif