// 2018 Mitchell Andrews
#pragma once
#include "Dementia.h"
#include "Core/UpdateContext.h"

class Game
{
public:
	Game() = delete;
	Game(int argc, char** argv) {};
	virtual void OnInitialize() = 0;

	virtual void OnStart() = 0;
	virtual void OnUpdate(const UpdateContext& inUpdateContext) = 0;
	virtual void OnEnd() = 0;
	virtual void PostRender() = 0;
	ME_NONCOPYABLE(Game)
	ME_NONMOVABLE(Game)
};

#ifndef __cplusplus_winrt
#define ME_APPLICATION_MAIN(className)                                      \
    int main(int argc, char** argv) {                                       \
        className app(argc, argv);                                          \
		GetEngine().Init(&app);                                             \
		GetEngine().Run();                                                  \
		return 0;                                                           \
	}
#else
#include "SDL.h"
#include "SDL_video.h"
#include "SDL_main.h"
#include <wrl.h>
#define ME_APPLICATION_MAIN(className)                                      \
    int _main(int argc, char** argv) {                                      \
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
#endif
