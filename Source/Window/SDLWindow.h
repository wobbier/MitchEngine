#pragma once
#include "IWindow.h"

#include <SDL.h>
#include <bgfx/bgfx.h>
#include <functional>
#include <optional>

class SDLWindow : public IWindow
{
public:
    SDLWindow(const std::string& title,
              std::function<void(const Vector2&)> resizeFunc,
              int X, int Y,
              Vector2 windowSize);
    ~SDLWindow();

    bool ShouldClose() override;
    void ParseMessageQueue() override;
    void InitDeferredSubsystems() override;

    Vector2 GetSize() const override;
    Vector2 GetPosition() override;
    Vector2 GetClientPosition() override;

    bool IsFullscreen() override;
    void Maximize() override;
    void Minimize() override;
    void ExitMaximize() override;

    void SetTitle(const std::string& title) override;
    void SetIcon( const uint8_t* InRgba, int InWidth, int InHeight ) override;
    void Exit() override;

    void* GetWindowPtr() override;

    void CanMoveWindow(bool param1);
    void SetBorderless(bool isBorderless) final;

    WindowMode GetWindowMode() const override;
    void SetWindowMode( WindowMode InMode, const Vector2& InResolution = Vector2() ) override;
    int GetDisplayCount() const override;
    int GetDisplay() const override;
    void MoveToDisplay( int InDisplay ) override;
    std::vector<Vector2> GetResolutions( int InDisplay ) const override;
    bool IsMaximized() final;

    void SetCustomDragCallback(std::function<std::optional<SDL_HitTestResult>(const Vector2&)> cb);
    std::function<std::optional<SDL_HitTestResult>(const Vector2&)> CustomDragCB;

    SDL_Window* WindowHandle = nullptr;

    Vector2 GetClientSize() override;

#if USING( ME_PLATFORM_LINUX )
    void* GetGLContext() const { return GLContext; }
    void* GetDisplayPtr() const { return NativeDisplay; }
    bgfx::NativeWindowHandleType::Enum GetWindowType() const { return NativeWindowType; }
#endif

private:
    void* NativeWindow = nullptr;
    void* NativeDisplay = nullptr;
    bgfx::NativeWindowHandleType::Enum NativeWindowType = bgfx::NativeWindowHandleType::Default;
    std::function<void(const Vector2&)> ResizeCB;
    // SDL subsystems InitDeferredSubsystems starts (game controllers), none in automated runs.
    uint32_t m_deferredSubsystems = 0;

    void SetWindow(SDL_Window* window);
    void HandleWindowEvent(const SDL_WindowEvent& event);

    bool CloseRequested = false;
public:
    void CancelClose() override { CloseRequested = false; }
private:
    bool isMaximized = false;

#if USING( ME_PLATFORM_LINUX )
    SDL_GLContext GLContext = nullptr;
    struct wl_egl_window* m_waylandEglWindow = nullptr;
#endif
};
