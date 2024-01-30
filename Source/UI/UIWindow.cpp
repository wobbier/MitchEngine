#include "PCH.h"
#include "UIWindow.h"

UIWindow::UIWindow( IWindow* window, ultralight::OverlayManager* manager )
    : m_overlayManager( manager )
    , m_window( window )
{

}

UIWindow::~UIWindow()
{

}

void UIWindow::set_listener( ultralight::WindowListener* listener )
{
}

void* UIWindow::native_handle() const
{
    return nullptr;
}

void UIWindow::DrawSurface( int x, int y, ultralight::Surface* surface )
{
    throw std::logic_error( "The method or operation is not implemented." );
}

ultralight::WindowListener* UIWindow::listener()
{
    throw std::logic_error( "The method or operation is not implemented." );
}

uint32_t UIWindow::width() const
{
    return static_cast<uint32_t>( m_window->GetSize().x );
}

uint32_t UIWindow::height() const
{
    return static_cast<uint32_t>( m_window->GetSize().y );
}

bool UIWindow::is_fullscreen() const
{
    return m_window->IsFullscreen();
}

double UIWindow::scale() const
{
    return 1.0;
}

void UIWindow::SetTitle( const char* title )
{
    m_window->SetTitle( title );
}

void UIWindow::SetCursor( ultralight::Cursor cursor )
{
    throw std::logic_error( "The method or operation is not implemented." );
}

void UIWindow::Close()
{
    throw std::logic_error( "The method or operation is not implemented." );
}

int UIWindow::ScreenToPixels( int val ) const
{
    return 1;
}

int UIWindow::PixelsToScreen( int val ) const
{
    throw std::logic_error( "The method or operation is not implemented." );
}

ultralight::OverlayManager* UIWindow::overlay_manager() const
{
    return m_overlayManager;
}
