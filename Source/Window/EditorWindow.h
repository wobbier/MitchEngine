// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>

#include "IWindow.h"

#if ME_PLATFORM_WIN64 && ME_EDITOR

#include <SDL.h>

class EditorWindow final
	: public IWindow
{
	friend class UIWindow;
public:
	enum class Style : DWORD
	{
		ME_WINDOWED = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
		ME_AERO_BORDERLESS = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
		ME_BASIC_BORDERLESS = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
	};

	EditorWindow(std::string title, std::function<void(const Vector2&)> resizeFunc, int X, int Y, Vector2 windowSize);
	~EditorWindow();

	virtual bool ShouldClose() final;
	virtual void ParseMessageQueue() final;
	virtual void Swap() final;
	virtual void SetTitle(const std::string& title) final;
	void SetIcon(const std::string& stricon);
	Vector2 GetSize() const;
	virtual Vector2 GetPosition() final;

	void CanMoveWindow(bool CanMove);
	void SetBorderless(bool enabled);

	auto IsWindowCompositionEnabled() -> bool;

	Style SelectBorderlessStyle();

	void SetDragBounds(const Vector2& DragPosition, const Vector2& Size);

	void SetBorderlessShadow(bool enabled);
	auto SetShadow(HWND handle, bool enabled) -> void;
	auto AdjustMaximizedClientRect(HWND window, RECT& rect) -> void;
	bool borderless = false;
	bool borderless_shadow = false;
	bool borderless_drag = true;
	bool borderless_resize = true;

	virtual void Minimize() final;
	virtual void ExitMaximize() final;

	bool IsMaximized();

	virtual bool IsFullscreen() override;

	virtual void Maximize() final;

	LRESULT HitTest(POINT cursor) const;

	virtual void Exit() final;

	// ??
	void Resized(const Vector2& NewSize);

	void* GetWindowPtr() final;

private:
	bool ExitRequested = false;
	bool canMoveWindow = false;

	bool IsMaximized(HWND hwnd);

	HICON hWindowIcon = NULL;
	HICON hWindowIconSm = NULL;
	Vector2 WindowSize;
	std::function<void(const Vector2&)> ResizeFunc;
	HWND Window;

	SDL_Window* sdlWindow = nullptr;
	Vector2 TitleBarDragPosition;
	Vector2 TitleBarDragSize;
};

#endif