#pragma once
#include <bgfx/bgfx.h>
#include <RenderViews.h>
#include "Math/Vector2.h"

struct ImGuiContext;
struct ImDrawData;

class ImGuiRenderer
{
public:
    ImGuiRenderer() = default;

    void Create();

    void NewFrame( const Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId );
    void EndFrame();

    void Render( ImDrawData* drawData, bgfx::ViewId viewId = Moonlight::RenderView::ImGuiMain );
private:
    ImGuiContext* Context = nullptr;

    bgfx::VertexLayout Layout;
    bgfx::ProgramHandle Program;
    bgfx::ProgramHandle ImageProgram;
    bgfx::UniformHandle sTexture;
    bgfx::UniformHandle ImageLODEnabled;
    bgfx::TextureHandle mTexture;
    bgfx::ViewId ViewId = Moonlight::RenderView::ImGuiMain;
};
