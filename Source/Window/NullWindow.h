#pragma once
#include "Window/IWindow.h"

// The window of a headless run (dedicated servers, NET-5): nothing on screen, a fixed size for
// whatever asks (cameras, UI), and SDL's event loop only, so Ctrl+C / SIGTERM still close it.
class NullWindow final
    : public IWindow
{
public:
    explicit NullWindow( const Vector2& InSize );
    ~NullWindow();

    bool ShouldClose() override { return m_closeRequested; }
    void ParseMessageQueue() override;
    Vector2 GetSize() const override { return m_size; }
    Vector2 GetPosition() override { return Vector2(); }
    Vector2 GetClientPosition() override { return Vector2(); }
    Vector2 GetClientSize() override { return m_size; }

    bool IsFullscreen() override { return false; }
    bool IsMaximized() override { return false; }

    void Maximize() override {}
    void Minimize() override {}
    void ExitMaximize() override {}
    void SetTitle( const std::string& ) override {}
    void Exit() override { m_closeRequested = true; }
    void CancelClose() override { m_closeRequested = false; }
    void* GetWindowPtr() override { return nullptr; }

    void SetBorderless( bool ) override {}

private:
    Vector2 m_size;
    bool m_closeRequested = false;
    bool m_sdlEvents = false;
};
