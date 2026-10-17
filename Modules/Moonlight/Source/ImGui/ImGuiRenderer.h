#pragma once
#include <bgfx/bgfx.h>
#include <RenderViews.h>
#include "Math/Vector2.h"
#include <string>

struct ImGuiContext;
struct ImDrawData;

class ImGuiRenderer
{
public:
    ImGuiRenderer() = default;

    void Create();
    void Destroy();

    void NewFrame( const Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId );
    void EndFrame();

    void Render( ImDrawData* drawData, bgfx::ViewId viewId = Moonlight::RenderView::ImGuiMain );

    // Rebuilds the font atlas before the next ImGui frame (the atlas is locked mid-frame).
    // An empty or unreadable path falls back to the embedded Roboto.
    void SetFont( const std::string& ttfPath, float sizePixels );

    static constexpr float kMinFontSize = 18.f;
private:
    void BuildFontAtlas();

    std::string FontPath;
    float FontSize = kMinFontSize;
    bool FontDirty = false;
    ImGuiContext* Context = nullptr;

    bgfx::VertexLayout Layout;
    bgfx::ProgramHandle Program;
    bgfx::ProgramHandle ImageProgram;
    bgfx::UniformHandle sTexture;
    bgfx::UniformHandle ImageLODEnabled;
    bgfx::TextureHandle mTexture = BGFX_INVALID_HANDLE;
    bgfx::ViewId ViewId = Moonlight::RenderView::ImGuiMain;
};
