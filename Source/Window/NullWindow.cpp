#include "PCH.h"
#include "Window/NullWindow.h"
#include <SDL.h>


NullWindow::NullWindow( const Vector2& InSize )
    : m_size( InSize )
{
    // Events only (no video): SDL turns SIGINT/SIGTERM into SDL_QUIT.
    m_sdlEvents = SDL_InitSubSystem( SDL_INIT_EVENTS ) == 0;
}


NullWindow::~NullWindow()
{
    if( m_sdlEvents )
    {
        SDL_QuitSubSystem( SDL_INIT_EVENTS );
    }
}


void NullWindow::ParseMessageQueue()
{
    if( !m_sdlEvents )
    {
        return;
    }
    SDL_Event event;
    while( SDL_PollEvent( &event ) )
    {
        if( event.type == SDL_QUIT )
        {
            m_closeRequested = true;
        }
    }
}
