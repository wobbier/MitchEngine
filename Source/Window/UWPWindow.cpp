#include "PCH.h"
#include "Window/UWPWindow.h"
#include "Engine/Input.h"
#include "CLog.h"
#include <assert.h>

#ifdef ME_PLATFORM_UWP2

#include <agile.h>

#include "Renderer.h"
#include "Device/DX11Device.h"
#include <tchar.h>
#include "Engine/Engine.h"
#include "BGFXRenderer.h"
#include <SDL.h>
#include <SDL_config_winrt.h>
#include <SDL_video.h>

using namespace Windows::UI::Core;
//using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
//using namespace Windows::Graphics::Display;
SDL_Renderer* renderer = NULL;
UWPWindow::UWPWindow( std::string title, int width, int height, std::function<void( const Vector2& )> resizeCallback )
    : ResizeCB( resizeCallback )
{
    MessageHandler = ref new UWPWindowMessageHandler( this );
    Size = Vector2( width, height );

    if( SDL_Init( SDL_INIT_EVERYTHING ) != 0 ) {
        return;
    }
    SDL_Window* WindowHandle;
    //SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_FULLSCREEN, &WindowHandle);
    WindowHandle = SDL_CreateWindow( title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE );
    //SDL_Window* WindowHandle = SDL_CreateWindowFrom(GetWindowPtr());
    //if (WindowHandle)
    //{

    //}
    //SDL_Window* window = NULL; 
    //if (SDL_CreateWindowAndRenderer(width, height, SDL_WINDOW_FULLSCREEN, &window, &renderer) != 0) {
    //	return;
    //}
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
    SDL_Event event;
    while( SDL_PollEvent( &event ) )
    {
        switch( event.type )
        {
        case SDL_QUIT:
        {
            ExitRequested = true;
            break;
        }

        case SDL_MOUSEWHEEL:
        {
            MouseScrollEvent evt( static_cast<float>( event.wheel.x ), static_cast<float>( event.wheel.y ) );
            evt.Fire();
            break;
        }

        case SDL_WINDOWEVENT:
        {
            const SDL_WindowEvent& wev = event.window;
            switch( wev.event ) {
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                OnWindowSizeChanged( Vector2( wev.data1, wev.data2 ) );
                break;

            case SDL_WINDOWEVENT_CLOSE:
                ExitRequested = true;
                break;
            }
            break;
        }
        default:
            break;
        }
    }
}

Vector2 UWPWindow::GetSize() const
{
    return Size;
}

void UWPWindow::SetTitle( const std::string& title )
{

}

void UWPWindow::SetVisibility( bool Visible )
{
    IsVisible = Visible;
}

Vector2 UWPWindow::GetPosition()
{
    return Vector2( 0.f, 0.f );
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

void* UWPWindow::GetWindowPtr()
{
    return reinterpret_cast<IUnknown*>( CoreWindow::GetForCurrentThread() );
}

bool UWPWindow::IsFullscreen()
{

    return false;
}

bool UWPWindow::IsMaximized()
{
    return false;
}

/************************************************************************/
/*                      UWPWindowMessageHandler                         */
/************************************************************************/

UWPWindow::UWPWindowMessageHandler::UWPWindowMessageHandler( UWPWindow* window )
{
    m_window = window;
    CoreWindow^ coreWindow = CoreWindow::GetForCurrentThread();

    coreWindow->SizeChanged +=
        ref new TypedEventHandler<CoreWindow^, WindowSizeChangedEventArgs^>( this, &UWPWindowMessageHandler::OnWindowSizeChanged );

    coreWindow->VisibilityChanged +=
        ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>( this, &UWPWindowMessageHandler::OnVisibilityChanged );

    coreWindow->Closed +=
        ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>( this, &UWPWindowMessageHandler::OnWindowClosed );

    //coreWindow->KeyDown +=
    //	ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow ^, Windows::UI::Core::KeyEventArgs ^>(this, &UWPWindowMessageHandler::OnKeyDown);

    Windows::Graphics::Display::DisplayInformation^ currentDisplayInformation = Windows::Graphics::Display::DisplayInformation::GetForCurrentView();

    Windows::Graphics::Display::DisplayInformation::DisplayContentsInvalidated +=
        ref new TypedEventHandler<Windows::Graphics::Display::DisplayInformation^, Object^>( this, &UWPWindowMessageHandler::OnDisplayContentsInvalidated );

    m_window->Size = Vector2( coreWindow->Bounds.Width, coreWindow->Bounds.Height );
}

void UWPWindow::UWPWindowMessageHandler::OnWindowSizeChanged( CoreWindow^ sender, WindowSizeChangedEventArgs^ args )
{
    //m_window->OnWindowSizeChanged(Vector2(sender->Bounds.Width, sender->Bounds.Height));
}

void UWPWindow::OnWindowSizeChanged( Vector2 newSize )
{
    Size = newSize;
    ResizeCB( Size );
}

void UWPWindow::UWPWindowMessageHandler::OnVisibilityChanged( CoreWindow^ sender, VisibilityChangedEventArgs^ args )
{
    m_window->SetVisibility( args->Visible );
}

void UWPWindow::UWPWindowMessageHandler::OnWindowClosed( CoreWindow^ sender, CoreWindowEventArgs^ args )
{
    m_window->ExitRequested = true;
}

void UWPWindow::UWPWindowMessageHandler::OnKeyDown( Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args )
{
    //Input::KeyCallback(args->VirtualKey);
}

// DisplayInformation event handlers.

void UWPWindow::UWPWindowMessageHandler::OnDisplayContentsInvalidated( Windows::Graphics::Display::DisplayInformation^ sender, Object^ args )
{
    //static_cast<Moonlight::DX11Device&>(GetEngine().GetRenderer().GetDevice()).ValidateDevice();
}

#endif