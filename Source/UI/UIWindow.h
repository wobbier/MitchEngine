#pragma once

#include <Ultralight/String16.h>
#include "UI/RefCountedImpl.h"
#include "AppCore/Window.h"
#include "Window/IWindow.h"
#include "Ultralight/platform/Surface.h"

class UIWindow
    : public ultralight::Window
    , public ultralight::RefCountedImpl<UIWindow>
{
    friend class OverlayImpl;
    friend class Window;
    friend class OverlayImpl;
public:
    UIWindow( IWindow* window, ultralight::OverlayManager* manager );
protected:

    virtual ~UIWindow() override;;
public:

    virtual void set_listener( ultralight::WindowListener* listener ) override;


    virtual ultralight::WindowListener* listener() override;


    virtual uint32_t width() const override;


    virtual uint32_t height() const override;


    virtual bool is_fullscreen() const override;


    virtual double scale() const override;


    virtual void SetTitle( const char* title ) override;


    virtual void SetCursor( ultralight::Cursor cursor ) override;


    virtual void Close() override;

    virtual int ScreenToPixels( int val ) const override;

    void set_app_listener( ultralight::WindowListener* listener ) {  }

    virtual int PixelsToScreen( int val ) const override;

    REF_COUNTED_IMPL( UIWindow );

    virtual ultralight::OverlayManager* overlay_manager() const override;

    void* native_handle() const override;


    void DrawSurface( int x, int y, ultralight::Surface* surface ) override;

protected:
    ultralight::OverlayManager* m_overlayManager = nullptr;
    IWindow* m_window;
    ultralight::WindowListener* listener_ = nullptr;
};
