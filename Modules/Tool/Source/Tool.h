#pragma once
#include "Engine/Input.h"
#include "Math/Vector2.h"

class SDLWindow;
class BGFXRenderer;

struct ToolCreationFlags
{
    bool isBorderless = false;
};


class Tool
{
public:
    Tool() = delete;
    Tool( ToolCreationFlags& inToolCreationFlags );

    void Start();

    virtual void OnStart() = 0;

    virtual void OnUpdate() = 0;

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