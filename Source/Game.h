// 2023 Mitchell Andrews
#pragma once
#include "Dementia.h"
#include "Core/UpdateContext.h"
#include "Core/CommandLine.h"

class Game
{
public:
    Game() = delete;
    Game( int argc, char** argv ) {};
    virtual void OnInitialize() = 0;

    virtual void OnStart() = 0;
    virtual void OnUpdate( const UpdateContext& inUpdateContext ) = 0;
    // Called at the fixed simulation rate (see Engine::SetFixedTimeStep), possibly several times a frame.
    virtual void OnFixedUpdate( const UpdateContext& inUpdateContext ) {}
    virtual void OnEnd() = 0;
    // The window was asked to close. Return false to keep running (the game is then responsible
    // for quitting later through Engine::Quit, which asks again).
    virtual bool OnQuitRequested() { return true; }
    virtual void PreRender() = 0;
    virtual void PostRender() = 0;
    ME_HARDSTUCK( Game )
};

#if USING( ME_PLATFORM_UWP )
#include "SDL.h"
#include "SDL_video.h"
#include "SDL_main.h"
#include <wrl.h>
#define ME_APPLICATION_MAIN(className)                                      \
    int _main(int argc, char** argv) {                                      \
        CommandLine::Set(argc, argv);                                       \
        className app(argc, argv);                                          \
		GetEngine().Init(&app);                                             \
		GetEngine().Run();                                                  \
		return 0;                                                           \
    }                                                                       \
    __pragma(warning(push))                                                 \
    __pragma(warning(disable: 4447))                                        \
    int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {                \
        if(FAILED(Windows::Foundation::Initialize(RO_INIT_MULTITHREADED)))  \
            return 1;                                                       \
        return SDL_WinRTRunApp(_main, nullptr);                             \
    }                                                                       \
    __pragma(warning(pop))
#else
#define ME_APPLICATION_MAIN(className)                                      \
    int main(int argc, char** argv) {                                       \
        CommandLine::Set(argc, argv);                                       \
        className app(argc, argv);                                          \
		GetEngine().Init(&app);                                             \
		GetEngine().Run();                                                  \
		return 0;                                                           \
	}
#endif
