#include "PCH.h"
#include "Window/D3D12Window.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <assert.h>

#if ME_PLATFORM_WIN64

#include "Renderer.h"
#include "Device/D3D12Device.h"
#include <tchar.h>

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

	WNDCLASS wc = { };

	wc.lpfnWndProc = WinProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);
	//int w = GetSystemMetrics(SM_CXSCREEN);
	//int h = GetSystemMetrics(SM_CYSCREEN);
	int w = width;
	int h = height;
	//HWND hmain = CreateWindow(L"className", L"title", WS_POPUP, 0, 0, w, h, 0, 0, hInst, 0);
	//ShowWindow(hmain, SW_SHOW);
	//RECT wr = { 0, 0, width, height };
	//AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
	DWORD style = WS_POPUP;

	Window = CreateWindowEx(
		0,                              // Optional D3D12Window styles.
		CLASS_NAME,                     // D3D12Window class
		windowTitle.c_str(),    // D3D12Window text
		WS_OVERLAPPEDWINDOW,            // D3D12Window style
		// Size and position
		CW_USEDEFAULT, CW_USEDEFAULT, w, h,
		NULL,       // Parent D3D12Window    
		NULL,       // Menu
		GetModuleHandle(0),  // Instance handle
		NULL        // Additional application data
	);

	if (Window == NULL)
	{
		return;
	}
	ShowWindow(Window, SW_SHOW);
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

Vector2 D3D12Window::GetSize() const
{
	RECT newSize;

	GetWindowRect(Window, &newSize);

	return Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top));
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WinProc(HWND D3D12Window, unsigned int msg, WPARAM wp, LPARAM lp)
{
	if (ImGui_ImplWin32_WndProcHandler(D3D12Window, msg, wp, lp))
		return true;
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_EXITSIZEMOVE:
		if (Game::GetEngine().IsInitialized())
		{
			RECT newSize;
			GetWindowRect(D3D12Window, &newSize);
			Game::GetEngine().GetRenderer().WindowResized(Vector2(static_cast<float>(newSize.right - newSize.left), static_cast<float>(newSize.bottom - newSize.top)));
		}
		//
	default:
		return DefWindowProc(D3D12Window, msg, wp, lp);
	}
}

#endif