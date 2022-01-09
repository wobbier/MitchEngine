#define SDL_MAIN_HANDLED
#include "Window/SDLWindow.h"
#include "BGFXRenderer.h"
#include <Camera/CameraData.h>
#include <Engine/Input.h>
#include "MitchHub.h"
#include <SDL_mouse.h>
#include <imgui.h>

#define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE    (SDL_VERSION_ATLEAST(2,0,4) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS))
#define SDL_HAS_MOUSE_FOCUS_CLICKTHROUGH    SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_WINDOW_ALPHA                SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_ALWAYS_ON_TOP               SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_USABLE_DISPLAY_BOUNDS       SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_PER_MONITOR_DPI             SDL_VERSION_ATLEAST(2,0,4)
#define SDL_HAS_VULKAN                      SDL_VERSION_ATLEAST(2,0,6)

extern bool ImGui_ImplSDL2_InitForMetal(SDL_Window* window);
extern bool ImGui_ImplSDL2_InitForD3D(SDL_Window* window);
int main(int argc, char** argv)
{
	BGFXRenderer* Renderer = new BGFXRenderer();

	std::function<void(const Vector2&)> ResizeFunc = [Renderer](const Vector2& NewSize)
	{
		if (Renderer)
		{
			Renderer->WindowResized(NewSize);
		}
	};

	SDLWindow* win = new SDLWindow("ME HUB", ResizeFunc, 500, 300, Vector2(1920, 1080));
	win->SetBorderless(true);
    ResizeFunc(Vector2(1920, 1080));
	RendererCreationSettings set;
	set.WindowPtr = win->GetWindowPtr();
	set.InitAssets = true;
	Renderer->Create(set);

#if ME_PLATFORM_WIN64
    ImGui_ImplSDL2_InitForD3D(static_cast<SDLWindow*>(win)->WindowHandle);
#endif
#if ME_PLATFORM_MACOS
    ImGui_ImplSDL2_InitForMetal(static_cast<SDLWindow*>(win)->WindowHandle);
#endif

	Input input;
	MitchHub hub(&input, win, Renderer->GetImGuiRenderer());

	while (!win->ShouldClose())
	{
		win->ParseMessageQueue();

		input.Update();
		ImGuiIO& io = ImGui::GetIO();

		Vector2 mousePos = input.GetMousePosition();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			// Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
			mousePos = input.GetGlobalMousePosition();
		}

		Renderer->BeginFrame(mousePos, (input.IsMouseButtonDown(MouseButton::Left) ? 0x01 : 0)
			| (input.IsMouseButtonDown(MouseButton::Right) ? 0x02 : 0)
			| (input.IsMouseButtonDown(MouseButton::Middle) ? 0x04 : 0)
			, (int32_t)input.GetMouseScrollOffset().y
			, win->GetSize()
			, -1
			, 255);

		hub.Draw();

		Moonlight::CameraData cam;
		Renderer->Render(cam);
		input.PostUpdate();
	}

	return 0;
}
