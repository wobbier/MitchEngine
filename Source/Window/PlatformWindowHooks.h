#pragma once

struct SDL_Window;
class ImGuiRenderer;

namespace ImGui
{
    void InitHooks( SDLWindow* window, ImGuiRenderer* renderer );
}