#include "PCH.h"
#include "UIWindow.h"
#include "Window/Win32Window.h"

UIWindow::UIWindow(IWindow* window, ultralight::OverlayManager* manager)
	: m_overlayManager(manager)
	, m_window(window)
{

}

void UIWindow::set_listener(ultralight::WindowListener* listener)
{
	throw std::logic_error("The method or operation is not implemented.");
}

ultralight::WindowListener* UIWindow::listener()
{
	throw std::logic_error("The method or operation is not implemented.");
}

uint32_t UIWindow::width() const
{
	return m_window->GetSize().X();
}

uint32_t UIWindow::height() const
{
	return m_window->GetSize().Y();
}

bool UIWindow::is_fullscreen() const
{
	return m_window->IsFullscreen();
}

double UIWindow::scale() const
{
	return 1.0;
}

void UIWindow::SetTitle(const char* title)
{
	m_window->SetTitle(title);
}

void UIWindow::SetCursor(ultralight::Cursor cursor)
{
	throw std::logic_error("The method or operation is not implemented.");
}

void UIWindow::Close()
{
	throw std::logic_error("The method or operation is not implemented.");
}

int UIWindow::DeviceToPixels(int val) const
{
	return 1;
}

HWND UIWindow::hwnd()
{
	return NULL;
	//return static_cast<Win32Window*>(m_window)->Window;
}

int UIWindow::PixelsToDevice(int val) const
{
	throw std::logic_error("The method or operation is not implemented.");
}

ultralight::OverlayManager* UIWindow::overlay_manager() const
{
	return m_overlayManager;
}
