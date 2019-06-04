#include "PCH.h"
#include "Window/D3D12Window.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <assert.h>

#if ME_PLATFORM_WIN64

#include "Renderer.h"
#include "Device/D3D12Device.h"
#include <tchar.h>
#include <dwmapi.h>

LRESULT CALLBACK WinProc(HWND Window, unsigned int msg, WPARAM wp, LPARAM lp);

std::wstring s2ws(const std::string& s)
{
	int len;
	int slength = (int)s.length() + 1;
	len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
	wchar_t* buf = new wchar_t[len];
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
	std::wstring r(buf);
	delete[] buf;
	return r;
};

D3D12Window::D3D12Window(std::string title, int width, int height)
	: IWindow(title, width, height)
{
	WINDOW_HEIGHT = height;
	WINDOW_WIDTH = width;

	const wchar_t CLASS_NAME[] = L"D3D12Window";

	std::wstring windowTitle = s2ws(title);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.hInstance = nullptr;
	wc.lpfnWndProc = WinProc;
	wc.lpszClassName = CLASS_NAME;
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
	const ATOM result = ::RegisterClassExW(&wc);
	if (!result) {
		Logger::Log(Logger::LogType::Error, "failed to register window class");
	}

	int w = width;
	int h = height;

	DWORD style = (DWORD)Style::ME_BASIC_BORDERLESS;

	Window = CreateWindowEx(
		0,                              // Optional D3D12Window styles.
		CLASS_NAME,                     // D3D12Window class
		windowTitle.c_str(),    // D3D12Window text
		style,            // D3D12Window style
		// Size and position
		CW_USEDEFAULT, CW_USEDEFAULT, w, h,
		nullptr,       // Parent D3D12Window    
		nullptr,       // Menu
		nullptr,  // Instance handle
		this        // Additional application data
	);

	if (Window == NULL)
	{
		return;
	}
	SetBorderless(borderless);
	SetBorderlessShadow(borderless_shadow);
	ShowWindow(Window, SW_SHOWMAXIMIZED);
}

D3D12Window::~D3D12Window()
{
}

bool D3D12Window::ShouldClose()
{
	return ExitRequested;
}

void D3D12Window::ParseMessageQueue()
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
		}
	}
}

void D3D12Window::Swap()
{
}

void D3D12Window::SetTitle(const std::string& title)
{
	SetWindowText(Window, s2ws(title).c_str());
}

Vector2 D3D12Window::GetSize() const
{
	RECT newSize;
	GetClientRect(Window, &newSize);

	return Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top));
}

Vector2 D3D12Window::GetPosition()
{
	RECT newSize;
	GetWindowRect(Window, &newSize);

	return Vector2(static_cast<float>(newSize.left), static_cast<float>(newSize.top));
}

void D3D12Window::CanMoveWindow(bool CanMove)
{
	canMoveWindow = CanMove;
}

