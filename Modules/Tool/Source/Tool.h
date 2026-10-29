#pragma once
#include "Engine/Input.h"
#include "Math/Vector2.h"
#include "imgui.h"
#include "Math/Vector4.h"
#include <string>

class SDLWindow;
class BGFXRenderer;

struct ToolCreationFlags
{
    bool isBorderless = false;
    bool isDockingEnabled = false;
    std::string toolName;
    Vector2 windowSize = { 1280.f, 720.f };
    // Where ImGui keeps the window layout; empty never saves one.
    std::string layoutFile;
};


// A small ImGui application on its own window and renderer, without the engine (no world, no
// project): Havana's Project Hub. The renderer is created without assets and only draws ImGui.
class Tool
{
public:
    Tool() = delete;
    Tool( ToolCreationFlags& inToolCreationFlags );
    virtual ~Tool();

    // Opens the window, calls OnStart, then OnUpdate every frame until the window is closed or
    // RequestClose is called, then OnEnd, and closes the window.
    void Start();

    virtual void OnStart() = 0;

    virtual void OnUpdate() = 0;

    virtual void OnEnd() {}

    // Ends the loop after the current frame.
    void RequestClose() { m_closeRequested = true; }

    // Saves the next frame's backbuffer as a PNG.
    void RequestScreenshot( const std::string& InPath );
    uint32_t GetScreenshotCount() const;

    // Top, Bottom, Left, Right
    Vector4 DockMargins = { 0.f, 0.f, 0.f, 0.f };

private:
    void Run();
    void Shutdown();

protected:
    ToolCreationFlags m_toolCreationFlags;
    Input m_input;
    SDLWindow* m_window = nullptr;
    BGFXRenderer* m_renderer = nullptr;
    Vector2 TitleBarDragPosition = { 0.f, 0.f };
    Vector2 TitleBarDragSize = { 0.f, 0.f };
    bool m_closeRequested = false;
};
