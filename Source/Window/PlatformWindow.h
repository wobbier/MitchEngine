#pragma once
#include <string>
#include <Math/Vector2.h>
#include <bgfx/bgfx.h>
#include <Window/IWindow.h>

struct SDL_Window;
struct ImGuiViewport;
class ImGuiRenderer;

struct PlatformWindowParams
{
	std::string Name;
	Vector2 Position;
	Vector2 Size;
	uint32_t Flags = 0;
};

struct PlatformWindow
	: public IWindow
{
	PlatformWindow() = delete;
	PlatformWindow(PlatformWindowParams& InParams);

	virtual bool ShouldClose() final { return false; }
	virtual void ParseMessageQueue() final {}
	Vector2 GetPosition() final;

	virtual bool IsFullscreen() final { return false; }
	virtual bool IsMaximized() final { return false; }

	virtual void Maximize() final { }
	virtual void Minimize() final { }
	virtual void ExitMaximize() final { }
	virtual void SetTitle(const std::string& title) final { }
	virtual void Exit() final { }
	virtual void* GetWindowPtr() final { return GetHWND(); }

	virtual void SetBorderless(bool isBorderless) final {}

	void Create();
	void Show();
	void CreateAndShow();
	void Destroy();

	void Render();

	void Reset(bool soft = false);

	void* GetHWND();

	void SetSize(const Vector2& InSize);
	Vector2 GetSize() const final;
	void SetPosition(const Vector2& InPosition);

	uint16_t GetViewId() const {
		return ViewId;
	}

	PlatformWindowParams Params;
	SDL_Window* WindowPtr = nullptr;
	ImGuiViewport* Viewport = nullptr;
	ImGuiRenderer* Renderer = nullptr;
	bgfx::FrameBufferHandle Buffer;
	uint16_t ViewId = 254;
};