void D3D12Window::SetBorderless(bool enabled)
{
	Style new_style = (enabled) ? SelectBorderlessStyle() : Style::ME_WINDOWED;
	Style old_style = static_cast<Style>(::GetWindowLongPtrW(Window, GWL_STYLE));

	if (new_style != old_style) {
		borderless = enabled;

		::SetWindowLongPtrW(Window, GWL_STYLE, static_cast<LONG>(new_style));

		SetShadow(Window, borderless_shadow && (new_style != Style::ME_WINDOWED));

		::SetWindowPos(Window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
		::ShowWindow(Window, SW_SHOW);
	}
}

auto D3D12Window::IsWindowCompositionEnabled() -> bool
{
	BOOL composition_enabled = FALSE;
	bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
	return composition_enabled && success;
}

D3D12Window::Style D3D12Window::SelectBorderlessStyle()
{
	return IsWindowCompositionEnabled() ? Style::ME_AERO_BORDERLESS : Style::ME_BASIC_BORDERLESS;
}

void D3D12Window::SetBorderlessShadow(bool enabled)
{
	if (borderless) {
		borderless_shadow = enabled;
		SetShadow(Window, enabled);
	}
}

auto D3D12Window::SetShadow(HWND handle, bool enabled) -> void
{
	if (IsWindowCompositionEnabled()) {
		static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
		::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
	}
}

auto D3D12Window::AdjustMaximizedClientRect(HWND window, RECT& rect) -> void
{
	if (!IsMaximized(window)) {
		return;
	}

	auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
	if (!monitor) {
		return;
	}

	MONITORINFO monitor_info{};
	monitor_info.cbSize = sizeof(monitor_info);
	if (!::GetMonitorInfoW(monitor, &monitor_info)) {
		return;
	}

	rect = monitor_info.rcWork;
}

void D3D12Window::Minimize()
{
	ShowWindow(Window, SW_MINIMIZE);
}

void D3D12Window::ExitMaximize()
{
	ShowWindow(Window, SW_SHOWNORMAL);
}

bool D3D12Window::IsMaximized(HWND hwnd)
{
	WINDOWPLACEMENT placement;
	if (!::GetWindowPlacement(hwnd, &placement)) {
		return false;
	}

	return placement.showCmd == SW_MAXIMIZE;
}

bool D3D12Window::IsMaximized()
{
	return IsMaximized(Window);
}

void D3D12Window::Maximize()
{
	ShowWindow(Window, SW_MAXIMIZE);
}

LRESULT D3D12Window::HitTest(POINT cursor) const
{
	const POINT border{
		::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
		::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
	};
	RECT window;
	if (!::GetWindowRect(Window, &window)) {
		return HTNOWHERE;
	}

	const auto drag = borderless_drag ? HTCAPTION : HTCLIENT;

	enum region_mask {
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

	switch (result) {
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
			if ((position.y) <= 35.f && canMoveWindow)
			{
				return drag;
			}
		}
		return HTNOWHERE;
	}
	default: return HTNOWHERE;
	}
}

void D3D12Window::Exit()
{
	ExitRequested = true;
}

POINT prevPos;
bool dragWindow = false;
#if ME_EDITOR
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
LRESULT CALLBACK WinProc(HWND hwnd, unsigned int msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_NCCREATE) {
		auto userdata = reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams;
		::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userdata));
	}
#if ME_EDITOR
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
		return true;
#endif
	if (auto window_ptr = reinterpret_cast<D3D12Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
	{
		auto& window = *window_ptr;

		switch (msg)
		{
		case WM_NCCALCSIZE: {
			if (wp == TRUE && window.borderless) {
				auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
				window.AdjustMaximizedClientRect(hwnd, params.rgrc[0]);
				return 0;
			}
			break;
		}
		case WM_NCHITTEST: {
			if (window.borderless) {
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
		case WM_NCACTIVATE: {
			if (!window.IsWindowCompositionEnabled()) {
				return 1;
			}
			break;
		}

		case WM_CLOSE: {
			::DestroyWindow(hwnd);
			return 0;
		}

		case WM_DESTROY: {
			PostQuitMessage(0);
			return 0;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: {
			switch (wp) {
			case VK_F8: { window.borderless_drag = !window.borderless_drag;        return 0; }
			case VK_F9: { window.borderless_resize = !window.borderless_resize;    return 0; }
			case VK_F10: { window.SetBorderless(!window.borderless);               return 0; }
			case VK_F11: { window.SetBorderlessShadow(!window.borderless_shadow); return 0; }
			}
			break;
		}
		//case WM_SIZE:
		//case WM_EXITSIZEMOVE:
		//	dragWindow = false;
		//	prevPos.x = 0;
		//	prevPos.y = 0;
		//	ReleaseCapture();
		//	if (GetEngine().IsInitialized())
		//	{
		//		RECT newSize;
		//		GetClientRect(hwnd, &newSize);
		//		GetEngine().GetRenderer().WindowResized(Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top)));
		//	}
		//	break;
		}
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

#endif