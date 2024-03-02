#pragma once
#include "Engine/Input.h"
#include "Math/Vector2.h"
#include "imgui.h"
#include "Math/Vector4.h"

class SDLWindow;
class BGFXRenderer;

struct ToolCreationFlags
{
    bool isBorderless = false;
    bool isDockingEnabled = false;
    std::string toolName;
};


class Tool
{
public:
    Tool() = delete;
    Tool( ToolCreationFlags& inToolCreationFlags );

    void Start();

    virtual void OnStart() = 0;

    virtual void OnUpdate() = 0;

    // Top, Bottom, Left, Right
    Vector4 DockMargins = { 0.f, 0.f, 0.f, 0.f };

private:
    void Run();

protected:
    ToolCreationFlags m_toolCreationFlags;
    Input m_input;
    SDLWindow* m_window = nullptr;
    BGFXRenderer* m_renderer = nullptr;
    Vector2 TitleBarDragPosition = { 0.f, 0.f };
    Vector2 TitleBarDragSize = { 0.f, 0.f };
};