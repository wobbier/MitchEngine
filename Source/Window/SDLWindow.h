#pragma once
#include "IWindow.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include "bgfx/bgfx.h"
#include <functional>
#include <optional>

class SDLWindow
	: public IWindow
{
public:
	SDLWindow(const std::string& title, std::function<void(const Vector2&)> resizeFunc, int X, int Y, Vector2 windowSize);
	bool ShouldClose() override;

	void ParseMessageQueue() override;

	void Swap() override;

	Vector2 GetSize() const override;

	Vector2 GetPosition() override;

	bool IsFullscreen() override;

	void Maximize() override;

	void Minimize() override;

	void ExitMaximize() override;

	void SetTitle(const std::string& title) override;

	void Exit() override;

	void* GetWindowPtr() override;

	void CanMoveWindow(bool param1);

	void SetBorderless(bool borderless);

	void SetCustomDragCallback(std::function<std::optional<SDL_HitTestResult>(const Vector2&)> cb);
	std::function<std::optional<SDL_HitTestResult>(const Vector2&)> CustomDragCB;
private:
	SDL_Window* WindowHandle = nullptr;

	bgfx::PlatformData PlatformInfo;
	std::function<void(const Vector2&)> ResizeCB;
	void SetWindow(SDL_Window* window);

	bool CloseRequested = false;

	//SDL_HitTestResult HitTestCallback(SDL_Window* window, const SDL_Point* area, void* data);
};