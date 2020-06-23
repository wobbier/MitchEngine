#include "PCH.h"
#include "Window/UWPWindow.h"
#include "Engine/Input.h"
#include "CLog.h"
#include <assert.h>

#if ME_PLATFORM_UWP

#include "Renderer.h"
#include "Device/DX11Device.h"
#include <tchar.h>
#include "Engine/Engine.h"

using namespace Windows::UI::Core;
//using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
//using namespace Windows::Graphics::Display;

UWPWindow::UWPWindow(std::string title, int width, int height)
	: IWindow(title, width, height)
{
	MessageHandler = ref new UWPWindowMessageHandler(this);
}

UWPWindow::~UWPWindow()
{
}

bool UWPWindow::ShouldClose()
{
	return ExitRequested;
}

void UWPWindow::ParseMessageQueue()
{
	if (m_windowVisible)
	{
		CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
	}
	else
	{
		CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
	}
}

void UWPWindow::Swap()
{
}

Vector2 UWPWindow::GetSize() const
{
	return Vector2(WINDOW_WIDTH, WINDOW_HEIGHT);
}

void UWPWindow::SetTitle(const std::string& title)
{

}

void UWPWindow::SetVisibility(bool Visible)
{
	m_windowVisible = Visible;
}

Vector2 UWPWindow::GetPosition()
{
	return Vector2(0.f, 0.f);
}

void UWPWindow::Maximize()
{
}

void UWPWindow::Minimize()
{
}

void UWPWindow::ExitMaximize()
{
}

void UWPWindow::Exit()
{
}

bool UWPWindow::IsFullscreen()
{

	return false;
}

/************************************************************************/
/*                      UWPWindowMessageHandler                         */
/************************************************************************/

UWPWindow::UWPWindowMessageHandler::UWPWindowMessageHandler(UWPWindow* window)
{
	m_window = window;
	CoreWindow^ coreWindow = CoreWindow::GetForCurrentThread();

	coreWindow->SizeChanged +=
		ref new TypedEventHandler<CoreWindow^, WindowSizeChangedEventArgs^>(this, &UWPWindowMessageHandler::OnWindowSizeChanged);

	coreWindow->VisibilityChanged +=
		ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &UWPWindowMessageHandler::OnVisibilityChanged);

	coreWindow->Closed +=
		ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &UWPWindowMessageHandler::OnWindowClosed);

	coreWindow->KeyDown +=
		ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow ^, Windows::UI::Core::KeyEventArgs ^>(this, &UWPWindowMessageHandler::OnKeyDown);

	Windows::Graphics::Display::DisplayInformation^ currentDisplayInformation = Windows::Graphics::Display::DisplayInformation::GetForCurrentView();

	Windows::Graphics::Display::DisplayInformation::DisplayContentsInvalidated +=
		ref new TypedEventHandler<Windows::Graphics::Display::DisplayInformation^, Object^>(this, &UWPWindowMessageHandler::OnDisplayContentsInvalidated);

	IWindow::WINDOW_WIDTH = coreWindow->Bounds.Width;
	IWindow::WINDOW_HEIGHT = coreWindow->Bounds.Height;
}

void UWPWindow::UWPWindowMessageHandler::OnWindowSizeChanged(CoreWindow^ sender, WindowSizeChangedEventArgs^ args)
{
	m_window->WINDOW_WIDTH = sender->Bounds.Width;
	m_window->WINDOW_HEIGHT = sender->Bounds.Height;
	GetEngine().GetRenderer().WindowResized(Vector2(WINDOW_WIDTH, WINDOW_HEIGHT));
}

void UWPWindow::UWPWindowMessageHandler::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
{
	m_window->SetVisibility(args->Visible);
}

void UWPWindow::UWPWindowMessageHandler::OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args)
{
	m_window->ExitRequested = true;
}

void UWPWindow::UWPWindowMessageHandler::OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args)
{
	//Input::KeyCallback(args->VirtualKey);
}

// DisplayInformation event handlers.

void UWPWindow::UWPWindowMessageHandler::OnDisplayContentsInvalidated(Windows::Graphics::Display::DisplayInformation^ sender, Object^ args)
{
	static_cast<Moonlight::DX11Device&>(GetEngine().GetRenderer().GetDevice()).ValidateDevice();
}

#endif