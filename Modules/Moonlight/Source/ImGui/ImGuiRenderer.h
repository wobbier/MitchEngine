#pragma once
#include <bgfx/bgfx.h>

struct ImGuiContext;
struct ImDrawData;

class ImGuiRenderer
{
public:
	ImGuiRenderer() = default;

	void Create();

	void NewFrame();
	void EndFrame();

private:
	int ViewId = 255;
	int LastScroll = 0;
	//int64_t Last = 0;
	ImGuiContext* Context = nullptr;

	bgfx::ProgramHandle Program;
	bgfx::VertexLayout Layout;
	bgfx::UniformHandle sTexture;
	bgfx::TextureHandle mTexture;

	void Render(ImDrawData* drawData);
};