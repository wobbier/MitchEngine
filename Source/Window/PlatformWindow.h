#pragma once
#include <string>
#include <Math/Vector2.h>
#include <bgfx/bgfx.h>

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
{
	PlatformWindow() = delete;
	PlatformWindow(PlatformWindowParams& InParams);

	void Create();
	void Show();
	void CreateAndShow();
	void Destroy();

	void Render();

	void Reset(bool soft = false);

	void* GetHWND();

	void SetSize(const Vector2& InSize);
	Vector2 GetSize();
	void SetPosition(const Vector2& InPosition);
	Vector2 GetPosition();

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

