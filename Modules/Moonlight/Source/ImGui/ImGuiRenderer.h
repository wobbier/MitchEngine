#pragma once
#include <bgfx/bgfx.h>
#include "Math/Vector2.h"

struct ImGuiContext;
struct ImDrawData;

class ImGuiRenderer
{
public:
	ImGuiRenderer() = default;

	void Create();

	void NewFrame(Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId);
	void EndFrame();

private:
	int ViewId = 255;
	int LastScroll = 0;
	//int64_t Last = 0;
	ImGuiContext* Context = nullptr;

	bgfx::ProgramHandle Program;
	bgfx::ProgramHandle ImageProgram;
	bgfx::VertexLayout Layout;
	bgfx::UniformHandle sTexture;
	bgfx::UniformHandle ImageLODEnabled;
	bgfx::TextureHandle mTexture;

	void Render(ImDrawData* drawData);
};