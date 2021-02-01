#pragma once
#include <bgfx/bgfx.h>
#include <Math/Vector2.h>

class ImGuiRenderer;

struct RendererCreationSettings
{
	void* WindowPtr = nullptr;
	Vector2 InitialSize = Vector2(1280.f, 720.f);
};

class BGFXRenderer
{
	const bgfx::ViewId kClearView = 0;

public:
	BGFXRenderer() = default;

	void Create(const RendererCreationSettings& settings);
	void Destroy();

	void BeginFrame();
	void Render();

	void WindowResized(const Vector2& newSize);

private:
	Vector2 PreviousSize;
	Vector2 CurrentSize;

	ImGuiRenderer* ImGuiRender = nullptr;
};