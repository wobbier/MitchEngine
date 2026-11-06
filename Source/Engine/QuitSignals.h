#pragma once

// Ctrl+C and SIGTERM end the process through the engine's normal quit path (ME-308).
//
// SDL turns both into SDL_QUIT, but the .NET runtime installs its own SIGINT / SIGTERM handlers when
// it starts and, with nothing registered on the managed side, exits the process from its signal
// thread while the main loop is still running a frame: a shipped dedicated server stopped with
// Ctrl+C or `kill` crashed in FrameStats::EndFrame (or aborted in free()) as the static destructors
// ran under it. Install() puts the engine's handler in place of the runtime's (ScriptEngine::Init
// calls it once the runtime is up, Engine::Run again before the loop); the loop polls Requested()
// and quits through Engine::Quit, so the world, the net session (a Disconnect to every client)
// and the runtime shut down in order. Windows keeps SDL's handling (no-op here).
namespace QuitSignals
{
    void Install();
    // A quit signal has arrived since Install().
    bool Requested();
    // The signal's name ("SIGTERM") once one arrived, for the log; "" before.
    const char* Name();
}
