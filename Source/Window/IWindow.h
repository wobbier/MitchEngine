#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <Math/Vector2.h>

// How the game window covers its display.
enum class WindowMode : uint8_t
{
    Windowed,
    Borderless,   // fullscreen at the desktop's resolution (fast alt-tab; the usual choice)
    Fullscreen,   // exclusive fullscreen at a chosen resolution
};

inline const char* ToString( WindowMode InMode )
{
    switch( InMode )
    {
    case WindowMode::Borderless: return "Borderless";
    case WindowMode::Fullscreen: return "Fullscreen";
    default: return "Windowed";
    }
}

// "Windowed", "Borderless" or "Fullscreen", any case; false (and OutMode untouched) otherwise.
inline bool ParseWindowMode( std::string InText, WindowMode& OutMode )
{
    for( char& c : InText )
    {
        c = static_cast<char>( c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c );
    }
    if( InText == "windowed" ) { OutMode = WindowMode::Windowed; return true; }
    if( InText == "borderless" ) { OutMode = WindowMode::Borderless; return true; }
    if( InText == "fullscreen" ) { OutMode = WindowMode::Fullscreen; return true; }
    return false;
}

class IWindow
{
public:
    virtual bool ShouldClose() = 0;
    virtual void ParseMessageQueue() = 0;
    virtual Vector2 GetSize() const = 0;
    virtual Vector2 GetPosition() = 0;
    virtual Vector2 GetClientPosition() = 0;
    virtual Vector2 GetClientSize() = 0;

    virtual bool IsFullscreen() = 0;
    virtual bool IsMaximized() = 0;

    virtual void Maximize() = 0;
    virtual void Minimize() = 0;
    virtual void ExitMaximize() = 0;
    virtual void SetTitle( const std::string& title ) = 0;
    virtual void Exit() = 0;
    // Withdraws a pending close request (the game vetoed quitting, e.g. to ask about unsaved work).
    virtual void CancelClose() {}
    // Starts the platform services that would delay the first frame (game controllers); the engine
    // calls it once, after that frame.
    virtual void InitDeferredSubsystems() {}
    virtual void* GetWindowPtr() = 0;

    virtual void SetBorderless( bool isBorderless ) = 0;

    // Window modes (player settings). Windowed: InResolution is the window's size (zero keeps it).
    // Fullscreen: the display mode closest to InResolution (zero = the desktop's). Borderless ignores it.
    virtual WindowMode GetWindowMode() const { return WindowMode::Windowed; }
    virtual void SetWindowMode( WindowMode InMode, const Vector2& InResolution = Vector2() ) {}
    // Displays: how many, which one the window is on, and moving it there (centered, same mode).
    virtual int GetDisplayCount() const { return 1; }
    virtual int GetDisplay() const { return 0; }
    virtual void MoveToDisplay( int InDisplay ) {}
    // The fullscreen resolutions a display offers, largest first.
    virtual std::vector<Vector2> GetResolutions( int InDisplay ) const { return {}; }
};
