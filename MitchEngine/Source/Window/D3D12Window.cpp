#include "PCH.h"
#include "Window/D3D12Window.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <assert.h>

#if ME_PLATFORM_WIN64

#include "Renderer.h"
#include "Device/D3D12Device.h"
#include <tchar.h>
#include <corecrt_wstdio.h>

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

	RECT wr = { 0, 0, width, height };
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

	Window = CreateWindowEx(
		0,                              // Optional D3D12Window styles.
		CLASS_NAME,                     // D3D12Window class
		windowTitle.c_str(),    // D3D12Window text
		WS_OVERLAPPEDWINDOW,            // D3D12Window style
		// Size and position
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,       // Parent D3D12Window    
		NULL,       // Menu
		GetModuleHandle(0),  // Instance handle
		NULL        // Additional application data
	);

	if (Window == NULL)
	{
		return;
	}

	ShowWindow(Window, SW_NORMAL);
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
		if (msg.message == WM_KEYDOWN)
		{
			//Logger::GetInstance().Log(Logger::LogType::Debug, "Yoooo");
		}
	}
}

void D3D12Window::Swap()
{
}

glm::vec2 D3D12Window::GetSize() const
{
	RECT newSize;

	GetWindowRect(Window, &newSize);

	return glm::vec2(newSize.right - newSize.left, newSize.bottom - newSize.top);
}

LRESULT CALLBACK WinProc(HWND D3D12Window, unsigned int msg, WPARAM wp, LPARAM lp)
{
	wchar_t msg2[32];
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
			Game::GetEngine().GetRenderer().WindowResized(glm::vec2(newSize.right - newSize.left, newSize.bottom - newSize.top));
		}
		break;
	case WM_SYSKEYDOWN:
		swprintf_s(msg2, L"WM_SYSKEYDOWN: 0x%x\n", wp);
		OutputDebugString(msg2);
		break;

	case WM_SYSCHAR:
		swprintf_s(msg2, L"WM_SYSCHAR: %c\n", (wchar_t)wp);
		OutputDebugString(msg2);
		break;

	case WM_SYSKEYUP:
		swprintf_s(msg2, L"WM_SYSKEYUP: 0x%x\n", wp);
		OutputDebugString(msg2);
		break;

	case WM_KEYDOWN:
		swprintf_s(msg2, L"WM_KEYDOWN: 0x%x\n", wp);
		OutputDebugString(msg2);
		break;

	case WM_KEYUP:
		swprintf_s(msg2, L"WM_KEYUP: 0x%x\n", wp);
		OutputDebugString(msg2);
		break;

	case WM_CHAR:
		swprintf_s(msg2, L"WM_CHAR: %c\n", (wchar_t)wp);
		OutputDebugString(msg2);
		break;
		//
	}
	return DefWindowProc(D3D12Window, msg, wp, lp);
}

#endif